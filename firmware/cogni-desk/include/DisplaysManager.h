#ifndef DISPLAYS_MANAGER_H
#define DISPLAYS_MANAGER_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Declaraciones extern (visibles para todo el proyecto que incluya este .h)
extern Adafruit_SSD1306 oledIzquierdo;
extern Adafruit_SSD1306 oledDerecho;

// Definición de pines de botones
#define BTN_IZQ_PIN  4
#define BTN_DER_PIN  5
#define BTN_OK_PIN   6

// Prototipo de la función de configuración inicial
void initDisplays();
void initButtons();
void mostrarSplashScreenALBA();
void mostrarOjosALBA();
void mostrarOjosCerradosALBA();
void mostrarMenuIconos(int opcionSeleccionada);
#endif