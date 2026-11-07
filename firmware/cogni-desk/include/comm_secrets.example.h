// Copia este archivo como  include/comm_secrets.h  y rellenalo.
// comm_secrets.h esta en .gitignore: NUNCA lo subas al repositorio (el repo es publico).
#ifndef COMM_SECRETS_H
#define COMM_SECRETS_H

#define COMM_WIFI_SSID   "nombre-de-tu-wifi"
#define COMM_WIFI_PASS   "clave-de-tu-wifi"

// Servidor de ThingsBoard. En ThingsBoard Cloud: mqtt.thingsboard.cloud
#define COMM_TB_HOST     "mqtt.thingsboard.cloud"
#define COMM_TB_PORT     8883
// Token de acceso del dispositivo "pulsera" (Device -> Copy access token)
#define COMM_TB_TOKEN    "pega-aqui-el-token"

// Hora local: Ecuador = UTC-5 = -300 minutos
#define COMM_UTC_OFFSET_MIN  (-300)

// (Opcional, recomendado) certificado de la CA del servidor, en formato PEM,
// para verificar que el servidor es el verdadero. Sin esto la conexion va cifrada
// pero NO se verifica la identidad del servidor (aceptable solo para pruebas).
// #define COMM_TB_CA_CERT "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"

#endif
