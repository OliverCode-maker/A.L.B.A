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

static QueueHandle_t q_tx, q_rx, q_cmd;
static TaskHandle_t  h_fall, h_vitals, h_comm, h_system;   /* solo para medir la pila */

enum { TX_ALERT = 1, TX_TELEMETRY = 2 };
enum { CMD_CANCEL_FALL = 1 };

typedef struct {
    uint8_t      kind;
    uint32_t     t_ms;
    band_alert_t alert;                         /* si kind == TX_ALERT      */
    uint8_t      bpm, spo2, bat;                /* si kind == TX_TELEMETRY  */
} tx_item_t;

typedef struct { uint8_t len; uint8_t buf[BAND_RX_MAX_LEN]; } rx_item_t;

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

/* ======================================================================== */
/*  tFall: IMU a 100 Hz -> detector de caidas                               */
/* ======================================================================== */
static void task_fall(void *)
{
    fall_detector_init(&g_fall, NULL);

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t   t_act_ms  = port_millis();

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

        const fall_event_t ev = fall_detector_process(&g_fall, &s);
        if (ev != FALL_EVT_NONE) {
            const fall_features_t *f = fall_detector_features(&g_fall);
            band_alert_t a;
            if (band_alert_from_fall(ev, f, &a)) {
                switch (ev) {
                case FALL_EVT_CANDIDATE:
                    STAT_INC(falls_candidate);
                    port_log("[caidas] POSIBLE CAIDA: pico %u mg, cambio de postura %.0f grados. "
                             "Esperando cancelacion...", (unsigned)a.valor_pico, (double)f->delta_angle);
                    break;
                case FALL_EVT_CONFIRMED:
                    STAT_INC(falls_confirmed);
                    port_log("[caidas] CAIDA CONFIRMADA (nadie cancelo)");
                    break;
                case FALL_EVT_CANCELLED:
                    STAT_INC(falls_cancelled);
                    port_log("[caidas] caida CANCELADA");
                    break;
                default: break;
                }
                send_alert(&a, s.t_ms);
            } else if (ev == FALL_EVT_REJECTED) {
                STAT_INC(falls_rejected);
                port_log("[caidas] golpe descartado (sin cambio de postura)");
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
            port_log("[sistema] pila libre minima (bytes): fall=%lu vitals=%lu comm=%lu system=%lu",
                     (unsigned long)(uxTaskGetStackHighWaterMark(h_fall)   * sizeof(StackType_t)),
                     (unsigned long)(uxTaskGetStackHighWaterMark(h_vitals) * sizeof(StackType_t)),
                     (unsigned long)(uxTaskGetStackHighWaterMark(h_comm)   * sizeof(StackType_t)),
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
    if (!g_clock_mtx || !g_act_mtx || !g_latest_mtx || !q_tx || !q_rx || !q_cmd) return false;

    bool ok = true;
    ok &= xTaskCreate(task_comm,   "tComm",   BAND_STACK(BAND_STACK_COMM),   NULL, BAND_PRIO_COMM,   &h_comm) == pdPASS;
    ok &= xTaskCreate(task_fall,   "tFall",   BAND_STACK(BAND_STACK_FALL),   NULL, BAND_PRIO_FALL,   &h_fall) == pdPASS;
    ok &= xTaskCreate(task_vitals, "tVitals", BAND_STACK(BAND_STACK_VITALS), NULL, BAND_PRIO_VITALS, &h_vitals) == pdPASS;
    ok &= xTaskCreate(task_system, "tSystem", BAND_STACK(BAND_STACK_SYSTEM), NULL, BAND_PRIO_SYSTEM, &h_system) == pdPASS;
    return ok;
}
