/**
 * cloud_outbox.h
 * Cola de mensajes del robot hacia ThingsBoard (C puro, se prueba en el PC).
 * Si no hay WiFi o MQTT, los mensajes esperan aqui; las alertas tienen prioridad:
 * cuando la cola se llena se descarta primero la telemetria mas antigua.
 *
 * Formato JSON: con hora real {"ts":ms,"values":{...}}; sin hora (el robot aun no sincronizo
 * NTP) JSON plano {...} y ThingsBoard usa su propia hora al recibirlo.
 *
 * No es thread-safe: CloudManager.cpp lo protege con un mutex.
 */
#ifndef CLOUD_OUTBOX_H
#define CLOUD_OUTBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alba_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLOUD_OUTBOX_CAP   16u
#define CLOUD_JSON_MAX     256u
#define CLOUD_ENV_INVALID_TEMP  INT16_MIN
#define CLOUD_ENV_INVALID_HUM   0xFFu

typedef struct {
    char json[CLOUD_JSON_MAX];
    bool is_event;
} cloud_msg_t;

typedef struct {
    cloud_msg_t msgs[CLOUD_OUTBOX_CAP];
    size_t count;
    uint32_t dropped;
} cloud_outbox_t;

void cloud_outbox_init(cloud_outbox_t *o);

/* epoch_ms = hora Unix en milisegundos, o 0 si el robot no la sabe. */
void cloud_outbox_push_telemetry(cloud_outbox_t *o, const alba_telemetry_t *tel, uint64_t epoch_ms);
void cloud_outbox_push_event(cloud_outbox_t *o, const alba_event_t *evt, uint64_t epoch_ms);
/* Temperatura en decimas de grado y humedad en %; los valores invalidos se omiten. */
void cloud_outbox_push_env(cloud_outbox_t *o, uint64_t epoch_ms, int16_t temp_c_x10, uint8_t hum_pct);
/* Resultado de una ronda de CogniGame. */
void cloud_outbox_push_game(cloud_outbox_t *o, uint64_t epoch_ms, uint8_t score, uint8_t total,
                            uint32_t avg_ms);

/* Leer el mas antiguo, publicarlo y SOLO si salio bien, quitarlo. */
const char *cloud_outbox_peek(const cloud_outbox_t *o);
void cloud_outbox_pop(cloud_outbox_t *o);
size_t cloud_outbox_count(const cloud_outbox_t *o);

#ifdef __cplusplus
}
#endif

#endif
