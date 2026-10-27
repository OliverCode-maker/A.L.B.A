/**
 * port_arduino.cpp
 * Tiempo y log en la placa (ESP32 Arduino). Solo se compila en la placa.
 */
#ifdef ARDUINO

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

#include "band_port.h"

uint32_t port_millis(void) { return millis(); }

void port_log(const char *fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    const uint32_t ms = millis();
    Serial.printf("[%4lu.%03lu] %s\n", (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u), line);
}

#endif /* ARDUINO */
