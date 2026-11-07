import argparse
from itertools import permutations
import json
from pathlib import Path

import numpy as np


WINDOW_SAMPLES = 512
CHANNEL_COUNT = 3
CLASS_COUNT = 3
RULE_CLASS_NAMES = ("non_fall", "pre_impact", "fall")
SPLITS = ("train", "val", "test")
THRESHOLD_QUANTILES = (
    0.05,
    0.10,
    0.20,
    0.30,
    0.40,
    0.50,
    0.60,
    0.70,
    0.80,
    0.90,
    0.95,
)
MAGNITUDE_BATCH_SIZE = 512


def load_split(data_root: Path, split: str) -> tuple[np.memmap, np.ndarray]:
    x_path = data_root / f"x_{split}_3"
    y_path = data_root / f"y_{split}_3"
    if not x_path.is_file() or not y_path.is_file():
        raise FileNotFoundError(f"Faltan archivos del split {split}: {x_path}, {y_path}")

    label_bytes = y_path.stat().st_size
    if label_bytes == 0 or label_bytes % CLASS_COUNT:
        raise ValueError(
            f"Split {split}: el archivo de etiquetas debe contener vectores "
            f"one-hot de {CLASS_COUNT} valores"
        )
    sample_count = label_bytes // CLASS_COUNT
    one_hot_labels = np.fromfile(y_path, dtype=np.uint8).reshape(
        sample_count, CLASS_COUNT
    )
    if not np.isin(one_hot_labels, (0, 1)).all() or not (
        one_hot_labels.sum(axis=1) == 1
    ).all():
        raise ValueError(f"Split {split}: Y debe contener etiquetas one-hot válidas")
    labels = np.argmax(one_hot_labels, axis=1).astype(np.uint8)

    expected_x_bytes = (
        sample_count
        * WINDOW_SAMPLES
        * CHANNEL_COUNT
        * np.dtype(np.float32).itemsize
    )
    if x_path.stat().st_size != expected_x_bytes:
        raise ValueError(
            f"Split {split}: se esperaban {expected_x_bytes} bytes para "
            f"{sample_count} ventanas de {WINDOW_SAMPLES}x{CHANNEL_COUNT} float32; "
            f"se encontraron {x_path.stat().st_size}"
        )
    windows = np.memmap(
        x_path,
        dtype=np.float32,
        mode="r",
        shape=(sample_count, WINDOW_SAMPLES, CHANNEL_COUNT),
    )
    for start in range(0, sample_count, 1024):
        if not np.isfinite(windows[start : start + 1024]).all():
            raise ValueError(f"Split {split}: X contiene valores NaN o infinitos")
    return windows, labels


def classify_magnitude_window(
    window: np.ndarray, freefall_threshold: float, impact_threshold: float
) -> int:
    magnitudes = np.sqrt(np.square(window).sum(axis=1))
    return int(
        classify_magnitude_vectors(
            magnitudes[np.newaxis, :], freefall_threshold, impact_threshold
        )[0]
    )


def classify_magnitude_vectors(
    magnitudes: np.ndarray, freefall_threshold: float, impact_threshold: float
) -> np.ndarray:
    below_freefall = magnitudes < freefall_threshold
    above_impact = magnitudes > impact_threshold
    has_freefall = below_freefall.any(axis=1)
    first_freefall = below_freefall.argmax(axis=1)
    sample_indices = np.arange(magnitudes.shape[1])[np.newaxis, :]
    impact_after_freefall = np.max(
        np.where(sample_indices > first_freefall[:, np.newaxis], magnitudes, -np.inf),
        axis=1,
    ) > impact_threshold
    has_impact = above_impact.any(axis=1)

    predictions = np.zeros(magnitudes.shape[0], dtype=np.uint8)
    predictions[has_freefall | has_impact] = 1
    predictions[has_freefall & impact_after_freefall] = 2
    return predictions


def magnitude_matrix(windows: np.ndarray) -> np.ndarray:
    magnitudes = np.empty((windows.shape[0], WINDOW_SAMPLES), dtype=np.float32)
    for start in range(0, windows.shape[0], MAGNITUDE_BATCH_SIZE):
        stop = min(start + MAGNITUDE_BATCH_SIZE, windows.shape[0])
        block = np.asarray(windows[start:stop])
        magnitudes[start:stop] = np.sqrt(np.square(block).sum(axis=2))
    return magnitudes


