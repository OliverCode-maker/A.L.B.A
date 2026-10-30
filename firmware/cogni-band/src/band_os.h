/**
 * band_os.h
 * Incluye FreeRTOS igual en la placa (ESP-IDF) y en el PC (puerto POSIX usado
 * para probar las tareas sin hardware).
 */
#ifndef BAND_OS_H
#define BAND_OS_H

#if defined(ESP_PLATFORM)
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/queue.h"
  #include "freertos/semphr.h"
  /* En ESP-IDF la profundidad de pila de xTaskCreate se da en bytes. */
  #define BAND_STACK(bytes)  (bytes)
#elif defined(ARDUINO)
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
