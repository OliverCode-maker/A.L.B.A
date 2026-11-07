import argparse
import csv
from itertools import product
import json
from pathlib import Path

import numpy as np
import tensorflow as tf


WINDOW_SAMPLES = 200
CHANNELS = ("AccX", "AccY", "AccZ", "GyrX", "GyrY", "GyrZ")
SAMPLE_RATE_HZ = 100


def load_file_windows(
    path: Path, window_stride: int
) -> tuple[np.ndarray, np.ndarray]:
    with path.open(newline="", encoding="utf-8-sig") as file:
        reader = csv.reader(file)
        try:
            header = next(reader)
        except StopIteration as exc:
            raise ValueError(f"CSV vacio: {path}") from exc

    required = ("TimeStamp(s)", *CHANNELS, "FallCheck")
    missing = [column for column in required if column not in header]
    if missing:
        raise ValueError(f"{path}: faltan columnas {missing}")
    column_indices = [header.index(column) for column in required]
    rows = np.loadtxt(
        path,
        delimiter=",",
        skiprows=1,
        usecols=column_indices,
        dtype=np.float32,
        ndmin=2,
        encoding="utf-8-sig",
    )
    if rows.shape[0] < WINDOW_SAMPLES:
        return (
            np.empty((0, WINDOW_SAMPLES, len(CHANNELS)), dtype=np.float32),
            np.empty((0,), dtype=np.uint8),
        )
    if not np.isfinite(rows).all():
        raise ValueError(f"{path}: contiene valores NaN o infinitos")

    timestamps = rows[:, 0]
    intervals = np.diff(timestamps)
    if intervals.size and not np.allclose(
        intervals, 1.0 / SAMPLE_RATE_HZ, atol=0.001, rtol=0
    ):
        raise ValueError(f"{path}: frecuencia de muestreo distinta de 100 Hz")

    sample_labels = rows[:, -1]
    if not np.isin(sample_labels, (0.0, 1.0)).all():
        raise ValueError(f"{path}: FallCheck debe contener solo 0 y 1")
    sample_labels = sample_labels.astype(np.uint8)
    starts = np.arange(0, rows.shape[0] - WINDOW_SAMPLES + 1, window_stride)

    label_prefix_sum = np.concatenate(
        (np.zeros(1, dtype=np.int64), np.cumsum(sample_labels, dtype=np.int64))
    )
    window_labels = (
        label_prefix_sum[starts + WINDOW_SAMPLES] - label_prefix_sum[starts]
    ) > 0
    sensor_values = rows[:, 1:-1]
    windows = np.lib.stride_tricks.sliding_window_view(
        sensor_values, WINDOW_SAMPLES, axis=0
    ).transpose(0, 2, 1)[starts]
    return np.ascontiguousarray(windows), window_labels.astype(np.uint8)


def load_subjects(
    data_root: Path, split_name: str, subjects: list[str], window_stride: int
) -> tuple[np.ndarray, np.ndarray]:
    windows_by_file = []
    labels_by_file = []
    split_root = data_root / split_name
    for subject in subjects:
        subject_dir = split_root / subject
        files = sorted(subject_dir.glob("*.csv"))
        if not files:
            raise FileNotFoundError(f"No hay CSV para {subject_dir}")
        for path in files:
            windows, labels = load_file_windows(path, window_stride)
            if labels.size:
                windows_by_file.append(windows)
                labels_by_file.append(labels)
        print(f"{split_name}/{subject}: {len(files)} registros")
    if not windows_by_file:
        raise ValueError(f"No se generaron ventanas para {split_name}: {subjects}")
    return np.concatenate(windows_by_file), np.concatenate(labels_by_file)


