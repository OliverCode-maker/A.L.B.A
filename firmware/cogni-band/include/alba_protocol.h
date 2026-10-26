/**
 * alba_protocol.h
 * Contrato de mensajes: pulsera (cogni-band, ESP32-C3) -> robot (cogni-desk, ESP32-S3).
 *
 * Este archivo lo deben incluir AMBOS firmwares. Si cambias un campo, sube
 * ALBA_PROTO_VERSION para que el receptor pueda descartar mensajes que no entiende.
 *
 * Los structs van empaquetados (sin relleno) y en little-endian, que es lo que
 * usan tanto el ESP32-C3 como el ESP32-S3, asi que se pueden enviar tal cual por
 * ESP-NOW (limite de 250 bytes por paquete; aqui usamos 11 y 12).
 *
 * -------------------------------------------------------------------------
 * BORRADOR del JSON que el robot reenviara al broker MQTT (se cierra en la
 * Tarea 3 junto con el enlace ESP-NOW). El robot agrega "ts" con la hora real,
 * porque la pulsera no tiene RTC y solo conoce milisegundos desde el arranque.
 *
 *   Telemetria:
 *   {"v":1,"dev":"cogni-band","type":"telemetry","seq":123,
 *    "t_ms":3600000,"bpm":62,"spo2":97,"bat":84,"ts":"2026-10-25T07:15:00-05:00"}
 *
 *   Evento critico:
 *   {"v":1,"dev":"cogni-band","type":"event","seq":124,
 *    "t_ms":3601234,"alert":"CAIDA_DETECTADA","peak":3850,"confirm":"PENDIENTE",
 *    "ts":"2026-10-25T07:15:01-05:00"}
 * -------------------------------------------------------------------------
 */
#ifndef ALBA_PROTOCOL_H
#define ALBA_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
#define ALBA_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define ALBA_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#define ALBA_PROTO_VERSION      1u
#define ALBA_ESPNOW_MAX_PAYLOAD 250u

/* Valores especiales: "no hay medicion valida" */
#define ALBA_BPM_INVALID   0u
#define ALBA_SPO2_INVALID  0xFFu

/* Tipo de mensaje (campo alba_hdr_t.type) */
typedef enum {
    ALBA_MSG_TELEMETRIA = 1,
    ALBA_MSG_EVENTO     = 2
} alba_msg_type_t;

/* Tipo de alerta (campo alba_event_t.alerta_tipo) */
typedef enum {
    ALBA_ALERTA_CAIDA_DETECTADA = 1,   /* valor_pico = pico de aceleracion en mili-g */
    ALBA_ALERTA_VIGILIA_TRIGGER = 2,   /* valor_pico = bpm al momento del disparo   */
    ALBA_ALERTA_BATERIA_BAJA    = 3    /* valor_pico = porcentaje de bateria         */
} alba_alerta_t;

/* Estado de confirmacion (campo alba_event_t.confirmacion).
 * Una caida genera hasta dos mensajes con el mismo tipo:
 *   1) PENDIENTE  -> la pulsera detecto un posible impacto y espera que el
 *                    paciente o el cuidador lo cancele (ventana de ~20 s).
 *   2) CONFIRMADA -> paso la ventana sin cancelacion: avisar al cuidador.
 *      o CANCELADA -> alguien pulso cancelar: era falsa alarma.
 * VIGILIA_TRIGGER y BATERIA_BAJA se envian siempre como CONFIRMADA. */
typedef enum {
    ALBA_CONF_PENDIENTE  = 0,
    ALBA_CONF_CONFIRMADA = 1,
    ALBA_CONF_CANCELADA  = 2
} alba_confirmacion_t;

/* Cabecera comun de todos los mensajes (4 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t  version;   /* ALBA_PROTO_VERSION                                   */
    uint8_t  type;      /* alba_msg_type_t                                      */
    uint16_t seq;       /* contador por emisor; el receptor detecta huecos      */
} alba_hdr_t;

/* Telemetria periodica (11 bytes) */
typedef struct __attribute__((packed)) {
    alba_hdr_t hdr;
    uint32_t   timestamp_ms;  /* ms desde el arranque (se desborda a los ~49 dias) */
    uint8_t    bpm;           /* ALBA_BPM_INVALID si no hay lectura confiable      */
    uint8_t    spo2;          /* 0-100, o ALBA_SPO2_INVALID                        */
    uint8_t    bateria_pct;   /* 0-100                                             */
} alba_telemetry_t;

/* Evento critico (12 bytes) */
typedef struct __attribute__((packed)) {
    alba_hdr_t hdr;
    uint32_t   timestamp_ms;
    uint8_t    alerta_tipo;   /* alba_alerta_t                                     */
    uint16_t   valor_pico;    /* unidad segun alerta_tipo (ver enum de arriba)     */
    uint8_t    confirmacion;  /* alba_confirmacion_t                               */
} alba_event_t;

ALBA_STATIC_ASSERT(sizeof(alba_hdr_t)       == 4,  "alba_hdr_t debe medir 4 bytes");
ALBA_STATIC_ASSERT(sizeof(alba_telemetry_t) == 11, "alba_telemetry_t debe medir 11 bytes");
ALBA_STATIC_ASSERT(sizeof(alba_event_t)     == 12, "alba_event_t debe medir 12 bytes");
ALBA_STATIC_ASSERT(sizeof(alba_telemetry_t) <= ALBA_ESPNOW_MAX_PAYLOAD, "excede ESP-NOW");
ALBA_STATIC_ASSERT(sizeof(alba_event_t)     <= ALBA_ESPNOW_MAX_PAYLOAD, "excede ESP-NOW");

#endif /* ALBA_PROTOCOL_H */