def training_threshold_candidates(
    windows: np.ndarray,
) -> tuple[list[float], list[float]]:
    window_min = np.empty(windows.shape[0], dtype=np.float32)
    window_max = np.empty(windows.shape[0], dtype=np.float32)
    for start in range(0, windows.shape[0], MAGNITUDE_BATCH_SIZE):
        stop = min(start + MAGNITUDE_BATCH_SIZE, windows.shape[0])
        block = np.asarray(windows[start:stop])
        magnitudes = np.sqrt(np.square(block).sum(axis=2))
        window_min[start:stop] = magnitudes.min(axis=1)
        window_max[start:stop] = magnitudes.max(axis=1)

    low_candidates = np.unique(
        np.quantile(window_min, THRESHOLD_QUANTILES)
    ).tolist()
    high_candidates = np.unique(
        np.quantile(window_max, THRESHOLD_QUANTILES)
    ).tolist()
    if not low_candidates or not high_candidates:
        raise ValueError("No se pudieron obtener candidatos para los umbrales")
    return low_candidates, high_candidates


def max_impact_after_first_freefall(
    magnitudes: np.ndarray, freefall_threshold: float
) -> np.ndarray:
    below_freefall = magnitudes < freefall_threshold
    has_freefall = below_freefall.any(axis=1)
    first_freefall = below_freefall.argmax(axis=1)
    sample_indices = np.arange(magnitudes.shape[1])[np.newaxis, :]
    max_after = np.max(
        np.where(sample_indices > first_freefall[:, np.newaxis], magnitudes, -np.inf),
        axis=1,
    )
    max_after[~has_freefall] = -np.inf
    return max_after


def classify_magnitude_vectors_with_max_after(
    magnitudes: np.ndarray,
    max_after_freefall: np.ndarray,
    freefall_threshold: float,
    impact_threshold: float,
) -> np.ndarray:
    has_freefall = (magnitudes < freefall_threshold).any(axis=1)
    has_impact = (magnitudes > impact_threshold).any(axis=1)
    fall = max_after_freefall > impact_threshold
    predictions = np.zeros(magnitudes.shape[0], dtype=np.uint8)
    predictions[has_freefall | has_impact] = 1
    predictions[fall] = 2
    return predictions


def classify_windows(
    windows: np.ndarray, freefall_threshold: float, impact_threshold: float
) -> np.ndarray:
    return classify_magnitude_vectors(
        magnitude_matrix(windows), freefall_threshold, impact_threshold
    )


def classification_metrics(labels: np.ndarray, predictions: np.ndarray) -> dict:
    confusion = np.bincount(
        labels.astype(np.int64) * CLASS_COUNT + predictions.astype(np.int64),
        minlength=CLASS_COUNT * CLASS_COUNT,
    ).reshape(CLASS_COUNT, CLASS_COUNT)
    supports = confusion.sum(axis=1)
    per_class = {}
    precisions = []
    recalls = []
    f1_scores = []
    for class_index, class_name in enumerate(RULE_CLASS_NAMES):
        true_positive = int(confusion[class_index, class_index])
        false_positive = int(confusion[:, class_index].sum() - true_positive)
        false_negative = int(supports[class_index] - true_positive)
        precision = true_positive / max(true_positive + false_positive, 1)
        recall = true_positive / max(true_positive + false_negative, 1)
        f1 = 2 * precision * recall / max(precision + recall, 1e-12)
        per_class[class_name] = {
            "precision": precision,
            "recall": recall,
            "f1": f1,
            "support": int(supports[class_index]),
        }
        precisions.append(precision)
        recalls.append(recall)
        f1_scores.append(f1)

    return {
        "accuracy": float(np.trace(confusion) / max(int(supports.sum()), 1)),
        "macro_precision": float(np.mean(precisions)),
        "macro_recall": float(np.mean(recalls)),
        "macro_f1": float(np.mean(f1_scores)),
        "per_class": per_class,
        "confusion_matrix": confusion.tolist(),
        "confusion_matrix_order": list(RULE_CLASS_NAMES),
    }


def semantic_labels_for_columns(
    labels: np.ndarray, rule_class_columns: tuple[int, ...]
) -> np.ndarray:
    column_to_rule_class = np.empty(CLASS_COUNT, dtype=np.uint8)
    for rule_class, column in enumerate(rule_class_columns):
        column_to_rule_class[column] = rule_class
    return column_to_rule_class[labels]


def fixed_threshold_metrics_by_permutation(
    labels: np.ndarray, predictions: np.ndarray
) -> dict:
    results = {}
    for rule_class_columns in permutations(range(CLASS_COUNT)):
        key = ",".join(str(column) for column in rule_class_columns)
        results[key] = {
            "assumed_one_hot_columns": {
                name: int(column)
                for name, column in zip(RULE_CLASS_NAMES, rule_class_columns)
            },
            **classification_metrics(
                semantic_labels_for_columns(labels, rule_class_columns), predictions
            ),
        }
    return results


