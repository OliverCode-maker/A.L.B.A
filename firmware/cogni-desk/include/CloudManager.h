#ifndef CLOUD_MANAGER_H
#define CLOUD_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "SensorManager.h"
#include "alba_protocol.h"
#include "cogni_game.h"

/* Publica a ThingsBoard por MQTT los datos de la pulsera y del propio robot.
 * Corre en una tarea aparte (nucleo 0): conectar o reintentar con la red caida no congela botones,
 * pantallas ni buzzer. Sin include/cloud_secrets.h (o sin token) la nube queda desactivada y el
 * resto del robot funciona igual. */

bool initCloud(void);

/* Los llama CommManager cuando llega un mensaje NUEVO ya validado de la pulsera. */
void cloudOnTelemetry(const alba_telemetry_t *telemetry);
void cloudOnEvent(const alba_event_t *event);

/* Llamar en cada vuelta del loop: encola temperatura/humedad (cada 30 s) y el resultado del juego. */
void actualizarCloud(uint32_t now_ms, const environment_reading_t *ambiente, const game_result_t *juego);

bool cloudConectado(void);
uint32_t cloudPublicados(void);

#endif
