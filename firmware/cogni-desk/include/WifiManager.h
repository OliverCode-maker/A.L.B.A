#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

bool initWifiManager(void);
void actualizarWifiManager(uint32_t now_ms);
bool wifiManagerConnected(void);
bool wifiManagerProvisioning(void);

#endif
