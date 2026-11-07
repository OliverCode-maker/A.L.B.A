/**
 * band_tasks.c
 * Las cuatro tareas de la pulsera y como se hablan entre si.
 *
 *   IMU --100 Hz--> tFall --------------+
 *                    |  ^               |  qTx (alertas, telemetria)
 *      actividad     |  | qCmd          v
 *      (cada 1 s)    |  | (CANCELAR) +-------+  esp_now_send   +-------+
 *                    v  |            | tComm | ---------------> | robot |
 *   PPG --15-300 s-> tVitals ------->|       | <--------------- |       |
 *                    ^   |  qTx      +-------+  HORA, CANCELAR  +-------+
 *                    |   |              ^ qRx (desde el callback de la radio)
 *      hora local    |   v latest
 *      (band_clock)  |  tSystem --- telemetria cada 30 s + bateria + estadisticas
 *
 * REGLA DE ORO: cada dato tiene UN dueno. Lo que otra tarea necesita se pide
 * por una cola o por una funcion con mutex; nunca se toca el struct ajeno.
 *
 *   dato                       dueno      quien mas lo toca          como
 *   fall_detector_t  g_fall    tFall      -                          (nadie)
 *   wake_detector_t  g_wake    tVitals    -                          (nadie)
 *   band_cancel_gate g_gate    tComm      -                          (nadie)
 *   band_battery_t   g_bat     tSystem    -                          (nadie)
 *   band_clock_t     g_clock   tComm      tVitals lee la hora        g_clock_mtx
 *   band_activity_t  g_act     tFall      tVitals la consume         g_act_mtx
 *   latest (bpm/spo2)          tVitals    tSystem lee                g_latest_mtx
 *   cancelar una caida                    tComm -> tFall             qCmd
 *   alertas y telemetria                  cualquiera -> tComm        qTx
 *   mensajes del robot                    radio -> tComm             qRx
 *
 * Los detectores NO son thread-safe; por eso cada uno vive en una sola tarea.
 */
#include "band_tasks.h"

#include <math.h>
#include <string.h>

#include "alba_codec.h"
#include "band_activity.h"
#include "band_alerts.h"
#include "band_battery.h"
#include "band_clock.h"
#include "band_config.h"
#include "band_os.h"
#include "band_port.h"
#include "fall_detector.h"
#include "fall_model.h"
#include "../models/fall_detector/fall_model_preprocess.h"
#include "wake_detector.h"

/* ======================================================================== */
/*  Estado                                                                  */
/* ======================================================================== */

/* Grandes: static, nunca en el stack de una tarea. */
static fall_detector_t g_fall;      /* dueno: tFall    */
static wake_detector_t g_wake;      /* dueno: tVitals  */

static band_clock_t        g_clock;
static SemaphoreHandle_t g_clock_mtx;
static band_activity_t     g_act;
static SemaphoreHandle_t g_act_mtx;
static band_cancel_gate_t  g_gate;        /* dueno: tComm   */
static band_battery_t      g_bat;         /* dueno: tSystem */

typedef struct { bool valid; uint8_t bpm, spo2; uint32_t t_s; } latest_t;
static latest_t g_latest;
static SemaphoreHandle_t g_latest_mtx;

static band_stats_t g_stats;
static int g_model_init_status;

static QueueHandle_t q_tx, q_rx, q_cmd, q_model_window, q_model_result;
static TaskHandle_t  h_fall, h_vitals, h_comm, h_model, h_system;
static fall_model_window_t g_model_window;

enum { TX_ALERT = 1, TX_TELEMETRY = 2 };
enum { CMD_CANCEL_FALL = 1 };

typedef struct {
    uint8_t      kind;
    uint32_t     t_ms;
    band_alert_t alert;                         /* si kind == TX_ALERT      */
    uint8_t      bpm, spo2, bat;                /* si kind == TX_TELEMETRY  */
} tx_item_t;

typedef struct { uint8_t len; uint8_t buf[BAND_RX_MAX_LEN]; } rx_item_t;
typedef struct {
    float probability, peak_g;
    uint32_t window_t_ms;
    fall_rule_class_t rule_class;
} fall_model_result_t;