def calibrate_rules(
    train_windows: np.ndarray,
    validation_windows: np.ndarray,
    validation_labels: np.ndarray,
) -> tuple[dict, list[float], list[float]]:
    low_candidates, high_candidates = training_threshold_candidates(train_windows)
    validation_magnitudes = magnitude_matrix(validation_windows)
    results = {}

    for rule_class_columns in permutations(range(CLASS_COUNT)):
        semantic_labels = semantic_labels_for_columns(
            validation_labels, rule_class_columns
        )
        best_score = (-1.0, -1.0)
        best_thresholds = None
        best_metrics = None
        for freefall_threshold in low_candidates:
            max_after_freefall = max_impact_after_first_freefall(
                validation_magnitudes, freefall_threshold
            )
            for impact_threshold in high_candidates:
                predictions = classify_magnitude_vectors_with_max_after(
                    validation_magnitudes,
                    max_after_freefall,
                    freefall_threshold,
                    impact_threshold,
                )
                metrics = classification_metrics(semantic_labels, predictions)
                score = (metrics["macro_f1"], metrics["accuracy"])
                if score > best_score:
                    best_score = score
                    best_thresholds = (freefall_threshold, impact_threshold)
                    best_metrics = metrics

        assert best_thresholds is not None and best_metrics is not None
        key = ",".join(str(column) for column in rule_class_columns)
        results[key] = {
            "assumed_one_hot_columns": {
                name: int(column)
                for name, column in zip(RULE_CLASS_NAMES, rule_class_columns)
            },
            "thresholds_raw_units": {
                "freefall": float(best_thresholds[0]),
                "impact": float(best_thresholds[1]),
            },
            "validation_metrics_for_threshold_selection": best_metrics,
        }
    return results, low_candidates, high_candidates


