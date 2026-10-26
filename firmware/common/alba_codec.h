/**
 * alba_codec.h
 * Funciones compartidas para armar, validar y convertir los mensajes de
 * alba_protocol.h. Lo incluyen los DOS firmwares:
 *
 *   PULSERA (cogni-band):  alba_pack_telemetry()  alba_pack_event()
 *                          (recibe) alba_validate() -> HORA / CANCELAR
 *   ROBOT   (cogni-desk):  alba_validate()  alba_telemetry_to_json()  alba_event_to_json()
 *                          (envia)  alba_pack_time()  alba_pack_cancel()
 *
 * Es "header-only" (todo static inline): no hay archivos .c que agregar al
 * build de ninguno de los dos proyectos; basta con el -I../common que ya
 * tienen. Lo que un firmware no usa, el compilador lo descarta.
 *
 * Flujo completo de un mensaje:
 *
 *   PULSERA                                   ROBOT
 *   alba_pack_event(&pkt, ...)                esp_now_recv_cb(mac, buf, len):
 *   esp_now_send(robot_mac, &pkt, sizeof pkt)   int t = alba_validate(buf, len);
 *        ~~~~~~~~~~~ aire ~~~~~~~~~~~>          if (t == ALBA_MSG_EVENTO) {
 *                                                 alba_event_t e; memcpy(&e, buf, sizeof e);
 *                                                 alba_event_to_json(&e, hora_iso, json, sizeof json);
 *                                                 mqtt_publish(topic, json);
 *                                               }
 *
 * IMPORTANTE para el receptor: nunca castear el buffer recibido directamente
 * a un struct sin validar. Siempre alba_validate() primero y memcpy despues
 * (el buffer puede estar desalineado y los structs estan empaquetados).
 */
#ifndef ALBA_CODEC_H
#define ALBA_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "alba_protocol.h"

/* Nombre del dispositivo que aparece en el JSON ("dev"). */
#define ALBA_DEVICE_NAME "cogni-band"

/* Errores de alba_validate() (los valores positivos son el tipo de mensaje). */
typedef enum {
    ALBA_ERR_LEN     = -1,   /* longitud incorrecta o buffer nulo         */
    ALBA_ERR_VERSION = -2,   /* version de protocolo que no entendemos    */
    ALBA_ERR_TYPE    = -3,   /* tipo de mensaje desconocido               */
    ALBA_ERR_FIELD   = -4    /* algun campo fuera de rango                */
} alba_err_t;

/* ======================================================================== */
/*  Lado PULSERA: armar paquetes                                            */
/* ======================================================================== */

/* El contador `seq` lo lleva quien envia (uno por tipo de emisor es suficiente);
 * incrementarlo en cada paquete permite al robot detectar paquetes perdidos. */

static inline void alba_pack_telemetry(alba_telemetry_t *p, uint16_t seq, uint32_t t_ms,
                                       uint8_t bpm, uint8_t spo2, uint8_t bateria_pct)
{
    p->hdr.version  = ALBA_PROTO_VERSION;
    p->hdr.type     = ALBA_MSG_TELEMETRIA;
    p->hdr.seq      = seq;
    p->timestamp_ms = t_ms;
    p->bpm          = bpm;
    p->spo2         = spo2;
    /* ALBA_BAT_INVALID (0xFF) pasa tal cual; cualquier otro valor se limita a 100. */
    p->bateria_pct  = (bateria_pct == ALBA_BAT_INVALID || bateria_pct <= 100u) ? bateria_pct : 100u;
}

static inline void alba_pack_event(alba_event_t *p, uint16_t seq, uint32_t t_ms,
                                   uint8_t alerta_tipo, uint16_t valor_pico, uint8_t confirmacion)
{
    p->hdr.version  = ALBA_PROTO_VERSION;
    p->hdr.type     = ALBA_MSG_EVENTO;
    p->hdr.seq      = seq;
    p->timestamp_ms = t_ms;
    p->alerta_tipo  = alerta_tipo;
    p->valor_pico   = valor_pico;
    p->confirmacion = confirmacion;
}

/* ======================================================================== */
/*  Lado ROBOT: armar mensajes para la pulsera                              */
/* ======================================================================== */