static uint32_t g_rule_model_comparison[2][2];
static uint32_t g_rule_class_counts[FALL_RULE_CLASS_COUNT];

#define STAT_INC(field)  __atomic_fetch_add(&g_stats.field, 1u, __ATOMIC_RELAXED)

band_stats_t band_stats_snapshot(void) { return g_stats; }

/* ======================================================================== */
/*  Ayudas                                                                  */
/* ======================================================================== */

/** Encola una alerta para tComm. Una alerta de caida es critica: se espera un
 *  poco por si la cola esta llena antes de descartarla. */
static void send_alert(const band_alert_t *a, uint32_t t_ms)
{
    tx_item_t it;
    memset(&it, 0, sizeof it);
    it.kind  = TX_ALERT;
    it.t_ms  = t_ms;
    it.alert = *a;
    if (xQueueSend(q_tx, &it, pdMS_TO_TICKS(20)) != pdTRUE) {
        STAT_INC(tx_dropped);
        port_log("[sistema] COLA DE ENVIO LLENA: alerta %u descartada", (unsigned)a->alerta_tipo);
    }
}

static uint8_t hour_now(uint32_t now_ms)
{
    uint8_t h = WAKE_HOUR_UNKNOWN;
    if (xSemaphoreTake(g_clock_mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
        h = band_clock_hour(&g_clock, now_ms);
        xSemaphoreGive(g_clock_mtx);
    }
    return h;
}

static void handle_fall_event(fall_event_t ev, uint32_t t_ms, float probability)
{
    if (ev == FALL_EVT_NONE) return;

    const fall_features_t *f = fall_detector_features(&g_fall);
    band_alert_t alert;
    if (band_alert_from_fall(ev, f, &alert)) {
        switch (ev) {
        case FALL_EVT_CANDIDATE:
            STAT_INC(falls_candidate);
            port_log("[caidas] POSIBLE CAIDA: modelo %.2f, pico %u mg. "
                     "Esperando cancelacion...", (double)probability,
                     (unsigned)alert.valor_pico);
            break;
        case FALL_EVT_CONFIRMED:
            STAT_INC(falls_confirmed);
            port_log("[caidas] CAIDA CONFIRMADA (nadie cancelo)");
            break;
        case FALL_EVT_CANCELLED:
            STAT_INC(falls_cancelled);
            port_log("[caidas] caida CANCELADA");
            break;
        default:
            break;
        }
        send_alert(&alert, t_ms);
    }
}

static float window_accel_magnitude(const fall_model_window_t *window,
                                    uint16_t sample_index)
{
    const float *sample =
        &window->values[(size_t)sample_index * FALL_MODEL_CHANNELS];
    return sqrtf(sample[0] * sample[0] + sample[1] * sample[1] +
                 sample[2] * sample[2]);
}

static float window_gyro_magnitude(const fall_model_window_t *window,
                                   uint16_t sample_index)
{
    const float *sample =
        &window->values[(size_t)sample_index * FALL_MODEL_CHANNELS + 3u];
    return sqrtf(sample[0] * sample[0] + sample[1] * sample[1] +
                 sample[2] * sample[2]);
}

static float window_orientation_change(const fall_model_window_t *window,
                                      uint16_t event_index)
{
    const uint16_t radius =
        (uint16_t)(BAND_RULE_CONFIRM_WINDOW_MS / BAND_IMU_PERIOD_MS / 2u);
    if (event_index < radius ||
        event_index + radius > FALL_MODEL_WINDOW_SAMPLES) {
        return 0.0f;
    }

    float before[3] = {0.0f, 0.0f, 0.0f};
    float after[3] = {0.0f, 0.0f, 0.0f};
    for (uint16_t offset = 0; offset < radius; offset++) {
        const float *pre =
            &window->values[(size_t)(event_index - radius + offset) *
                            FALL_MODEL_CHANNELS];
        const float *post =
            &window->values[(size_t)(event_index + offset) *
                            FALL_MODEL_CHANNELS];
        for (uint8_t axis = 0; axis < 3u; axis++) {
            before[axis] += pre[axis];
            after[axis] += post[axis];
        }
    }

    const float before_norm =
        sqrtf(before[0] * before[0] + before[1] * before[1] +
              before[2] * before[2]);
    const float after_norm =
        sqrtf(after[0] * after[0] + after[1] * after[1] +
              after[2] * after[2]);
    if (before_norm < 1e-6f || after_norm < 1e-6f) return 0.0f;

    float cosine = (before[0] * after[0] + before[1] * after[1] +
                    before[2] * after[2]) /
                   (before_norm * after_norm);
    if (cosine > 1.0f) cosine = 1.0f;
    if (cosine < -1.0f) cosine = -1.0f;
    return acosf(cosine) * 57.29578f;
}

static bool window_post_impact_confirmed(const fall_model_window_t *window,
                                        uint16_t event_index)
{
    const uint16_t post_samples =
        (uint16_t)(BAND_RULE_CONFIRM_WINDOW_MS / BAND_IMU_PERIOD_MS);
    const uint16_t end = (uint16_t)(
        event_index + post_samples < FALL_MODEL_WINDOW_SAMPLES
            ? event_index + post_samples
            : FALL_MODEL_WINDOW_SAMPLES);
    float gyro_peak = 0.0f;
    float magnitude_sum = 0.0f;
    float magnitude_sq_sum = 0.0f;
    uint16_t count = 0;
    for (uint16_t i = event_index; i < end; i++) {
        const float gyro = window_gyro_magnitude(window, i);
        if (gyro > gyro_peak) gyro_peak = gyro;
        const float magnitude = window_accel_magnitude(window, i);
        magnitude_sum += magnitude;
        magnitude_sq_sum += magnitude * magnitude;
        count++;
    }

    if (gyro_peak >= BAND_RULE_CONFIRM_GYRO_DPS ||
        window_orientation_change(window, event_index) >=
            BAND_RULE_CONFIRM_ANGLE_DEG) {
        return true;
    }
    if (count < 2u) return false;
    const float mean = magnitude_sum / (float)count;
    const float variance = magnitude_sq_sum / (float)count - mean * mean;
    return variance >= 0.0f &&
           sqrtf(variance) < BAND_RULE_CONFIRM_POST_STD_G;
}

static fall_rule_class_t classify_rule_window(const fall_model_window_t *window)
{
    const uint16_t min_freefall_samples =
        (uint16_t)(BAND_RULE_FREEFALL_MIN_MS / BAND_IMU_PERIOD_MS);
    const uint16_t max_freefall_samples =
        (uint16_t)(BAND_RULE_FREEFALL_MAX_MS / BAND_IMU_PERIOD_MS);
    const uint16_t impact_samples =
        (uint16_t)(BAND_RULE_IMPACT_WINDOW_MS / BAND_IMU_PERIOD_MS);
    int32_t freefall_start = -1;

    for (uint16_t i = 0; i < FALL_MODEL_WINDOW_SAMPLES; i++) {
        const float acceleration = window_accel_magnitude(window, i);

        if (acceleration < BAND_RULE_FREEFALL_G) {
            if (freefall_start < 0) freefall_start = (int32_t)i;
            continue;
        }

        if (freefall_start >= 0) {
            const uint16_t duration =
                (uint16_t)(i - (uint16_t)freefall_start);
            if (duration >= min_freefall_samples &&
                duration <= max_freefall_samples) {
                const uint16_t impact_end = (uint16_t)(
                    i + impact_samples < FALL_MODEL_WINDOW_SAMPLES
                        ? i + impact_samples
                        : FALL_MODEL_WINDOW_SAMPLES);
                for (uint16_t j = i; j < impact_end; j++) {
                    if (window_accel_magnitude(window, j) >
                        BAND_RULE_IMPACT_G) {
                        return window_post_impact_confirmed(window, j)
                                   ? FALL_RULE_CLASS_FALL
                                   : FALL_RULE_CLASS_PRE_IMPACT;
                    }
                }
                return FALL_RULE_CLASS_PRE_IMPACT;
            }
            freefall_start = -1;
        }

        if (acceleration > BAND_RULE_DIRECT_IMPACT_G) {
            return window_post_impact_confirmed(window, i)
                       ? FALL_RULE_CLASS_FALL
                       : FALL_RULE_CLASS_PRE_IMPACT;
        }
        if (acceleration > BAND_RULE_IMPACT_G) {
            return FALL_RULE_CLASS_PRE_IMPACT;
        }
    }

    if (freefall_start >= 0) {
        const uint16_t duration =
            (uint16_t)(FALL_MODEL_WINDOW_SAMPLES - (uint16_t)freefall_start);
        if (duration >= min_freefall_samples &&
            duration <= max_freefall_samples) {
            return FALL_RULE_CLASS_PRE_IMPACT;
        }
    }
    return FALL_RULE_CLASS_NON_FALL;
}

static const char *rule_class_name(fall_rule_class_t rule_class)
{
    switch (rule_class) {
    case FALL_RULE_CLASS_NON_FALL: return "no-caida";
    case FALL_RULE_CLASS_PRE_IMPACT: return "pre-impacto";
    case FALL_RULE_CLASS_FALL: return "caida";
    default: return "invalida";
    }
}

static void compare_rule_model_window(const fall_model_result_t *result)
{
    if (result->rule_class >= FALL_RULE_CLASS_COUNT) {
        port_log("[comparacion] clase de reglas invalida: %u",
                 (unsigned)result->rule_class);
        return;
    }

    const uint8_t model_fall =
        result->probability >= FALL_MODEL_THRESHOLD ? 1u : 0u;
    g_rule_class_counts[result->rule_class]++;

    if (result->rule_class != FALL_RULE_CLASS_PRE_IMPACT) {
        const uint8_t rule_fall =
            result->rule_class == FALL_RULE_CLASS_FALL ? 1u : 0u;
        g_rule_model_comparison[rule_fall][model_fall]++;
    }
}

/* ======================================================================== */
/*  tFall: IMU a 100 Hz -> reglas y modelo en paralelo                      */
/* ======================================================================== */
static void task_fall(void *)
{
    fall_config_t fall_cfg;
    fall_config_default(&fall_cfg);
    fall_cfg.model_only = true;
    fall_detector_init(&g_fall, &fall_cfg);
    memset(g_rule_model_comparison, 0, sizeof g_rule_model_comparison);
    memset(g_rule_class_counts, 0, sizeof g_rule_class_counts);

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t   t_act_ms  = port_millis();
    uint32_t   t_model_log_ms = 0;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BAND_IMU_PERIOD_MS));

        /* 1) Comandos de otras tareas (el detector solo se toca desde aqui). */
        uint8_t cmd;
        while (xQueueReceive(q_cmd, &cmd, 0) == pdTRUE) {
            if (cmd == CMD_CANCEL_FALL) fall_detector_cancel(&g_fall);
        }

        /* 2) Una muestra y al detector. */
        fall_sample_t s;
        if (!port_imu_read(port_millis(), &s)) { STAT_INC(imu_errors); continue; }

        handle_fall_event(fall_detector_process(&g_fall, &s), s.t_ms, 0.0f);

        if (fall_model_append_sample(&s, &g_model_window)) {
            g_model_window.rule_class = classify_rule_window(&g_model_window);
            if (xQueueOverwrite(q_model_window, &g_model_window) != pdPASS) {
                port_log("[modelo] no se pudo encolar ventana IMU");
            }
        }

        fall_model_result_t model_result;
        while (xQueueReceive(q_model_result, &model_result, 0) == pdTRUE) {
            compare_rule_model_window(&model_result);
            if (t_model_log_ms == 0u ||
                (uint32_t)(model_result.window_t_ms - t_model_log_ms) >= 1000u) {
                const uint32_t compared_windows =
                    g_rule_model_comparison[1][1] +
                    g_rule_model_comparison[1][0] +
                    g_rule_model_comparison[0][1] +
                    g_rule_model_comparison[0][0];
                const float agreement = compared_windows > 0u
                    ? 100.0f *
                          (float)(g_rule_model_comparison[1][1] +
                                  g_rule_model_comparison[0][0]) /
                          (float)compared_windows
                    : 0.0f;
                t_model_log_ms = model_result.window_t_ms;
                port_log("[comparacion] t=%u ms reglas=%s modelo=%s p=%.3f | "
                         "reglas(no/pre/caida)=%u/%u/%u "
                         "TP=%u FN=%u FP=%u TN=%u acuerdo=%.1f%% n=%u",
                         (unsigned)model_result.window_t_ms,
                         rule_class_name(model_result.rule_class),
                         model_result.probability >= FALL_MODEL_THRESHOLD
                             ? "caida" : "no-caida",
                         (double)model_result.probability,
                         (unsigned)g_rule_class_counts[FALL_RULE_CLASS_NON_FALL],
                         (unsigned)g_rule_class_counts[FALL_RULE_CLASS_PRE_IMPACT],
                         (unsigned)g_rule_class_counts[FALL_RULE_CLASS_FALL],
                         (unsigned)g_rule_model_comparison[1][1],
                         (unsigned)g_rule_model_comparison[1][0],
                         (unsigned)g_rule_model_comparison[0][1],
                         (unsigned)g_rule_model_comparison[0][0],
                         (double)agreement, (unsigned)compared_windows);
            }
            if (model_result.probability >= FALL_MODEL_THRESHOLD) {
                const fall_event_t model_event =
                    fall_detector_model_candidate(&g_fall, s.t_ms,
                                                  model_result.peak_g);
                handle_fall_event(model_event, s.t_ms,
                                  model_result.probability);
            }
        }

        /* 3) Cada segundo, la actigrafia para tVitals. */
        if ((uint32_t)(s.t_ms - t_act_ms) >= BAND_ACTIVITY_PERIOD_MS) {
            t_act_ms = s.t_ms;
            const uint8_t pct = fall_detector_take_activity_pct(&g_fall);
            if (xSemaphoreTake(g_act_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
                band_activity_add(&g_act, pct);
                xSemaphoreGive(g_act_mtx);
            }
        }
    }
}

