/**
 * band_config.h
 * Parametros del firmware de la pulsera: periodos, colas, prioridades y el
 * modo de simulacion. Todo lo "ajustable" vive aqui, no en las tareas.
 */
#ifndef BAND_CONFIG_H
#define BAND_CONFIG_H

/* ---- Fuentes de datos ------------------------------------------------------
 * BAND_SIM = 1: el IMU, el PPG, la bateria y el robot son SIMULADOS (guion en
 *               port_sim.cpp). Sirve para ver todo el flujo en el monitor
 *               serie sin sensores ni robot.
 * BAND_SIM = 0: usa los drivers reales (aun por escribir: port_mpu6050,
 *               port_max30102, port_espnow). Ahora mismo no compila el enlace.
 * BAND_SIM_FAST = 1: guion comprimido (~100 s) y detector de vigilia con
 *               ventanas cortas, solo para la demo. 0 = tiempos reales
 *               (el despertar tarda ~10 min en aparecer). */
#ifndef BAND_SIM
#define BAND_SIM        1
#endif
#ifndef BAND_SIM_FAST
#define BAND_SIM_FAST   1
#endif

/* ---- Periodos ---------------------------------------------------------------*/
#define BAND_IMU_PERIOD_MS        10u      /* 100 Hz                                  */
#define BAND_ACTIVITY_PERIOD_MS   1000u    /* cada cuanto se pasa la actigrafia a vitales */
#define BAND_SYSTEM_PERIOD_MS     1000u

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
#define BAND_PRIO_SYSTEM   2

#define BAND_STACK_FALL    4096u
#define BAND_STACK_COMM    4096u
#define BAND_STACK_VITALS  4096u
#define BAND_STACK_SYSTEM  4096u

#endif /* BAND_CONFIG_H */
