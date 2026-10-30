/**
 * port_sim.c
 * Mundo simulado (BAND_SIM = 1): un IMU, un sensor de pulso, una bateria y un
 * ROBOT que siguen un guion. Permite ver en el monitor serie, sin ningun
 * sensor ni el robot, el recorrido completo:
 *
 *   senal -> detector -> alerta -> paquete ESP-NOW -> robot -> JSON de MQTT
 *
 * Guion (BAND_SIM_FAST = 1, segundos desde el arranque):
 *    3  el robot manda la HORA (07:00 locales)
 *   10  caida 1: nadie la cancela -> PENDIENTE y, 20 s despues, CONFIRMADA
 *   45  caida 2: el robot la CANCELA 4 s despues de enterarse (y manda el
 *               CANCELAR por duplicado: el segundo debe ser ignorado)
 *   70  empieza el despertar: el pulso sube de 55 a 88 bpm con movimiento
 *    -  la bateria baja de 3.70 V a 3.45 V y avisa al cruzar el 15 %
 * Con BAND_SIM_FAST = 0 los tiempos son los reales (el despertar tarda minutos).
 *
 * Todo es deterministico (mismo guion, mismo ruido) para poder compararlo.
 */
#include "band_config.h"

#if BAND_SIM

#include <math.h>
#include <string.h>

#include "alba_codec.h"
#include "band_os.h"
#include "band_port.h"
#include "band_tasks.h"

/* ---- Guion ----------------------------------------------------------------- */
#if BAND_SIM_FAST
#define T_HORA_S       3u
#define T_FALL1_S      10u
#define T_FALL2_S      45u
#define T_WAKE_S       70u
#define WAKE_RAMP_S    15u
#define BAT_START_MV   3700.0f
#define BAT_END_MV     3450.0f
#define BAT_DROP_S     100.0f
#else
#define T_HORA_S       3u
#define T_FALL1_S      20u
#define T_FALL2_S      120u
#define T_WAKE_S       900u
#define WAKE_RAMP_S    300u
#define BAT_START_MV   3900.0f
#define BAT_END_MV     3450.0f
#define BAT_DROP_S     3600.0f
#endif
#define CANCEL_DELAY_S 4u

/* 2026-10-26 12:00:00 UTC = 07:00 en Ecuador (UTC-5) */
#define SIM_EPOCH_S    1793016000u
#define SIM_UTC_OFFSET (-300)

/* ---- Ruido determinista ---------------------------------------------------- */
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float noise(uint32_t *s, float amp)
{
    return ((float)((lcg(s) >> 8) & 0xFFFFu) / 65535.0f * 2.0f - 1.0f) * amp;
}

/* ---- IMU simulado ------------------------------------------------------------
 * Avanza por NUMERO de muestra (10 ms cada una), no por el reloj real: asi el
 * guion no depende de la fluidez del planificador. */
bool port_imu_read(uint32_t now_ms, fall_sample_t *out)
{
    static uint32_t n = 0;
    static uint32_t rng = 12345;
    const uint32_t t = n++ * 10u;                        /* tiempo del guion, ms */

    float ax = 0.0f, ay = 0.0f, az = 1.0f, amp = 0.02f, gyro = 0.0f;

    const uint32_t f1 = T_FALL1_S * 1000u, f2 = T_FALL2_S * 1000u, w = T_WAKE_S * 1000u;

    if (t < f1) {                                        /* brazo colgando: gravedad en X */
        ax = 1.0f; az = 0.0f;
    } else if (t < f1 + 250u) {                          /* caida libre */
        ax = 0.3f; az = 0.0f; amp = 0.02f; gyro = 250.0f;
    } else if (t < f1 + 280u) {                          /* impacto: 3 muestras */
        const uint32_t k = (t - (f1 + 250u)) / 10u;
        if (k == 0u) { ax = 0.0f; az = 4.0f; }
        else if (k == 1u) { ax = 0.0f; az = 2.8f; }
        else { ax = 0.0f; ay = 0.5f; az = 1.8f; }
        amp = 0.0f;
    } else if (t < f2 - 2500u) {                         /* tendido, quieto: gravedad en Z */
        /* (0,0,1) */
    } else if (t < f2) {                                 /* se incorpora: brazo colgando otra vez */
        ax = 1.0f; az = 0.0f;
    } else if (t < f2 + 250u) {
        ax = 0.3f; az = 0.0f; gyro = 250.0f;
    } else if (t < f2 + 280u) {
        const uint32_t k = (t - (f2 + 250u)) / 10u;
        if (k == 0u) { ax = 0.0f; az = 4.0f; }
        else if (k == 1u) { ax = 0.0f; az = 2.8f; }
        else { ax = 0.0f; ay = 0.5f; az = 1.8f; }
        amp = 0.0f;
    } else if (t < w) {                                  /* tendido, quieto */
        /* (0,0,1) */
    } else {                                             /* despierta: se mueve en la cama */
        amp = 0.30f;
    }

    out->t_ms = now_ms;
    out->ax = ax + noise(&rng, amp);
    out->ay = ay + noise(&rng, amp);
    out->az = az + noise(&rng, amp);
    out->gx = gyro; out->gy = 0.0f; out->gz = 0.0f;
    return true;
}