static void task_fall_model(void *)
{
    static fall_model_window_t window;
    if (!fall_model_init()) {
        __atomic_store_n(&g_model_init_status, -1, __ATOMIC_RELEASE);
        vTaskDelete(NULL);
        return;
    }
    __atomic_store_n(&g_model_init_status, 1, __ATOMIC_RELEASE);
    vTaskPrioritySet(NULL, BAND_PRIO_MODEL);

    for (;;) {
        if (xQueueReceive(q_model_window, &window, portMAX_DELAY) != pdTRUE) {
            port_log("[modelo] error al recibir ventana");
            continue;
        }

        fall_model_result_t result;
        result.peak_g = window.peak_g;
        result.window_t_ms = window.t_ms;
        result.rule_class = window.rule_class;
        if (!fall_model_infer(&window, &result.probability)) {
            port_log("[modelo] inferencia fallida; ventana descartada");
            continue;
        }
        if (xQueueSend(q_model_result, &result, 0) != pdTRUE) {
            port_log("[modelo] cola de resultados llena; resultado descartado");
        }
    }
}

/* ======================================================================== */
/*  tVitals: PPG a ritmo variable -> detector de vigilia                    */
/* ======================================================================== */
static void configure_wake(wake_config_t *cfg)
{
    wake_config_default(cfg);
#if BAND_SIM_FAST
    /* Solo para la demo: ventanas de segundos en vez de minutos. */
    cfg->period_sleep_s  = 2;
    cfg->period_rising_s = 1;
    cfg->period_awake_s  = 5;
    cfg->slope_window_s  = 12;
    cfg->rise_confirm_s  = 10;
    cfg->rising_timeout_s = 60;
    cfg->refractory_s    = 120;
#endif
}

