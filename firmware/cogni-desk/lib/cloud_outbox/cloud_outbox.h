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

/* Estado del ultimo aviso de caida CONFIRMADA (el que debe llegar al cuidador). */
typedef enum {
    CLOUD_ALERT_NONE = 0,   /* todavia no hubo un aviso de caida confirmada               */
    CLOUD_ALERT_PENDING,    /* en la cola, aun sin publicar                               */
    CLOUD_ALERT_SENT,       /* publicado en ThingsBoard                                   */
    CLOUD_ALERT_LOST        /* se descarto de la cola llena sin enviarse                  */
} cloud_alert_status_t;

/* Lo que la pantalla debe decir sobre ese aviso. */
typedef enum {
    CLOUD_SHOW_NOT_SENT = 0,   /* "Aviso NO enviado" (no hay aviso en la nube o se perdio)  */
    CLOUD_SHOW_SENDING,        /* "Enviando aviso..."                                       */
    CLOUD_SHOW_NO_NETWORK,     /* "Sin red: aviso en cola"                                  */
    CLOUD_SHOW_SENT,           /* "Aviso enviado"                                           */
    CLOUD_SHOW_CLOUD_OFF       /* "Nube no configurada"                                     */
} cloud_alert_show_t;

typedef struct {
    char json[CLOUD_JSON_MAX];
    bool is_event;
    uint32_t id;            /* numero unico, creciente */
} cloud_msg_t;

typedef struct {
    cloud_msg_t msgs[CLOUD_OUTBOX_CAP];
    size_t count;
    uint32_t dropped;
    uint32_t next_id;
    uint32_t alert_id;                 /* id del mensaje del ultimo aviso de caida confirmada */
    cloud_alert_status_t alert_status;
} cloud_outbox_t;

void cloud_outbox_init(cloud_outbox_t *o);

/* epoch_ms = hora Unix en milisegundos, o 0 si el robot no la sabe. */
void cloud_outbox_push_telemetry(cloud_outbox_t *o, const alba_telemetry_t *tel, uint64_t epoch_ms);
void cloud_outbox_push_event(cloud_outbox_t *o, const alba_event_t *evt, uint64_t epoch_ms);
/* Temperatura en decimas de grado y humedad en %; los valores invalidos se omiten. */
void cloud_outbox_push_env(cloud_outbox_t *o, uint64_t epoch_ms, int16_t temp_c_x10, uint8_t hum_pct);
/* Estado del ambiente respecto al rango de confort: "OK", "TEMP_ALTA", "HUM_BAJA+TEMP_ALTA"... */
void cloud_outbox_push_comfort(cloud_outbox_t *o, uint64_t epoch_ms, const char *text);
/* Resultado de una ronda de CogniGame. */
void cloud_outbox_push_game(cloud_outbox_t *o, uint64_t epoch_ms, uint8_t score, uint8_t total,
                            uint32_t avg_ms);

/* Leer el mas antiguo, publicarlo y SOLO si salio bien, quitarlo. */
const char *cloud_outbox_peek(const cloud_outbox_t *o);
void cloud_outbox_pop(cloud_outbox_t *o);
size_t cloud_outbox_count(const cloud_outbox_t *o);

cloud_alert_status_t cloud_outbox_alert_status(const cloud_outbox_t *o);
/* Combina el estado del aviso con si la nube esta configurada y conectada. */
cloud_alert_show_t cloud_alert_show(cloud_alert_status_t status, bool cloud_enabled, bool connected);

#ifdef __cplusplus
}
#endif

#endif
