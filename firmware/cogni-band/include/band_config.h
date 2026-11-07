/**
 * band_config.h
 * Parametros del firmware de la pulsera: periodos, colas, prioridades y el
 * modo de simulacion. Todo lo "ajustable" vive aqui, no en las tareas.
 */
#ifndef BAND_CONFIG_H
#define BAND_CONFIG_H

/* ---- Fuentes de datos ------------------------------------------------------
 * BAND_SIM = 1: el IMU, el PPG, la bateria y el robot son SIMULADOS (guion en
 *               port_sim.c). Sirve para ver todo el flujo en el monitor
 *               serie sin sensores ni robot.
 * BAND_SIM = 0: usa los sensores reales (MPU6050 y MAX30102); la radio y la
 *               medicion de bateria quedan deshabilitadas hasta integrarlas.
 * BAND_SIM_FAST = 1: guion comprimido (~100 s) y detector de vigilia con
 *               ventanas cortas, solo para la demo. 0 = tiempos reales
 *               (el despertar tarda ~10 min en aparecer). */
#ifndef BAND_SIM1
#define BAND_SIM1        0
#endif
#ifndef BAND_SIM_FAST1
#define BAND_SIM_FAST1   0
#endif

/* Salida temporal de resultados por el AP del ESP32-S3 (UDP broadcast). */
#define BAND_WIFI_DEBUG_SSID      "ALBA-Fall-Debug"
#define BAND_WIFI_DEBUG_UDP_PORT  3333u

/* ---- Periodos ---------------------------------------------------------------*/
#define BAND_IMU_PERIOD_MS        10u      /* 100 Hz                                  */
#define BAND_ACTIVITY_PERIOD_MS   1000u    /* cada cuanto se pasa la actigrafia a vitales */
#define BAND_SYSTEM_PERIOD_MS     1000u

/* Reglas experimentales del clasificador Three Classes sobre la IMU en g y deg/s. */
#define BAND_RULE_FREEFALL_G              0.5f
#define BAND_RULE_IMPACT_G                2.5f
#define BAND_RULE_DIRECT_IMPACT_G         3.2f
#define BAND_RULE_FREEFALL_MIN_MS         60u
#define BAND_RULE_FREEFALL_MAX_MS         400u
#define BAND_RULE_IMPACT_WINDOW_MS        600u
#define BAND_RULE_CONFIRM_GYRO_DPS        200.0f
#define BAND_RULE_CONFIRM_ANGLE_DEG       40.0f
#define BAND_RULE_CONFIRM_POST_STD_G      0.12f
#define BAND_RULE_CONFIRM_WINDOW_MS       500u

#if BAND_SIM_FAST
#define BAND_TELEMETRY_PERIOD_S   5u
#define BAND_STATS_PERIOD_S       30u
#else
#define BAND_TELEMETRY_PERIOD_S   30u
#define BAND_STATS_PERIOD_S       300u
#endif

/* Una lectura de pulso mas vieja que esto se reporta como "sin medicion". */
#define BAND_VITALS_MAX_AGE_S     600u

/* ---- Colas ------------------------------------------------------------------*/
#define BAND_TX_QUEUE_LEN         8        /* alertas y telemetria hacia ESP-NOW      */
#define BAND_RX_QUEUE_LEN         4        /* HORA / CANCELAR desde el robot          */
#define BAND_CMD_QUEUE_LEN        2        /* comandos hacia la tarea de caidas       */
#define BAND_RX_MAX_LEN           16u      /* el mayor mensaje del robot mide 10 B    */

/* ---- Envio -------------------------------------------------------------------*/
#define BAND_TX_RETRIES           3        /* reintentos de una alerta si esp_now_send falla */
#define BAND_TX_RETRY_DELAY_MS    50u

/* ---- Tareas: prioridad (mayor = mas urgente) y pila en bytes ---------------------*/
#define BAND_PRIO_FALL     5     /* temporizacion estricta de 100 Hz        */
#define BAND_PRIO_COMM     4     /* una alerta no debe esperar              */
#define BAND_PRIO_VITALS   3
#define BAND_PRIO_MODEL    2     /* inferencia fuera de la tarea de IMU       */
#define BAND_PRIO_MODEL_INIT 6   /* inicializa TFLM antes de iniciar tFall    */
#define BAND_PRIO_SYSTEM   2

#define BAND_STACK_FALL    4096u
#define BAND_STACK_COMM    4096u
#define BAND_STACK_VITALS  4096u
#define BAND_STACK_MODEL   16384u
#define BAND_STACK_SYSTEM  4096u

#endif /* BAND_CONFIG_H */