static void task_vitals(void *)
{
    wake_config_t cfg;
    configure_wake(&cfg);
    wake_detector_init(&g_wake, &cfg);

    TickType_t last_wake = xTaskGetTickCount();
    wake_state_t prev_state = wake_detector_state(&g_wake);

    for (;;) {
        /* El detector dice cada cuanto medir: poco de noche, mas si sospecha. */
        const uint16_t period_s = wake_detector_sampling_period_s(&g_wake);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS((uint32_t)period_s * 1000u));

        uint8_t bpm = 0, spo2 = 0, sqi = 0;
        if (!port_ppg_read(&bpm, &spo2, &sqi)) { STAT_INC(ppg_errors); continue; }

        uint8_t act = 0;
        if (xSemaphoreTake(g_act_mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
            act = band_activity_take(&g_act);
            xSemaphoreGive(g_act_mtx);
        }

        const uint32_t now_ms = port_millis();
        wake_vital_t v;
        v.t_s     = now_ms / 1000u;
        v.bpm     = bpm;
        v.sqi     = sqi;
        v.act_pct = act;

        /* Lo ultimo que se midio, para la telemetria de tSystem. */
        if (xSemaphoreTake(g_latest_mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
            g_latest.valid = (sqi >= cfg.sqi_min);
            g_latest.bpm   = bpm;
            g_latest.spo2  = spo2;
            g_latest.t_s   = v.t_s;
            xSemaphoreGive(g_latest_mtx);
        }

        const uint8_t hour = hour_now(now_ms);
        const wake_event_t ev = wake_detector_update(&g_wake, &v, hour);

        const wake_state_t st = wake_detector_state(&g_wake);
        if (st != prev_state) {
            static const char *const names[] = { "SUENO", "SUBIENDO", "DESPIERTO" };
            port_log("[vitales] estado %s -> %s (bpm=%u basal=%.0f actividad=%u%% hora=%u)",
                     names[prev_state], names[st], (unsigned)bpm,
                     (double)wake_detector_baseline(&g_wake), (unsigned)act,
                     (unsigned)hour);
            prev_state = st;
        }

        band_alert_t a;
        if (band_alert_from_wake(ev, bpm, &a)) {
            STAT_INC(wake_triggers);
            port_log("[vitales] DESPERTAR DETECTADO a %u bpm", (unsigned)bpm);
            send_alert(&a, now_ms);
        }
    }
}

/* ======================================================================== */
/*  tComm: unica duena de la radio                                          */
/* ======================================================================== */
static uint16_t g_seq;     /* un contador para todo lo que sale; dueno: tComm */

static void comm_transmit(const tx_item_t *it)
{
    const uint16_t seq = g_seq++;
    uint8_t  buf[ALBA_ESPNOW_MAX_PAYLOAD];
    size_t   len = 0;

    if (it->kind == TX_ALERT) {
        alba_event_t p;
        alba_pack_event(&p, seq, it->t_ms, it->alert.alerta_tipo, it->alert.valor_pico,
                        it->alert.confirmacion);
        memcpy(buf, &p, sizeof p);
        len = sizeof p;
        band_cancel_note_sent(&g_gate, &it->alert, seq);   /* arma/cierra la compuerta de cancelacion */
    } else {
        alba_telemetry_t p;
        alba_pack_telemetry(&p, seq, it->t_ms, it->bpm, it->spo2, it->bat);
        memcpy(buf, &p, sizeof p);
        len = sizeof p;
    }

    /* Las alertas se reintentan; la telemetria no (llega otra en 30 s).
     * TODO ESP-NOW: confirmacion (ACK) del robot para eventos criticos. */
    const unsigned tries = (it->kind == TX_ALERT) ? (1u + BAND_TX_RETRIES) : 1u;
    for (unsigned i = 0; i < tries; i++) {
        if (port_send(buf, len)) { STAT_INC(tx_sent); return; }
        if (i + 1u < tries) vTaskDelay(pdMS_TO_TICKS(BAND_TX_RETRY_DELAY_MS));
    }
    STAT_INC(tx_failed);
    port_log("[comm] ENVIO FALLIDO (seq %u, tipo %u)", (unsigned)seq, (unsigned)it->kind);
}

static void comm_handle_rx(const rx_item_t *rx)
{
    const int type = alba_validate(rx->buf, rx->len);

    if (type == ALBA_MSG_HORA) {
        alba_time_t h;
        memcpy(&h, rx->buf, sizeof h);
        bool ok = false;
        if (xSemaphoreTake(g_clock_mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
            ok = band_clock_sync(&g_clock, port_millis(), h.epoch_s, h.utc_offset_min);
            xSemaphoreGive(g_clock_mtx);
        }
        STAT_INC(rx_accepted);
        port_log("[comm] hora recibida del robot: epoch=%lu zona=%+d min -> hora local %u (%s)",
                 (unsigned long)h.epoch_s, (int)h.utc_offset_min,
                 (unsigned)hour_now(port_millis()), ok ? "ok" : "rechazada");
    } else if (type == ALBA_MSG_CANCELAR) {
        alba_cancel_t c;
        memcpy(&c, rx->buf, sizeof c);
        if (band_cancel_accept(&g_gate, c.ref_seq)) {
            const uint8_t cmd = CMD_CANCEL_FALL;
            xQueueSend(q_cmd, &cmd, 0);
            STAT_INC(rx_accepted);
            port_log("[comm] CANCELAR aceptado (ref_seq %u)", (unsigned)c.ref_seq);
        } else {
            STAT_INC(rx_ignored);
            port_log("[comm] CANCELAR ignorado: no hay caida pendiente con seq %u", (unsigned)c.ref_seq);
        }
    } else {
        STAT_INC(rx_ignored);
        port_log("[comm] mensaje descartado (validacion = %d)", type);
    }
}

static void task_comm(void *)
{
    for (;;) {
        tx_item_t it;
        if (xQueueReceive(q_tx, &it, pdMS_TO_TICKS(20)) == pdTRUE) comm_transmit(&it);

        rx_item_t rx;
        while (xQueueReceive(q_rx, &rx, 0) == pdTRUE) comm_handle_rx(&rx);
    }
}

/* ======================================================================== */
/*  tSystem: telemetria, bateria y salud                                    */
/* ======================================================================== */
static void task_system(void *)
{
    band_battery_init(&g_bat);

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t seconds = 0;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BAND_SYSTEM_PERIOD_MS));
        seconds++;

        if (seconds % BAND_TELEMETRY_PERIOD_S == 0u) {
            const uint8_t bat = band_battery_pct_from_mv(port_battery_mv());

            latest_t l;
            memset(&l, 0, sizeof l);
            if (xSemaphoreTake(g_latest_mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
                l = g_latest;
                xSemaphoreGive(g_latest_mtx);
            }
            const uint32_t now_ms = port_millis();
            const bool fresh = l.valid && ((now_ms / 1000u) - l.t_s) <= BAND_VITALS_MAX_AGE_S;

            tx_item_t it;
            memset(&it, 0, sizeof it);
            it.kind = TX_TELEMETRY;
            it.t_ms = now_ms;
            it.bpm  = fresh ? l.bpm  : (uint8_t)ALBA_BPM_INVALID;
            it.spo2 = fresh ? l.spo2 : (uint8_t)ALBA_SPO2_INVALID;
            it.bat  = bat;
            if (xQueueSend(q_tx, &it, 0) != pdTRUE) STAT_INC(tx_dropped);   /* la telemetria no es critica */

            if (band_battery_low_alert(&g_bat, bat)) {
                band_alert_t a;
                a.alerta_tipo  = ALBA_ALERTA_BATERIA_BAJA;
                a.valor_pico   = bat;
                a.confirmacion = ALBA_CONF_CONFIRMADA;
                port_log("[sistema] BATERIA BAJA: %u%%", (unsigned)bat);
                send_alert(&a, now_ms);
            }
        }

        if (seconds % BAND_STATS_PERIOD_S == 0u) {
            const band_stats_t s = g_stats;
            port_log("[sistema] tx ok=%lu fallo=%lu desc=%lu | rx ok=%lu ign=%lu | imu_err=%lu ppg_err=%lu",
                     (unsigned long)s.tx_sent, (unsigned long)s.tx_failed, (unsigned long)s.tx_dropped,
                     (unsigned long)s.rx_accepted, (unsigned long)s.rx_ignored,
                     (unsigned long)s.imu_errors, (unsigned long)s.ppg_errors);
#if INCLUDE_uxTaskGetStackHighWaterMark
            /* Cuanta pila NO se ha usado nunca (minimo historico). Si baja de ~500 bytes, agrandar. */
            port_log("[sistema] pila libre minima (bytes): fall=%lu vitals=%lu comm=%lu model=%lu system=%lu",
                     (unsigned long)(uxTaskGetStackHighWaterMark(h_fall)   * sizeof(StackType_t)),
                     (unsigned long)(uxTaskGetStackHighWaterMark(h_vitals) * sizeof(StackType_t)),
                     (unsigned long)(uxTaskGetStackHighWaterMark(h_comm)   * sizeof(StackType_t)),
                     (unsigned long)(uxTaskGetStackHighWaterMark(h_model)  * sizeof(StackType_t)),
                     (unsigned long)(uxTaskGetStackHighWaterMark(h_system) * sizeof(StackType_t)));
#endif
        }
    }
}