/* ---- PPG simulado --------------------------------------------------------------- */
bool port_ppg_read(uint8_t *bpm, uint8_t *spo2, uint8_t *sqi)
{
    static uint32_t rng = 777;
    const float t = (float)port_millis() / 1000.0f;
    const float t0 = (float)T_WAKE_S;

    float hr = 55.0f;
    if (t >= t0 + (float)WAKE_RAMP_S)  hr = 88.0f;
    else if (t >= t0)                  hr = 55.0f + 33.0f * (t - t0) / (float)WAKE_RAMP_S;

    *bpm  = (uint8_t)(hr + noise(&rng, 2.0f) + 0.5f);
    *spo2 = 97;
    *sqi  = 90;
    return true;
}

/* ---- Bateria simulada ----------------------------------------------------------- */
uint16_t port_battery_mv(void)
{
    float t = (float)port_millis() / 1000.0f;
    if (t > BAT_DROP_S) t = BAT_DROP_S;
    return (uint16_t)(BAT_START_MV + (BAT_END_MV - BAT_START_MV) * t / BAT_DROP_S);
}

/* ---- Robot simulado ---------------------------------------------------------------
 * port_send() es lo que la pulsera cree que es "esp_now_send". Aqui el robot
 * valida el paquete como lo hara el ESP32-S3 real, lo convierte a JSON y lo
 * imprime (en el robot real iria al broker MQTT). */
typedef struct { uint16_t seq; } robot_job_t;
static QueueHandle_t q_robot;
static uint32_t g_falls_seen;

bool port_send(const uint8_t *buf, size_t len)
{
    const int type = alba_validate(buf, len);
    char json[256];

    if (type == ALBA_MSG_TELEMETRIA) {
        alba_telemetry_t p;
        memcpy(&p, buf, sizeof p);
        if (alba_telemetry_to_json(&p, NULL, json, sizeof json) > 0)
            port_log("[ROBOT]  RX telemetria -> MQTT %s", json);
    } else if (type == ALBA_MSG_EVENTO) {
        alba_event_t p;
        memcpy(&p, buf, sizeof p);
        if (alba_event_to_json(&p, NULL, json, sizeof json) > 0)
            port_log("[ROBOT]  RX EVENTO     -> MQTT %s", json);

        if (p.alerta_tipo == ALBA_ALERTA_CAIDA_DETECTADA && p.confirmacion == ALBA_CONF_PENDIENTE) {
            g_falls_seen++;
            if (g_falls_seen == 2u) {                    /* la 2.a caida el paciente dice "estoy bien" */
                robot_job_t job = { p.hdr.seq };
                xQueueSend(q_robot, &job, 0);
            }
        }
    } else {
        port_log("[ROBOT]  paquete invalido (%d)", type);
        return false;
    }
    return true;
}

static void send_to_band(const void *msg, size_t len)
{
    band_on_receive((const uint8_t *)msg, len);
}

static void task_sim_robot(void *)
{
    /* 1) La hora real, poco despues de arrancar. */
    vTaskDelay(pdMS_TO_TICKS(T_HORA_S * 1000u));
    alba_time_t h;
    alba_pack_time(&h, 1, SIM_EPOCH_S + T_HORA_S, SIM_UTC_OFFSET);
    port_log("[ROBOT]  TX HORA (07:00 local)");
    send_to_band(&h, sizeof h);

    /* 2) Cancelar la segunda caida. */
    for (;;) {
        robot_job_t job;
        if (xQueueReceive(q_robot, &job, portMAX_DELAY) != pdTRUE) continue;

        vTaskDelay(pdMS_TO_TICKS(CANCEL_DELAY_S * 1000u));
        alba_cancel_t c;
        alba_pack_cancel(&c, 2, job.seq);
        port_log("[ROBOT]  el paciente pulso 'estoy bien' -> TX CANCELAR (ref_seq %u)", (unsigned)job.seq);
        send_to_band(&c, sizeof c);

        vTaskDelay(pdMS_TO_TICKS(1000));                 /* duplicado: debe ignorarse */
        port_log("[ROBOT]  TX CANCELAR repetido (ref_seq %u)", (unsigned)job.seq);
        send_to_band(&c, sizeof c);
    }
}

void port_start(void)
{
    q_robot = xQueueCreate(2, sizeof(robot_job_t));
    xTaskCreate(task_sim_robot, "simRobot", BAND_STACK(4096u), NULL, 1, NULL);
}

#endif /* BAND_SIM */
