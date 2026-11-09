#ifndef DESK_CONFIG_H
#define DESK_CONFIG_H

#define DESK_BUTTON_LEFT_GPIO       4u
#define DESK_BUTTON_RIGHT_GPIO      5u
#define DESK_BUTTON_OK_GPIO         6u

#define DESK_DHT11_DATA_GPIO       15u
#define DESK_DHT11_SAMPLE_INTERVAL_MS 2000u

#define DESK_OLED_LEFT_SDA_GPIO     8u
#define DESK_OLED_LEFT_SCL_GPIO     9u
#define DESK_OLED_RIGHT_SDA_GPIO   17u
#define DESK_OLED_RIGHT_SCL_GPIO   18u
#define DESK_OLED_I2C_ADDRESS     0x3Cu

#define DESK_ESPNOW_CHANNEL         1u
#define DESK_WIFI_CHANNEL           DESK_ESPNOW_CHANNEL   /* solo para el AP de configuracion */
/* Canal al conectarse al router/hotspot: 0 = el que use esa red. La pulsera busca el canal
 * del robot sola (ver docs/integracion-robot.md, "Canal automatico"). */
#define DESK_WIFI_STA_CHANNEL       0u
#define DESK_BAND_STA_MAC { 0xACu, 0x27u, 0x6Eu, 0xCFu, 0x88u, 0xE4u }
#define DESK_EXPECTED_STA_MAC { 0x44u, 0x1Bu, 0xF6u, 0x8Cu, 0xACu, 0x04u }

#define DESK_BUTTON_DEBOUNCE_MS    30u

/* Zona horaria que el robot le informa a la pulsera en el mensaje HORA (Ecuador = UTC-5). */
#define DESK_UTC_OFFSET_MIN      (-300)

#define DESK_BUZZER_GPIO           14u
/* La pulsera manda telemetria cada 30 s: si pasan 90 s sin recibir nada, se muestra "sin senal". */
#define DESK_HEALTH_STALE_MS    90000u
/* El aviso de medicamento deja de sonar solo despues de este tiempo (la pantalla sigue hasta OK). */
#define DESK_MEDICATION_BUZZ_MAX_MS 60000u

#endif
