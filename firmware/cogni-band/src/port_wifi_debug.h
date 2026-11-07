#ifndef PORT_WIFI_DEBUG_H
#define PORT_WIFI_DEBUG_H

#include <stdbool.h>

void port_wifi_debug_start(void);
bool port_wifi_debug_send(const char *message);

#endif
