#ifndef CLOUD_MANAGER_H
#define CLOUD_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "SensorManager.h"
#include "alba_protocol.h"
#include "cloud_outbox.h"
#include "cogni_game.h"

/* Publica a ThingsBoard por MQTT los datos de la pulsera y del propio robot.
 * Corre en una tarea aparte (nucleo 0): conectar o reintentar con la red caida no congela botones,
 * pantallas ni buzzer. Sin include/cloud_secrets.h (o sin token) la nube queda desactivada y el
 * resto del robot funciona igual. */

bool initCloud(void);

/* Los llama CommManager cuando llega un mensaje NUEVO ya validado de la pulsera. */
void cloudOnTelemetry(const alba_telemetry_t *telemetry);
void cloudOnEvent(const alba_event_t *event);

/* El ambiente salio del rango de confort (o volvio): texto de comfort_describe(), p. ej. "TEMP_ALTA". */
void cloudOnComfort(const char *text);

/* Llamar en cada vuelta del loop: encola temperatura/humedad (cada 30 s) y el resultado del juego. */
void actualizarCloud(uint32_t now_ms, const environment_reading_t *ambiente, const game_result_t *juego);

/* Preguntas del cuidador: el atributo compartido `questions` de ThingsBoard llega por MQTT.
 * cloudTakeQuestions() entrega el texto nuevo una sola vez (true) para que QuestionManager lo aplique;
 * cloudOnQuestions() publica cuantas preguntas quedaron activas (0 = banco incluido) y cuantas
 * entradas se descartaron (q_count / q_rejected). */
bool cloudTakeQuestions(char *out, size_t cap);
void cloudOnQuestions(uint8_t active, uint8_t rejected);

bool cloudConectado(void);

/* Que debe decir la pantalla de caida confirmada sobre el aviso al cuidador. */
cloud_alert_show_t cloudEstadoAviso(void);
uint32_t cloudPublicados(void);

#endif