def random_rotation_matrices(
    rng: np.random.Generator, count: int
) -> np.ndarray:
    """Sample uniform 3D rotations for independent sensor windows."""
    u1, u2, u3 = rng.random((3, count))
    qx = np.sqrt(1 - u1) * np.sin(2 * np.pi * u2)
    qy = np.sqrt(1 - u1) * np.cos(2 * np.pi * u2)
    qz = np.sqrt(u1) * np.sin(2 * np.pi * u3)
    qw = np.sqrt(u1) * np.cos(2 * np.pi * u3)

    matrices = np.empty((count, 3, 3), dtype=np.float32)
    matrices[:, 0, 0] = 1 - 2 * (qy * qy + qz * qz)
    matrices[:, 0, 1] = 2 * (qx * qy - qz * qw)
    matrices[:, 0, 2] = 2 * (qx * qz + qy * qw)
    matrices[:, 1, 0] = 2 * (qx * qy + qz * qw)
    matrices[:, 1, 1] = 1 - 2 * (qx * qx + qz * qz)
    matrices[:, 1, 2] = 2 * (qy * qz - qx * qw)
    matrices[:, 2, 0] = 2 * (qx * qz - qy * qw)
    matrices[:, 2, 1] = 2 * (qy * qz + qx * qw)
    matrices[:, 2, 2] = 1 - 2 * (qx * qx + qy * qy)
    return matrices


def augment_orientation_windows(
    windows: np.ndarray,
    rng: np.random.Generator,
    copies: int,
    acceleration_gain_range: tuple[float, float],
    batch_size: int = 256,
) -> np.ndarray:
    """Augment sensor orientation and accelerometer scale per window."""
    if copies < 0:
        raise ValueError("El numero de rotaciones debe ser >= 0")
    if (
        acceleration_gain_range[0] <= 0
        or acceleration_gain_range[1] < acceleration_gain_range[0]
    ):
        raise ValueError("Rango de ganancia de aceleracion invalido")
    if batch_size < 1:
        raise ValueError("El tamano de lote de aumentacion debe ser positivo")
    if windows.ndim != 3 or windows.shape[2] != len(CHANNELS):
        raise ValueError(
            f"Forma de ventanas inesperada para rotacion: {windows.shape}"
        )

    augmented = np.empty(
        (windows.shape[0] * copies, *windows.shape[1:]), dtype=np.float32
    )
    for copy_index in range(copies):
        copy_start = copy_index * windows.shape[0]
        for start in range(0, windows.shape[0], batch_size):
            stop = min(start + batch_size, windows.shape[0])
            batch = windows[start:stop]
            rotations = random_rotation_matrices(rng, stop - start)
            gains = rng.uniform(
                *acceleration_gain_range, size=(stop - start, 1, 1)
            ).astype(np.float32)
            rotated = augmented[copy_start + start : copy_start + stop]
            rotated[..., :3] = np.einsum(
                "ntc,ndc->ntd", batch[..., :3] * gains, rotations
            )
            rotated[..., 3:] = np.einsum(
                "ntc,ndc->ntd", batch[..., 3:], rotations
            )
    return augmented


def stationary_orientation_windows(
    normalization_mean: np.ndarray,
    normalization_scale: np.ndarray,
    accelerometer_magnitudes: tuple[float, ...],
    gyro_profiles: tuple[tuple[float, float, float], ...],
) -> tuple[np.ndarray, list[dict]]:
    directions = np.asarray(
        list(product((-1.0, 0.0, 1.0), repeat=3)), dtype=np.float32
    )
    directions = directions[np.linalg.norm(directions, axis=1) > 0]
    directions /= np.linalg.norm(directions, axis=1, keepdims=True)
    cases = [
        {
            "acceleration_g": float(magnitude),
            "direction": direction.tolist(),
            "gyro_dps": list(gyro),
        }
        for magnitude in accelerometer_magnitudes
        for gyro in gyro_profiles
        for direction in directions
    ]
    raw_windows = np.zeros(
        (len(cases), WINDOW_SAMPLES, len(CHANNELS)), dtype=np.float32
    )
    for index, case in enumerate(cases):
        raw_windows[index, :, :3] = (
            np.asarray(case["direction"], dtype=np.float32)
            * case["acceleration_g"]
        )
        raw_windows[index, :, 3:] = np.asarray(
            case["gyro_dps"], dtype=np.float32
        )
    normalized_windows = (raw_windows - normalization_mean) / normalization_scale
    return normalized_windows, cases


