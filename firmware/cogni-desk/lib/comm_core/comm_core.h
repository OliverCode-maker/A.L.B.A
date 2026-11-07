/**
 * comm_core.h
 * Cerebro de las comunicaciones del ROBOT (cogni-desk). Une:
 *   robot_link  (valida paquetes de la pulsera, duplicados, cuando mandar la HORA)
 *   robot_fall  (el asistente "¿Estas bien?" y la cancelacion de caidas)
 *   alba_thingsboard.h (JSON para ThingsBoard)
 * y agrega una COLA DE SALIDA para la nube: si no hay WiFi/MQTT, los mensajes
 * esperan (las alertas tienen prioridad sobre la telemetria).
 *
 * C puro, sin WiFi, ESP-NOW ni Arduino: se prueba en el PC (pio test -e native).
 * CommManager.cpp es la capa delgada que lo conecta con el hardware.
 *
 * No es thread-safe: llamarlo siempre desde la misma tarea (el loop()). El
 * callback de ESP-NOW NO debe llamarlo: debe copiar el paquete a una cola y
 * dejar que el loop() lo entregue (ver CommManager.cpp).
 */
#ifndef COMM_CORE_H
#define COMM_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "robot_fall.h"
#include "robot_link.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMM_OUTBOX_CAP        16u    /* mensajes esperando para la nube        */
#define COMM_JSON_MAX          200u   /* tamano maximo de un mensaje JSON       */
#define COMM_BAND_OFFLINE_MS   (120u * 1000u)  /* sin paquetes en este tiempo = pulsera desconectada */
#define COMM_ENV_INVALID_TEMP  INT16_MIN
#define COMM_ENV_INVALID_HUM   0xFFu

/** Manda bytes a la pulsera (ESP-NOW). true = aceptado por la radio. */
typedef bool (*comm_send_fn)(const uint8_t *buf, size_t len, void *ctx);

typedef struct {
    char json[COMM_JSON_MAX];
    bool is_event;                 /* las alertas no se descartan antes que la telemetria */
} comm_msg_t;

typedef struct {
    robot_link_t link;
    robot_fall_t fall;
    comm_send_fn send;
    void        *send_ctx;
    uint16_t     tx_seq;           /* numero de secuencia de lo que el ROBOT envia */

    comm_msg_t   outbox[COMM_OUTBOX_CAP];
    size_t       out_count;
    uint32_t     out_dropped;      /* mensajes descartados por cola llena */

    /* lo que la interfaz (pantallas) necesita saber */
    bool     prompt_active;        /* mostrar "¿Estas bien?" y atender el boton */
    bool     help_called;          /* la caida se confirmo: avisar al paciente (se consume) */
    bool     have_vitals;
    uint8_t  bpm, spo2, bat;       /* ultimo valor recibido (invalidos = ver alba_protocol.h) */
    uint32_t vitals_ms;            /* cuando llego el ultimo dato de la pulsera */
    uint32_t last_rx_ms;
    bool     seen_band;
    uint32_t send_fail;            /* envios a la pulsera que la radio rechazo */
} comm_core_t;

void comm_core_init(comm_core_t *c, comm_send_fn send, void *send_ctx);

/** Un paquete llego de la pulsera. epoch_ms = hora real en ms Unix (0 si el robot no la sabe). */
void comm_core_on_packet(comm_core_t *c, const uint8_t *buf, size_t len,
                         uint32_t now_ms, uint64_t epoch_ms);

/** El paciente pulso "estoy bien". */
void comm_core_button_ok(comm_core_t *c, uint32_t now_ms);

/** Llamar cada ~100 ms. epoch_s = hora real en segundos Unix (0 si no se sabe); con
 *  hora invalida NO se manda HORA a la pulsera. utc_offset_min: Ecuador = -300. */
void comm_core_tick(comm_core_t *c, uint32_t now_ms, uint32_t epoch_s, int16_t utc_offset_min);

/** Datos del propio robot (sensor del cuarto) para la nube. Valores invalidos se omiten. */
void comm_core_push_env(comm_core_t *c, uint64_t epoch_ms, int16_t temp_c_x10, uint8_t hum_pct);

/** Mensaje propio ya formateado para la nube (p. ej. "se tomo la pastilla"). Debe ser JSON de ThingsBoard. */
bool comm_core_push_raw(comm_core_t *c, const char *json, bool is_event);

/* --- cola hacia la nube: leer el mas antiguo, publicar y SOLO si se envio, quitarlo --- */
const char *comm_core_outbox_peek(const comm_core_t *c);
void        comm_core_outbox_pop(comm_core_t *c);
size_t      comm_core_outbox_count(const comm_core_t *c);

/* --- consultas para la interfaz --- */
bool comm_core_prompt_active(const comm_core_t *c);
/** true UNA vez cuando la caida se confirmo (luego se borra). */
bool comm_core_take_help_called(comm_core_t *c);
bool comm_core_band_online(const comm_core_t *c, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* COMM_CORE_H */