static inline void alba_pack_time(alba_time_t *p, uint16_t seq, uint32_t epoch_s, int16_t utc_offset_min)
{
    p->hdr.version    = ALBA_PROTO_VERSION;
    p->hdr.type       = ALBA_MSG_HORA;
    p->hdr.seq        = seq;
    p->epoch_s        = epoch_s;
    p->utc_offset_min = utc_offset_min;
}

static inline void alba_pack_cancel(alba_cancel_t *p, uint16_t seq, uint16_t ref_seq)
{
    p->hdr.version = ALBA_PROTO_VERSION;
    p->hdr.type    = ALBA_MSG_CANCELAR;
    p->hdr.seq     = seq;
    p->ref_seq     = ref_seq;
}

/* ======================================================================== */
/*  Validar lo que llega (robot: telemetria/evento; pulsera: hora/cancelar) */
/* ======================================================================== */

/* Revisa un buffer recibido por ESP-NOW.
 * Devuelve el tipo de mensaje (ALBA_MSG_TELEMETRIA o ALBA_MSG_EVENTO) si es
 * valido, o un codigo negativo de alba_err_t. */
static inline int alba_validate(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len < sizeof(alba_hdr_t)) return ALBA_ERR_LEN;

    const uint8_t version = buf[0];
    const uint8_t type    = buf[1];
    if (version != ALBA_PROTO_VERSION) return ALBA_ERR_VERSION;

    if (type == ALBA_MSG_TELEMETRIA) {
        if (len != sizeof(alba_telemetry_t)) return ALBA_ERR_LEN;
        alba_telemetry_t t;
        for (size_t i = 0; i < sizeof t; i++) ((uint8_t *)&t)[i] = buf[i];
        if (t.spo2 != ALBA_SPO2_INVALID && t.spo2 > 100u) return ALBA_ERR_FIELD;
        if (t.bateria_pct != ALBA_BAT_INVALID && t.bateria_pct > 100u) return ALBA_ERR_FIELD;
        return ALBA_MSG_TELEMETRIA;
    }
    if (type == ALBA_MSG_HORA) {
        if (len != sizeof(alba_time_t)) return ALBA_ERR_LEN;
        alba_time_t h;
        for (size_t i = 0; i < sizeof h; i++) ((uint8_t *)&h)[i] = buf[i];
        if (h.epoch_s < ALBA_EPOCH_MIN_S)                         return ALBA_ERR_FIELD;
        if (h.utc_offset_min < -720 || h.utc_offset_min > 840)    return ALBA_ERR_FIELD;
        return ALBA_MSG_HORA;
    }
    if (type == ALBA_MSG_CANCELAR) {
        if (len != sizeof(alba_cancel_t)) return ALBA_ERR_LEN;
        return ALBA_MSG_CANCELAR;
    }
    if (type == ALBA_MSG_EVENTO) {
        if (len != sizeof(alba_event_t)) return ALBA_ERR_LEN;
        alba_event_t e;
        for (size_t i = 0; i < sizeof e; i++) ((uint8_t *)&e)[i] = buf[i];
        if (e.alerta_tipo < ALBA_ALERTA_CAIDA_DETECTADA ||
            e.alerta_tipo > ALBA_ALERTA_BATERIA_BAJA)     return ALBA_ERR_FIELD;
        if (e.confirmacion > ALBA_CONF_CANCELADA)         return ALBA_ERR_FIELD;
        return ALBA_MSG_EVENTO;
    }
    return ALBA_ERR_TYPE;
}

/* ======================================================================== */
/*  Lado ROBOT: convertir a JSON para el broker MQTT                        */
/* ======================================================================== */

static inline const char *alba_alerta_name(uint8_t a)
{
    switch (a) {
    case ALBA_ALERTA_CAIDA_DETECTADA: return "CAIDA_DETECTADA";
    case ALBA_ALERTA_VIGILIA_TRIGGER: return "VIGILIA_TRIGGER";
    case ALBA_ALERTA_BATERIA_BAJA:    return "BATERIA_BAJA";
    default:                          return NULL;
    }
}

static inline const char *alba_conf_name(uint8_t c)
{
    switch (c) {
    case ALBA_CONF_PENDIENTE:  return "PENDIENTE";
    case ALBA_CONF_CONFIRMADA: return "CONFIRMADA";
    case ALBA_CONF_CANCELADA:  return "CANCELADA";
    default:                   return NULL;
    }
}

