/**
 * alba_thingsboard.h
 * Convierte los mensajes de la pulsera al formato de telemetria de ThingsBoard.
 * Lo usa el ROBOT antes de publicar por MQTT. (alba_codec.h tiene el JSON
 * "generico"; este es el que entiende ThingsBoard.)
 *
 *   Broker:  <tu servidor ThingsBoard>:8883 (TLS)       Usuario MQTT: el token del dispositivo
 *   Topico:  ALBA_TB_TOPIC  ("v1/devices/me/telemetry")
 *
 *   Telemetria:  {"ts":1793016003000,"values":{"bpm":55,"spo2":97,"bat":32}}
 *   Evento:      {"ts":1793016013270,"values":{"alert":"CAIDA_DETECTADA","confirm":"CONFIRMADA","value":4000}}
 *
 * - "ts" = hora real en milisegundos Unix. La pulsera no la conoce: la pone el
 *   robot al recibir el paquete (el retraso del aire es de milisegundos). Si
 *   epoch_ms es 0 se omite y ThingsBoard usa la hora del servidor.
 * - Las mediciones sin dato valido (bpm = 0, spo2 = 0xFF, bateria = 0xFF) NO se
 *   envian: la clave se omite, en vez de mandar un null o un cero enganoso.
 * - Eventos: "value" depende de la alerta: CAIDA_DETECTADA = pico en mili-g,
 *   VIGILIA_TRIGGER = bpm, BATERIA_BAJA = porcentaje.
 * - La alarma del cuidador se configura en ThingsBoard con una regla sobre
 *   alert == "CAIDA_DETECTADA" y confirm == "CONFIRMADA" (y se borra con "CANCELADA").
 *
 * Fuente del formato: https://thingsboard.io/docs/paas/reference/mqtt-api/telemetry/
 */
#ifndef ALBA_THINGSBOARD_H
#define ALBA_THINGSBOARD_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "alba_codec.h"

#define ALBA_TB_TOPIC "v1/devices/me/telemetry"

/* Escribe el JSON en `out` (con '\0'). Devuelve su longitud, o -1 si no cabe, si
 * hay un argumento nulo, o si no queda ninguna medicion valida que enviar
 * (en ese caso `out` queda vacio). */
static inline int alba_telemetry_to_tb(const alba_telemetry_t *p, uint64_t epoch_ms,
                                       char *out, size_t cap)
{
    if (p == NULL || out == NULL || cap == 0u) return -1;
    out[0] = '\0';

    char ts_s[40];
    ts_s[0] = '\0';
    if (epoch_ms != 0u) snprintf(ts_s, sizeof ts_s, "\"ts\":%llu,", (unsigned long long)epoch_ms);

    char vals[64];
    size_t n = 0;
    vals[0] = '\0';
    int ret;
    if (p->bpm != ALBA_BPM_INVALID) {
        ret = snprintf(vals + n, sizeof vals - n, "%s\"bpm\":%u", n ? "," : "", (unsigned)p->bpm);
        if (ret < 0 || (size_t)ret >= sizeof vals - n) return -1;
        n += (size_t)ret;
    }
    if (p->spo2 != ALBA_SPO2_INVALID) {
        ret = snprintf(vals + n, sizeof vals - n, "%s\"spo2\":%u", n ? "," : "", (unsigned)p->spo2);
        if (ret < 0 || (size_t)ret >= sizeof vals - n) return -1;
        n += (size_t)ret;
    }
    if (p->bateria_pct != ALBA_BAT_INVALID) {
        ret = snprintf(vals + n, sizeof vals - n, "%s\"bat\":%u", n ? "," : "", (unsigned)p->bateria_pct);
        if (ret < 0 || (size_t)ret >= sizeof vals - n) return -1;
        n += (size_t)ret;
    }
    if (n == 0u) return -1;                                /* nada valido que enviar */

    const int w = snprintf(out, cap, "{%s\"values\":{%s}}", ts_s, vals);
    if (w < 0 || (size_t)w >= cap) { out[0] = '\0'; return -1; }
    return w;
}

static inline int alba_event_to_tb(const alba_event_t *p, uint64_t epoch_ms,
                                   char *out, size_t cap)
{
    if (p == NULL || out == NULL || cap == 0u) return -1;
    out[0] = '\0';

    const char *alert   = alba_alerta_name(p->alerta_tipo);
    const char *confirm = alba_conf_name(p->confirmacion);
    if (alert == NULL || confirm == NULL) return -1;       /* mensaje no validado */

    char ts_s[40];
    ts_s[0] = '\0';
    if (epoch_ms != 0u) snprintf(ts_s, sizeof ts_s, "\"ts\":%llu,", (unsigned long long)epoch_ms);

    const int w = snprintf(out, cap,
        "{%s\"values\":{\"alert\":\"%s\",\"confirm\":\"%s\",\"value\":%u}}",
        ts_s, alert, confirm, (unsigned)p->valor_pico);
    if (w < 0 || (size_t)w >= cap) { out[0] = '\0'; return -1; }
    return w;
}

#endif /* ALBA_THINGSBOARD_H */