def load_fallarchive_results(metrics_path: Path) -> dict:
    if not metrics_path.is_file():
        raise FileNotFoundError(
            f"No se encontraron las métricas de FallArchive: {metrics_path}. "
            "Entrena primero con train_firmware_fall_model.py."
        )
    metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
    required = ("float_model", "int8_model")
    missing = [key for key in required if key not in metrics]
    if missing:
        raise ValueError(
            f"{metrics_path}: faltan métricas de FallArchive {missing}"
        )
    return {
        "metrics_file": str(metrics_path),
        "test_set": {
            "float_model": metrics["float_model"],
            "int8_model": metrics["int8_model"],
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Evalúa reglas de magnitud en SISFall Three Classes y recopila, "
            "por separado, las métricas TensorFlow de FallArchive."
        )
    )
    parser.add_argument(
        "--data-root",
        type=Path,
        required=True,
        help="Carpeta con x_train_3/y_train_3, x_val_3/y_val_3 y x_test_3/y_test_3",
    )
    parser.add_argument(
        "--fallarchive-metrics",
        type=Path,
        default=Path("models/fall_detector/test_metrics.json"),
        help="test_metrics.json generado por train_firmware_fall_model.py",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("models/three_classes_rules"),
        help="Carpeta donde guardar las métricas de comparación",
    )
    parser.add_argument(
        "--freefall-threshold",
        type=float,
        default=0.5,
        help=(
            "Umbral inferior en unidades crudas para el reporte base "
            "(no se asume que sean g)"
        ),
    )
    parser.add_argument(
        "--impact-threshold",
        type=float,
        default=2.5,
        help=(
            "Umbral superior en unidades crudas para el reporte base "
            "(no se asume que sean g)"
        ),
    )
    args = parser.parse_args()
    if (
        not np.isfinite(args.freefall_threshold)
        or not np.isfinite(args.impact_threshold)
        or args.freefall_threshold < 0
        or args.impact_threshold <= args.freefall_threshold
    ):
        parser.error(
            "Los umbrales deben ser finitos y cumplir "
            "0 <= --freefall-threshold < --impact-threshold"
        )

    data_root = args.data_root.expanduser().resolve()
    fallarchive_metrics_path = args.fallarchive_metrics.expanduser().resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    splits = {split: load_split(data_root, split) for split in SPLITS}
    for split, (_, labels) in splits.items():
        print(
            f"{split}: {labels.size} ventanas; "
            f"conteos one-hot {np.bincount(labels, minlength=CLASS_COUNT).tolist()}"
        )

    x_test, y_test = splits["test"]
    baseline_predictions = classify_windows(
        x_test, args.freefall_threshold, args.impact_threshold
    )
    calibrated, low_candidates, high_candidates = calibrate_rules(
        splits["train"][0], splits["val"][0], splits["val"][1]
    )
    test_magnitudes = magnitude_matrix(x_test)
    for result in calibrated.values():
        thresholds = result["thresholds_raw_units"]
        predictions = classify_magnitude_vectors(
            test_magnitudes, thresholds["freefall"], thresholds["impact"]
        )
        semantic_labels = semantic_labels_for_columns(
            y_test,
            tuple(
                result["assumed_one_hot_columns"][name]
                for name in RULE_CLASS_NAMES
            ),
        )
        result["test_metrics"] = classification_metrics(
            semantic_labels, predictions
        )
        result["test_prediction_counts"] = {
            RULE_CLASS_NAMES[class_index]: int(count)
            for class_index, count in enumerate(
                np.bincount(predictions, minlength=CLASS_COUNT)
            )
        }

    rule_metrics = {
        "dataset": "SisFall Enhanced - Three Classes",
        "split": "test",
        "samples": int(y_test.size),
        "input_shape": [WINDOW_SAMPLES, CHANNEL_COUNT],
        "feature": "per-sample Euclidean magnitude of the three raw channels",
        "channel_meaning": "unverified",
        "units": "unverified; thresholds are in raw dataset units",
        "rule": {
            "non_fall": "no threshold crossing in the window",
            "pre_impact": (
                "freefall or impact threshold crossed, without a low-then-high sequence"
            ),
            "fall": "a low-magnitude sample occurs before a high-magnitude sample",
            "threshold_units": "raw dataset values; physical units not confirmed",
        },
        "fixed_initial_thresholds": {
            "freefall": args.freefall_threshold,
            "impact": args.impact_threshold,
            "prediction_counts": {
                RULE_CLASS_NAMES[class_index]: int(count)
                for class_index, count in enumerate(
                    np.bincount(baseline_predictions, minlength=CLASS_COUNT)
                )
            },
            "metrics_by_assumed_label_permutation": (
                fixed_threshold_metrics_by_permutation(y_test, baseline_predictions)
            ),
        },
        "calibration": {
            "method": (
                "threshold candidates are quantiles of training-window minima/maxima; "
                "macro-F1, then accuracy, selects thresholds on validation data"
            ),
            "selection_split": "val",
            "quantiles": list(THRESHOLD_QUANTILES),
            "freefall_candidate_count": len(low_candidates),
            "impact_candidate_count": len(high_candidates),
            "metrics_by_assumed_label_permutation": calibrated,
            "label_mapping": (
                "unknown; calibration and test metrics are reported for all six "
                "possible one-hot column permutations; none is selected as authoritative"
            ),
        },
    }
    fallarchive_results = load_fallarchive_results(fallarchive_metrics_path)
    comparison = {
        "comparison_type": "independent_test_sets_not_head_to_head",
        "direct_metric_comparison_valid": False,
        "reason": (
            "Three Classes has three unlabeled raw channels and three unknown "
            "one-hot meanings; FallArchive has six physical IMU channels and a "
            "binary FallCheck label. Their test samples and target definitions differ."
        ),
        "three_classes_algorithmic": rule_metrics,
        "fallarchive_tensorflow": fallarchive_results,
        "limitations": [
            "Three Classes magnitude thresholds are raw values; physical units are unverified.",
            "Threshold calibration selects parameters on the validation split and therefore depends on label meaning; all six label permutations are reported.",
            "Three Classes channel meanings, units, sampling rate, and class-column order are unverified.",
            "FallArchive reports binary FallCheck metrics; pre-impact is not a confirmed class.",
            "These independent test metrics cannot establish which system is more accurate on the same events.",
            "Both datasets require validation with wrist-worn recordings before deployment claims.",
        ],
    }

    (output_dir / "three_classes_rules_metrics.json").write_text(
        json.dumps(rule_metrics, indent=2) + "\n", encoding="utf-8"
    )
    (output_dir / "comparison.json").write_text(
        json.dumps(comparison, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Reglas Three Classes: {output_dir / 'three_classes_rules_metrics.json'}")
    print(f"Comparación independiente: {output_dir / 'comparison.json'}")
    for permutation, result in calibrated.items():
        metrics = result["test_metrics"]
        thresholds = result["thresholds_raw_units"]
        print(
            f"Three Classes columnas {permutation}: "
            f"accuracy={metrics['accuracy']:.3f}, "
            f"macro-F1={metrics['macro_f1']:.3f}, "
            f"umbrales=({thresholds['freefall']:.5g}, {thresholds['impact']:.5g})"
        )
    print(
        "FallArchive TFLite int8 test: "
        + json.dumps(fallarchive_results["test_set"]["int8_model"])
    )
    print(
        "No es una comparación directa: los datasets y las etiquetas son distintos."
    )


if __name__ == "__main__":
    main()
