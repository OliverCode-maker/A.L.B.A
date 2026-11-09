#ifndef COMM_MANAGER_H
#define COMM_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "DeskApp.h"

bool initCommunication(void);
void procesarComunicacionesBackground(desk_app_t *app, uint32_t now_ms);
void ejecutarAccionRobot(robot_action_t action, uint16_t ref_seq);

#endif
