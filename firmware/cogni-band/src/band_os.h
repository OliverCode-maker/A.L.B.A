/**
 * band_os.h
 * Incluye FreeRTOS igual en la placa (ESP32 Arduino) y en el PC (puerto POSIX
 * usado para probar las tareas sin hardware).
 */
#ifndef BAND_OS_H
#define BAND_OS_H

#ifdef ARDUINO
  #include <Arduino.h>
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/queue.h"
  #include "freertos/semphr.h"
  /* En ESP32 la profundidad de pila de xTaskCreate se da en BYTES. */
  #define BAND_STACK(bytes)  (bytes)
#else
  #include "FreeRTOS.h"
  #include "task.h"
  #include "queue.h"
  #include "semphr.h"
  /* En FreeRTOS estandar se da en palabras. */
  #define BAND_STACK(bytes)  ((bytes) / sizeof(StackType_t))
#endif

#endif /* BAND_OS_H */
