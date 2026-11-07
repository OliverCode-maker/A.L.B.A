/**
 * band_radio.h
 * Radio ESP-NOW de la pulsera.
 *
 *   BAND_RADIO_REAL = 1  usa la radio de verdad aunque los sensores sean simulados
 *                        (es el modo de la "prueba de enlace": BAND_SIM=1 + BAND_RADIO_REAL=1).
 *   BAND_SIM = 0         (sensores reales) siempre usa la radio real.
 *   BAND_ESPNOW_CHANNEL  canal WiFi fijo. Debe ser el MISMO en el robot.
 */
#ifndef BAND_RADIO_H
#define BAND_RADIO_H

#include "band_config.h"

#ifndef BAND_RADIO_REAL
#define BAND_RADIO_REAL 0
#endif

#ifndef BAND_ESPNOW_CHANNEL
#define BAND_ESPNOW_CHANNEL 1
#endif

#if !BAND_SIM || BAND_RADIO_REAL
#define BAND_USE_REAL_RADIO 1
#else
#define BAND_USE_REAL_RADIO 0
#endif

/** Inicia WiFi (solo para ESP-NOW, sin conectarse a ningun router) y ESP-NOW. */
void port_radio_start(void);

#endif
