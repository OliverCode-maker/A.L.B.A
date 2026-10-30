/**
 * band_tasks.h
 * Arquitectura FreeRTOS de la pulsera. Ver el diagrama y las reglas de
 * propiedad de datos en band_tasks.c y en docs/arquitectura-freertos.md.
 */
#ifndef BAND_TASKS_H
#define BAND_TASKS_H

#include <stddef.h>
#include <stdint.h>

/** Crea colas, mutex y las cuatro tareas. Llamar una vez desde app_main(). */
bool band_tasks_start(void);

/** La radio (callback de ESP-NOW) o el robot simulado entregan aqui lo que
 *  recibieron. No bloquea: copia a una cola y vuelve (puede llamarse desde el
 *  callback de la radio). false = descartado (invalido, cola llena). */
bool band_on_receive(const uint8_t *buf, size_t len);

/** Contadores para depurar. */
typedef struct {
    uint32_t tx_sent, tx_failed, tx_dropped;
    uint32_t rx_accepted, rx_ignored, rx_dropped;
    uint32_t imu_errors, ppg_errors;
    uint32_t falls_candidate, falls_confirmed, falls_cancelled, falls_rejected;
    uint32_t wake_triggers;
} band_stats_t;

band_stats_t band_stats_snapshot(void);

#endif /* BAND_TASKS_H */
