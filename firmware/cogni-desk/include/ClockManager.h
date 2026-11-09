#ifndef CLOCK_MANAGER_H
#define CLOCK_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "DeskApp.h"

bool initClockManager(desk_app_t *app);
void actualizarClockManager(desk_app_t *app, uint32_t now_ms);

#endif
