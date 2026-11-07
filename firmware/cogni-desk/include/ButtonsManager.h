#ifndef BUTTONS_MANAGER_H
#define BUTTONS_MANAGER_H

// Definición de pines de botones
#define BTN_IZQ_PIN  4
#define BTN_DER_PIN  5
#define BTN_OK_PIN   6

// Prototipo de la función de configuración inicial
void initButtons();
void actualizarBotones();
void leerBotonOKPresionado();

#endif