def build_model() -> tf.keras.Model:
    model = tf.keras.Sequential(
        [
            tf.keras.layers.Input(
                shape=(WINDOW_SAMPLES, len(CHANNELS)), name="imu_window"
            ),
            tf.keras.layers.Conv1D(32, 5, activation="relu"),
            tf.keras.layers.BatchNormalization(),
            tf.keras.layers.MaxPooling1D(2),
            tf.keras.layers.Conv1D(64, 3, activation="relu"),
            tf.keras.layers.BatchNormalization(),
            tf.keras.layers.MaxPooling1D(2),
            tf.keras.layers.Flatten(),
            tf.keras.layers.Dense(64, activation="relu"),
            tf.keras.layers.Dropout(0.3),
            tf.keras.layers.Dense(1, activation="sigmoid", name="fall_probability"),
        ],
        name="sisfall_firmware_cnn",
    )
    model.compile(
        optimizer="adam",
        loss="binary_crossentropy",
        metrics=[
            tf.keras.metrics.BinaryAccuracy(name="accuracy"),
            tf.keras.metrics.AUC(name="auc"),
            tf.keras.metrics.Precision(name="precision"),
            tf.keras.metrics.Recall(name="recall"),
        ],
    )
    return model


def classification_metrics(
    labels: np.ndarray, probabilities: np.ndarray, threshold: float
) -> dict:
    predictions = probabilities >= threshold
    positives = labels == 1
    negatives = labels == 0
    true_positive = int(np.count_nonzero(predictions & positives))
    false_positive = int(np.count_nonzero(predictions & negatives))
    false_negative = int(np.count_nonzero(~predictions & positives))
    true_negative = int(np.count_nonzero(~predictions & negatives))
    precision = true_positive / max(true_positive + false_positive, 1)
    recall = true_positive / max(true_positive + false_negative, 1)
    return {
        "threshold": threshold,
        "accuracy": float((true_positive + true_negative) / labels.size),
        "precision": precision,
        "recall": recall,
        "f1": 2 * precision * recall / max(precision + recall, 1e-12),
        "true_positive": true_positive,
        "false_positive": false_positive,
        "false_negative": false_negative,
        "true_negative": true_negative,
    }


def export_int8_model(
    model: tf.keras.Model,
    train_windows: np.ndarray,
    train_labels: np.ndarray,
    output_path: Path,
    seed: int,
) -> None:
    rng = np.random.default_rng(seed)
    representative_indices = []
    for label in (0, 1):
        indices = np.flatnonzero(train_labels == label)
        if not indices.size:
            raise ValueError(f"No hay ejemplos de entrenamiento de la clase {label}")
        representative_indices.extend(
            rng.choice(indices, min(256, indices.size), replace=False).tolist()
        )
    rng.shuffle(representative_indices)

    def representative_dataset():
        for index in representative_indices:
            yield [train_windows[index : index + 1]]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    output_path.write_bytes(converter.convert())


def quantize_input(window: np.ndarray, scale: float, zero_point: int) -> np.ndarray:
    quantized = (
        window.astype(np.float32, copy=False) / np.float32(scale)
        + np.float32(zero_point)
    )
    rounded = np.where(
        quantized >= 0,
        np.floor(quantized + 0.5),
        np.ceil(quantized - 0.5),
    )
    return np.clip(rounded, -128, 127).astype(np.int8)


