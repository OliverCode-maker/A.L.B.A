#ifndef BUTTONS_MANAGER_H
#define BUTTONS_MANAGER_H

#include <stdint.h>

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_LEFT,
    BUTTON_EVENT_RIGHT,
    BUTTON_EVENT_OK
} button_event_t;

void initButtons(void);
button_event_t actualizarBotones(uint32_t now_ms);

#endif