/* El texto de la hora (por ejemplo "2026-10-25T07:15:00-05:00") lo pone el
 * robot, que es el unico que conoce la hora real. Como se inserta tal cual
 * dentro del JSON, solo se acepta texto simple; si trae comillas, barras o
 * caracteres de control se omite el campo en vez de producir un JSON roto. */
static inline int alba_ts_is_safe(const char *s)
{
    if (s == NULL || s[0] == '\0') return 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (i >= 40u)                       return 0;
        if (c < 0x20u || c > 0x7Eu)         return 0;
        if (c == (unsigned char)'"' || c == (unsigned char)'\\') return 0;
    }
    return 1;
}

/* Escribe el JSON en `out` (con '\0' final). Devuelve la longitud sin el '\0',
 * o -1 si no cabe o hay un argumento invalido (en ese caso out queda vacio).
 * `iso_ts` puede ser NULL: entonces no se agrega el campo "ts".
 * Los valores "sin medicion valida" (bpm = 0, spo2 = 0xFF, bat = 0xFF) salen como null. */
static inline int alba_telemetry_to_json(const alba_telemetry_t *p, const char *iso_ts,
                                         char *out, size_t cap)
{
    if (p == NULL || out == NULL || cap == 0u) return -1;
    out[0] = '\0';

    const unsigned version = p->hdr.version;
    const unsigned seq     = p->hdr.seq;
    const unsigned long t  = (unsigned long)p->timestamp_ms;
    const unsigned bpm     = p->bpm;
    const unsigned spo2    = p->spo2;
    const unsigned bat     = p->bateria_pct;

    char bpm_s[8], spo2_s[8], bat_s[8], ts_s[64];
    if (bat == ALBA_BAT_INVALID)   snprintf(bat_s,  sizeof bat_s,  "null");
    else                           snprintf(bat_s,  sizeof bat_s,  "%u", bat);
    if (bpm == ALBA_BPM_INVALID)   snprintf(bpm_s,  sizeof bpm_s,  "null");
    else                           snprintf(bpm_s,  sizeof bpm_s,  "%u", bpm);
    if (spo2 == ALBA_SPO2_INVALID) snprintf(spo2_s, sizeof spo2_s, "null");
    else                           snprintf(spo2_s, sizeof spo2_s, "%u", spo2);
    ts_s[0] = '\0';
    if (alba_ts_is_safe(iso_ts))   snprintf(ts_s, sizeof ts_s, ",\"ts\":\"%s\"", iso_ts);

    const int n = snprintf(out, cap,
        "{\"v\":%u,\"dev\":\"" ALBA_DEVICE_NAME "\",\"type\":\"telemetry\","
        "\"seq\":%u,\"t_ms\":%lu,\"bpm\":%s,\"spo2\":%s,\"bat\":%s%s}",
        version, seq, t, bpm_s, spo2_s, bat_s, ts_s);
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return -1; }
    return n;
}

static inline int alba_event_to_json(const alba_event_t *p, const char *iso_ts,
                                     char *out, size_t cap)
{
    if (p == NULL || out == NULL || cap == 0u) return -1;
    out[0] = '\0';

    const char *alert   = alba_alerta_name(p->alerta_tipo);
    const char *confirm = alba_conf_name(p->confirmacion);
    if (alert == NULL || confirm == NULL) return -1;       /* mensaje no validado */

    const unsigned version = p->hdr.version;
    const unsigned seq     = p->hdr.seq;
    const unsigned long t  = (unsigned long)p->timestamp_ms;
    const unsigned peak    = p->valor_pico;

    char ts_s[64];
    ts_s[0] = '\0';
    if (alba_ts_is_safe(iso_ts)) snprintf(ts_s, sizeof ts_s, ",\"ts\":\"%s\"", iso_ts);

    const int n = snprintf(out, cap,
        "{\"v\":%u,\"dev\":\"" ALBA_DEVICE_NAME "\",\"type\":\"event\","
        "\"seq\":%u,\"t_ms\":%lu,\"alert\":\"%s\",\"peak\":%u,\"confirm\":\"%s\"%s}",
        version, seq, t, alert, peak, confirm, ts_s);
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return -1; }
    return n;
}

#endif /* ALBA_CODEC_H */
