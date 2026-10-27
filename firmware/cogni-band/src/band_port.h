/**
 * band_port.h
 * Frontera entre las tareas (band_tasks.cpp, que no saben de hardware) y lo
 * que cambia segun donde corran: la placa real, la simulacion en la placa o
 * el PC.  Las tareas solo llaman a estas funciones.
 *
 *   quien las implementa                 que son
 *   ----------------------------------   ---------------------------------
 *   port_arduino.cpp / port_host.cpp     tiempo y log
 *   port_sim.cpp          (BAND_SIM=1)   IMU, PPG, bateria y robot simulados
 *   (por escribir)        (BAND_SIM=0)   port_mpu6050, port_max30102, port_espnow
 */
#ifndef BAND_PORT_H
#define BAND_PORT_H

#include <stddef.h>
#include <stdint.h>

#include "fall_detector.h"

/* ---- Tiempo y log (siempre reales) ------------------------------------------*/
uint32_t port_millis(void);                                    /* ms desde el arranque */
void     port_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- Sensores ----------------------------------------------------------------*/
/** Una muestra del IMU (g y grados/s). false = error de lectura. Se llama cada 10 ms. */
bool port_imu_read(uint32_t now_ms, fall_sample_t *out);

/** Una medicion de pulso. Puede BLOQUEAR varios segundos (el MAX30102 necesita
 *  acumular muestras). false = sin lectura valida. */
bool port_ppg_read(uint8_t *bpm, uint8_t *spo2, uint8_t *sqi);

/** Voltaje de la bateria en mV; 0 = no se puede medir (sin divisor). */
uint16_t port_battery_mv(void);

/* ---- Radio -------------------------------------------------------------------*/
/** Envia un paquete al robot. true = el envio fue aceptado por la radio. */
bool port_send(const uint8_t *buf, size_t len);

/** Arranca lo que haga falta ademas de las tareas (en simulacion: el robot simulado). */
void port_start(void);

#endif /* BAND_PORT_H */
