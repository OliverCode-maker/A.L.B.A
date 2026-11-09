// Copia este archivo como include/cloud_secrets.h y completa los datos.
// cloud_secrets.h NO se sube a GitHub (esta en .gitignore): contiene el token del dispositivo.
#pragma once

// Servidor ThingsBoard propio (AWS), sin TLS. El token viaja sin cifrar: solo para pruebas/demo.
#define CLOUD_TB_HOST   "100.27.157.49"
#define CLOUD_TB_PORT   1883
#define CLOUD_TB_TLS    0

// ThingsBoard Cloud (mqtt.thingsboard.cloud), con TLS:
// #define CLOUD_TB_HOST   "mqtt.thingsboard.cloud"
// #define CLOUD_TB_PORT   8883
// #define CLOUD_TB_TLS    1

// Token del dispositivo "Pulsera ALBA": ThingsBoard -> Devices -> Pulsera ALBA -> Copy access token.
#define CLOUD_TB_TOKEN  "PEGA_AQUI_EL_TOKEN"