def predict_int8_model(
    model_path: Path, windows: np.ndarray
) -> tuple[np.ndarray, dict, list[str]]:
    interpreter = tf.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    expected_shape = [1, WINDOW_SAMPLES, len(CHANNELS)]
    if input_details["dtype"] != np.int8 or output_details["dtype"] != np.int8:
        raise RuntimeError("La exportacion TFLite no produjo entrada y salida int8")
    if input_details["shape"].tolist() != expected_shape:
        raise RuntimeError(
            f"Forma TFLite inesperada: {input_details['shape'].tolist()} "
            f"(esperada {expected_shape})"
        )

    input_scale, input_zero_point = input_details["quantization"]
    output_scale, output_zero_point = output_details["quantization"]
    if input_scale <= 0 or output_scale <= 0:
        raise RuntimeError("El modelo TFLite no contiene escalas de cuantizacion validas")

    probabilities = np.empty(windows.shape[0], dtype=np.float32)
    for index, window in enumerate(windows):
        quantized_window = quantize_input(window, input_scale, input_zero_point)
        interpreter.set_tensor(
            input_details["index"], quantized_window[np.newaxis, ...]
        )
        interpreter.invoke()
        quantized_output = interpreter.get_tensor(output_details["index"]).item()
        probabilities[index] = (quantized_output - output_zero_point) * output_scale

    tensor_metadata = {
        "input_shape": input_details["shape"].tolist(),
        "input_dtype": str(input_details["dtype"]),
        "input_scale": float(input_scale),
        "input_zero_point": int(input_zero_point),
        "output_shape": output_details["shape"].tolist(),
        "output_dtype": str(output_details["dtype"]),
        "output_scale": float(output_scale),
        "output_zero_point": int(output_zero_point),
    }
    operator_names = [
        operation["op_name"] for operation in interpreter._get_ops_details()
    ]
    return probabilities, tensor_metadata, operator_names


def evaluate_int8_model(
    model_path: Path, windows: np.ndarray, labels: np.ndarray, threshold: float
) -> tuple[dict, dict, list[str]]:
    probabilities, tensor_metadata, operator_names = predict_int8_model(
        model_path, windows
    )
    return (
        classification_metrics(labels, probabilities, threshold),
        tensor_metadata,
        operator_names,
    )


def write_model_array(model_path: Path, output_path: Path) -> None:
    model_bytes = model_path.read_bytes()
    lines = []
    for offset in range(0, len(model_bytes), 16):
        values = ", ".join(f"0x{value:02x}" for value in model_bytes[offset : offset + 16])
        lines.append(f"    {values},")
    source = (
        "#include <stdint.h>\n\n"
        "const unsigned char fall_detector_model_data[] = {\n"
        + "\n".join(lines)
        + "\n};\n\n"
        "const unsigned int fall_detector_model_data_len = "
        f"{len(model_bytes)}u;\n"
    )
    output_path.write_text(source, encoding="ascii")