/* ======================================================================== */
/*  Arranque y radio                                                        */
/* ======================================================================== */
bool band_on_receive(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len == 0u || len > BAND_RX_MAX_LEN || q_rx == NULL) {
        STAT_INC(rx_dropped);
        return false;
    }
    rx_item_t it;
    memset(&it, 0, sizeof it);
    it.len = (uint8_t)len;
    memcpy(it.buf, buf, len);
    if (xQueueSend(q_rx, &it, 0) != pdTRUE) { STAT_INC(rx_dropped); return false; }
    return true;
}

bool band_tasks_start(void)
{
    band_clock_init(&g_clock);
    band_activity_init(&g_act);
    band_cancel_gate_init(&g_gate);

    g_clock_mtx  = xSemaphoreCreateMutex();
    g_act_mtx    = xSemaphoreCreateMutex();
    g_latest_mtx = xSemaphoreCreateMutex();
    q_tx  = xQueueCreate(BAND_TX_QUEUE_LEN,  sizeof(tx_item_t));
    q_rx  = xQueueCreate(BAND_RX_QUEUE_LEN,  sizeof(rx_item_t));
    q_cmd = xQueueCreate(BAND_CMD_QUEUE_LEN, sizeof(uint8_t));
    q_model_window = xQueueCreate(1, sizeof(fall_model_window_t));
    q_model_result = xQueueCreate(4, sizeof(fall_model_result_t));
    if (!g_clock_mtx || !g_act_mtx || !g_latest_mtx || !q_tx || !q_rx ||
        !q_cmd || !q_model_window || !q_model_result) return false;

    g_model_init_status = 0;
    if (xTaskCreate(task_fall_model, "tFallModel", BAND_STACK(BAND_STACK_MODEL),
                    NULL, BAND_PRIO_MODEL_INIT, &h_model) != pdPASS) {
        return false;
    }
    while (__atomic_load_n(&g_model_init_status, __ATOMIC_ACQUIRE) == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (__atomic_load_n(&g_model_init_status, __ATOMIC_ACQUIRE) < 0) {
        return false;
    }

    bool ok = true;
    ok &= xTaskCreate(task_comm,   "tComm",   BAND_STACK(BAND_STACK_COMM),   NULL, BAND_PRIO_COMM,   &h_comm) == pdPASS;
    ok &= xTaskCreate(task_fall,   "tFall",   BAND_STACK(BAND_STACK_FALL),   NULL, BAND_PRIO_FALL,   &h_fall) == pdPASS;
    ok &= xTaskCreate(task_vitals, "tVitals", BAND_STACK(BAND_STACK_VITALS), NULL, BAND_PRIO_VITALS, &h_vitals) == pdPASS;
    ok &= xTaskCreate(task_system, "tSystem", BAND_STACK(BAND_STACK_SYSTEM), NULL, BAND_PRIO_SYSTEM, &h_system) == pdPASS;
    return ok;
}
