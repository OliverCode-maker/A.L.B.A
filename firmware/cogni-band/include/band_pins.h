/**
 * band_pins.h
 * Pines de la pulsera: ESP32-S3 Super Mini (placa lolin_s3_mini en PlatformIO).
 * Fuente: respuestas de Gaby sobre el montaje. Cambiar aqui si el cableado cambia.
 *
 * Reglas del S3 a tener en cuenta:
 *  - GPIO 1-5 son ADC1 (funcionan con WiFi/ESP-NOW activo; ADC2 no).
 *  - Evitar para botones/entradas: GPIO0 (BOOT), GPIO3, GPIO45, GPIO46 (arranque).
 */
#ifndef BAND_PINS_H
#define BAND_PINS_H

#define BAND_PIN_I2C_SDA   6    /* MPU6050 y MAX30102 comparten este bus     */
#define BAND_PIN_I2C_SCL   7
#define BAND_I2C_HZ        400000

/* El MPU6050 y el MAX30102 NO tienen el pin INT conectado: se leen por
 * sondeo (polling). Si algun dia se conectan, agregar aqui sus pines. */

#define BAND_PIN_BAT_ADC   1    /* divisor 2 x 100 kOhm + 100 nF a GND (propuesto) */
#define BAND_BAT_DIVIDER   2    /* el pin ve la mitad del voltaje de la bateria    */

#define BAND_PIN_BUTTON    (-1) /* sin boton todavia (propuesto: GPIO5, pull-up)   */
#define BAND_PIN_LED_RGB   48   /* LED RGB de la placa (para estados)              */

#endif /* BAND_PINS_H */
