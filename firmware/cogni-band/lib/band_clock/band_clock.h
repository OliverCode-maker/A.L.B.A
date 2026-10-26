/**
 * band_clock.h
 * Reloj de la pulsera. La pulsera no tiene RTC: recibe la hora real del robot
 * (mensaje ALBA_MSG_HORA) y entre mensajes la sigue con su contador de
 * milisegundos. Entrega la hora local que necesita wake_detector.
 *
 *   al recibir HORA:   band_clock_sync(&clk, millis(), msg.epoch_s, msg.utc_offset_min);
 *   al medir:          hora = band_clock_hour(&clk, millis());   // WAKE_HOUR_UNKNOWN si no hay
 *
 * Si pasan mas de BAND_CLOCK_MAX_AGE_MS sin recibir hora nueva, se considera
 * desconocida (el cristal de la pulsera deriva y el contador de 32 bits se
 * desborda a los ~49 dias). El robot debe reenviar la hora cada pocas horas.
 */
#ifndef BAND_CLOCK_H
#define BAND_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BAND_CLOCK_MAX_AGE_MS (48ul * 3600ul * 1000ul)   /* 48 h */

typedef struct {
    bool     synced;
    uint32_t epoch_s;         /* hora UTC recibida                         */
    uint32_t t_ms_at_sync;    /* millis() en ese momento                   */
    int16_t  utc_offset_min;
} band_clock_t;

void band_clock_init(band_clock_t *c);

/** Guarda la hora recibida. Devuelve false (y no cambia nada) si epoch_s es
 *  anterior a ALBA_EPOCH_MIN_S o la zona horaria esta fuera de -12h..+14h. */
bool band_clock_sync(band_clock_t *c, uint32_t now_ms, uint32_t epoch_s, int16_t utc_offset_min);

/** Hora UTC actual en segundos Unix, o 0 si no hay hora vigente. */
uint32_t band_clock_epoch_s(const band_clock_t *c, uint32_t now_ms);

/** Hora local 0-23, o WAKE_HOUR_UNKNOWN (0xFF) si no hay hora vigente. */
uint8_t band_clock_hour(const band_clock_t *c, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* BAND_CLOCK_H */