def write_preprocess_header(
    output_path: Path,
    threshold: float,
    tensor_metadata: dict,
    normalization_mean: np.ndarray,
    normalization_scale: np.ndarray,
) -> None:
    means = ", ".join(f"{value:.10g}f" for value in normalization_mean)
    scales = ", ".join(f"{value:.10g}f" for value in normalization_scale)
    source = (
        "#ifndef FALL_MODEL_PREPROCESS_H\n"
        "#define FALL_MODEL_PREPROCESS_H\n\n"
        f"#define FALL_MODEL_THRESHOLD {threshold:.8g}f\n"
        f"#define FALL_MODEL_INPUT_SCALE {tensor_metadata['input_scale']:.10g}f\n"
        f"#define FALL_MODEL_INPUT_ZERO_POINT {tensor_metadata['input_zero_point']}\n"
        f"#define FALL_MODEL_OUTPUT_SCALE {tensor_metadata['output_scale']:.10g}f\n"
        f"#define FALL_MODEL_OUTPUT_ZERO_POINT {tensor_metadata['output_zero_point']}\n\n"
        f"static const float fall_model_mean[{len(CHANNELS)}] = {{{means}}};\n"
        f"static const float fall_model_scale[{len(CHANNELS)}] = {{{scales}}};\n\n"
        "#endif\n"
    )
    output_path.write_text(source, encoding="ascii")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Entrena y exporta la CNN binaria SisFall para el IMU 200x6."
    )
    parser.add_argument(
        "--data-root",
        type=Path,
        required=True,
        help="Carpeta con Sample_Training/<sujeto> y Sample_Test/<sujeto>",
    )
    parser.add_argument("--output-dir", type=Path, default=Path("models/fall_detector"))
    parser.add_argument("--epochs", type=int, default=15)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--window-stride", type=int, default=25)
    parser.add_argument("--validation-fraction", type=float, default=0.2)
    parser.add_argument(
        "--orientation-augmentation",
        type=int,
        default=1,
        help="Rotaciones 3D aleatorias adicionales por ventana de entrenamiento",
    )
    parser.add_argument("--acceleration-gain-min", type=float, default=0.85)
    parser.add_argument("--acceleration-gain-max", type=float, default=1.15)
    parser.add_argument("--threshold", type=float, default=0.6)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()
    if args.epochs < 1 or args.batch_size < 1 or args.window_stride < 1:
        parser.error("--epochs, --batch-size y --window-stride deben ser positivos")
    if args.orientation_augmentation < 0:
        parser.error("--orientation-augmentation no puede ser negativo")
    if (
        args.acceleration_gain_min <= 0
        or args.acceleration_gain_max < args.acceleration_gain_min
    ):
        parser.error("El rango de ganancia de aceleracion debe ser positivo y ordenado")
    if not 0 < args.validation_fraction < 1:
        parser.error("--validation-fraction debe estar entre 0 y 1")
    if not 0 < args.threshold < 1:
        parser.error("--threshold debe estar entre 0 y 1")

    tf.keras.utils.set_random_seed(args.seed)
    data_root = args.data_root.expanduser().resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    train_root = data_root / "Sample_Training"
    test_root = data_root / "Sample_Test"
    train_subjects = sorted(path.name for path in train_root.iterdir() if path.is_dir())
    test_subjects = sorted(path.name for path in test_root.iterdir() if path.is_dir())
    if not train_subjects or not test_subjects:
        raise FileNotFoundError(
            f"Se requieren sujetos en {train_root} y {test_root}"
        )
    overlapping_subjects = set(train_subjects) & set(test_subjects)
    if overlapping_subjects:
        raise ValueError(
            f"Sujetos presentes en entrenamiento y prueba: "
            f"{sorted(overlapping_subjects)}"
        )

    rng = np.random.default_rng(args.seed)
    validation_count = max(1, round(len(train_subjects) * args.validation_fraction))
    validation_subjects = sorted(
        rng.choice(train_subjects, validation_count, replace=False).tolist()
    )
    fit_subjects = sorted(set(train_subjects) - set(validation_subjects))
    if not fit_subjects:
        raise ValueError("Se necesita al menos un sujeto para entrenamiento")
    x_train, y_train = load_subjects(
        data_root, "Sample_Training", fit_subjects, args.window_stride
    )
    x_val, y_val = load_subjects(
        data_root, "Sample_Training", validation_subjects, args.window_stride
    )
    x_test, y_test = load_subjects(
        data_root, "Sample_Test", test_subjects, args.window_stride
    )
    if any(np.unique(labels).size != 2 for labels in (y_train, y_val, y_test)):
        raise ValueError("Cada split debe contener ambas clases FallCheck 0 y 1")

    original_training_windows = int(y_train.size)
    if args.orientation_augmentation:
        augmentation_rng = np.random.default_rng(args.seed + 1)
        augmented_windows = augment_orientation_windows(
            x_train,
            augmentation_rng,
            args.orientation_augmentation,
            (args.acceleration_gain_min, args.acceleration_gain_max),
        )
        x_train = np.concatenate((x_train, augmented_windows), axis=0)
        y_train = np.concatenate(
            (
                y_train,
                np.tile(y_train, args.orientation_augmentation),
            )
        )

    normalization_mean = x_train.mean(axis=(0, 1))
    normalization_scale = x_train.std(axis=(0, 1))
    if not np.isfinite(normalization_scale).all() or np.any(normalization_scale <= 0):
        raise ValueError("No se pudieron calcular escalas de normalizacion validas")
    for windows in (x_train, x_val, x_test):
        windows -= normalization_mean
        windows /= normalization_scale

    class_counts = np.bincount(y_train, minlength=2)
    class_weights = {
        label: float(y_train.size / (2 * count))
        for label, count in enumerate(class_counts)
    }
    print(f"Entrenamiento: {x_train.shape}; etiquetas: {np.bincount(y_train)}")
    print(f"Validacion:    {x_val.shape}; etiquetas: {np.bincount(y_val)}")
    print(f"Prueba:        {x_test.shape}; etiquetas: {np.bincount(y_test)}")
    print(f"Clases: 0=No fall, 1=FallCheck; pesos: {class_weights}")
    print(f"Sujetos de validacion: {validation_subjects}")

    model = build_model()
    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor="val_auc", patience=5, restore_best_weights=True, mode="max"
        ),
        tf.keras.callbacks.ReduceLROnPlateau(
            monitor="val_loss", patience=3, factor=0.5
        ),
    ]
    history = model.fit(
        x_train,
        y_train,
        validation_data=(x_val, y_val),
        epochs=args.epochs,
        batch_size=args.batch_size,
        class_weight=class_weights,
        callbacks=callbacks,
        verbose=2,
    )

    keras_path = output_dir / "fall_detector.keras"
    tflite_path = output_dir / "fall_detector_int8.tflite"
    model.save(keras_path)
    float_probabilities = model.predict(
        x_test, batch_size=args.batch_size, verbose=0
    ).ravel()
    float_metrics = classification_metrics(y_test, float_probabilities, args.threshold)
    export_int8_model(model, x_train, y_train, tflite_path, args.seed)
    int8_metrics, tensor_metadata, operators = evaluate_int8_model(
        tflite_path, x_test, y_test, args.threshold
    )
    validation_metrics, _, _ = evaluate_int8_model(
        tflite_path, x_val, y_val, args.threshold
    )
    stationary_windows, stationary_cases = stationary_orientation_windows(
        normalization_mean,
        normalization_scale,
        (0.85, 0.87, 0.9, 0.95, 1.0, 1.05, 1.1),
        ((0.0, 0.0, 0.0), (0.5, 1.2, -0.4)),
    )
    stationary_probabilities, _, _ = predict_int8_model(
        tflite_path, stationary_windows
    )
    stationary_max_by_magnitude = {}
    for magnitude in (0.85, 0.87, 0.9, 0.95, 1.0, 1.05, 1.1):
        indices = [
            index
            for index, case in enumerate(stationary_cases)
            if case["acceleration_g"] == magnitude
        ]
        worst_index = max(indices, key=lambda index: stationary_probabilities[index])
        stationary_max_by_magnitude[str(magnitude)] = {
            "max_probability": float(stationary_probabilities[worst_index]),
            "worst_case": stationary_cases[worst_index],
        }
    stationary_probe = {
        "orientations_tested_per_magnitude_and_gyro_profile": 26,
        "gyro_profiles_dps": [[0.0, 0.0, 0.0], [0.5, 1.2, -0.4]],
        "accelerometer_magnitudes_g": [0.85, 0.87, 0.9, 0.95, 1.0, 1.05, 1.1],
        "max_probability_by_magnitude_g": stationary_max_by_magnitude,
        "threshold": args.threshold,
        "max_probability": float(np.max(stationary_probabilities)),
        "all_below_threshold": bool(
            np.all(stationary_probabilities < args.threshold)
        ),
    }

    model_metadata = {
        "dataset": "SisFall fallarchive raw CSV",
        "classes": {"0": "FallCheck=0", "1": "FallCheck=1"},
        "preimpact_class_confirmed": False,
        "input": {
            "shape": [WINDOW_SAMPLES, len(CHANNELS)],
            "channels": list(CHANNELS),
            "firmware_axis_transform": {
                "AccX": "MPU6050.AccX",
                "AccY": "-MPU6050.AccZ",
                "AccZ": "MPU6050.AccY",
                "GyrX": "MPU6050.GyrX",
                "GyrY": "-MPU6050.GyrZ",
                "GyrZ": "MPU6050.GyrY",
            },
            "units": {"AccX": "g", "AccY": "g", "AccZ": "g",
                      "GyrX": "degrees/second", "GyrY": "degrees/second",
                      "GyrZ": "degrees/second"},
            "sample_rate_hz": SAMPLE_RATE_HZ,
            "window_stride_samples": args.window_stride,
            "window_label_rule": "any FallCheck=1 in window",
            "decision_threshold": args.threshold,
            "normalization": (
                "z-score per channel, fitted on original and orientation-augmented "
                "training subjects only"
            ),
            "normalization_mean": normalization_mean.tolist(),
            "normalization_scale": normalization_scale.tolist(),
        },
        "architecture": "Conv1D(32,5)-BN-MaxPool-Conv1D(64,3)-BN-MaxPool-Flatten-Dense(64)-sigmoid",
        "training_subjects": fit_subjects,
        "validation_subjects": validation_subjects,
        "test_subjects": test_subjects,
        "class_weights": {str(label): weight for label, weight in class_weights.items()},
        "training_windows": int(y_train.size),
        "original_training_windows": original_training_windows,
        "orientation_augmentation_rotations_per_window": args.orientation_augmentation,
        "acceleration_gain_augmentation_range": [
            args.acceleration_gain_min,
            args.acceleration_gain_max,
        ],
        "validation_windows": int(y_val.size),
        "test_windows": int(y_test.size),
        "epochs_completed": len(history.history["loss"]),
        "test_metrics_float_model": float_metrics,
        "validation_metrics_int8_model": validation_metrics,
        "test_metrics_int8_model": int8_metrics,
        "stationary_orientation_probe_int8": stationary_probe,
        "tflite_tensors": tensor_metadata,
        "tflite_operators": operators,
        "firmware_input_shape": [1, WINDOW_SAMPLES, len(CHANNELS)],
    }
    (output_dir / "metadata.json").write_text(
        json.dumps(model_metadata, indent=2) + "\n", encoding="utf-8"
    )
    (output_dir / "test_metrics.json").write_text(
        json.dumps(
            {
                "threshold": args.threshold,
                "validation_int8_model": validation_metrics,
                "float_model": float_metrics,
                "int8_model": int8_metrics,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    write_model_array(tflite_path, output_dir / "fall_detector_model_data.c")
    write_preprocess_header(
        output_dir / "fall_model_preprocess.h",
        args.threshold,
        tensor_metadata,
        normalization_mean,
        normalization_scale,
    )
    print(f"TFLite int8: {tflite_path}")
    print(f"Métricas int8 de prueba: {json.dumps(int8_metrics)}")
    print(
        "Probabilidad máxima en reposo (364 combinaciones): "
        f"{stationary_probe['max_probability']:.4f}"
    )
    if not stationary_probe["all_below_threshold"]:
        print(
            "ADVERTENCIA: la prueba de orientación estática supera el umbral; "
            "la aumentación no eliminó todos los falsos positivos en reposo."
        )
    print(f"Operadores TFLite: {operators}")


if __name__ == "__main__":
    main()
