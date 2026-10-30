/**
 * band_activity.h
 * Acumula el % de actividad que mide la tarea de caidas (una cifra por
 * segundo) para que la tarea de signos vitales lo lea cuando le toque medir
 * (cada 15-300 s). Sin esto se perderia la actigrafia entre mediciones.
 *
 * C puro y sin bloqueo propio: quien lo comparte entre tareas lo protege con
 * una seccion critica o un mutex (ver band_tasks.c).
 */
#ifndef BAND_ACTIVITY_H
#define BAND_ACTIVITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sum_pct;   /* suma de los porcentajes recibidos */
    uint32_t n;         /* cuantos se recibieron             */
} band_activity_t;

void band_activity_init(band_activity_t *a);

/** Agrega una muestra (0-100; valores mayores se limitan a 100). */
void band_activity_add(band_activity_t *a, uint8_t pct);

/** Promedio (redondeado) desde la ultima llamada, y reinicia. 0 si no hubo muestras. */
uint8_t band_activity_take(band_activity_t *a);

#ifdef __cplusplus
}
#endif

#endif /* BAND_ACTIVITY_H */
