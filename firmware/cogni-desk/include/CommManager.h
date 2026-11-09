#ifndef COMM_MANAGER_H
#define COMM_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "DeskApp.h"
#include "HealthData.h"

bool initCommunication(void);
void procesarComunicacionesBackground(desk_app_t *app, uint32_t now_ms);
void ejecutarAccionRobot(robot_action_t action, uint16_t ref_seq);
/* Ultimos signos vitales de la pulsera; online=false si hace mas de DESK_HEALTH_STALE_MS que no llega nada. */
health_reading_t leerSalud(uint32_t now_ms);

#endif
