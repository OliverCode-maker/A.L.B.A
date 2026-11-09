#ifndef HEALTH_DATA_H
#define HEALTH_DATA_H

#include <stdbool.h>
#include <stdint.h>

/* Ultimos signos vitales recibidos de la pulsera (telemetria por ESP-NOW). */
typedef struct {
    bool online;        /* llego telemetria hace menos de DESK_HEALTH_STALE_MS            */
    bool bpm_valid;     /* false = la pulsera no tiene lectura confiable (por ej. sin muneca) */
    uint8_t bpm;
    bool spo2_valid;
    uint8_t spo2;
    bool bat_valid;     /* false = la pulsera no mide su bateria                           */
    uint8_t bat_pct;
} health_reading_t;

#endif
