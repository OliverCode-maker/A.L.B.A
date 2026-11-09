#ifndef DISPLAYS_MANAGER_H
#define DISPLAYS_MANAGER_H

#include "DeskApp.h"
#include "SensorManager.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

extern Adafruit_SSD1306 oledIzquierdo;
extern Adafruit_SSD1306 oledDerecho;

bool initDisplays(void);
void mostrarSplashScreenALBA(void);
void mostrarOjosALBA(void);
void mostrarOjosCerradosALBA(void);
void mostrarVista(const desk_view_t *vista, const environment_reading_t *ambiente);

#endif