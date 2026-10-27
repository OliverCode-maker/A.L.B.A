/* Tiempo y log para la prueba en el PC (equivalente a port_idf.cpp). */
#include <stdarg.h>
#include <stdio.h>

#include "band_os.h"
#include "band_port.h"

/* El tiempo es el de FreeRTOS (ticks de 1 ms), igual que en la placa. En el PC
 * el planificador POSIX puede ir un poco mas lento que el reloj de pared, y
 * asi todo el guion sigue siendo consistente. */
uint32_t port_millis(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void port_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    const uint32_t ms = port_millis();
    printf("[%4lu.%03lu] %s\n", (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u), line);
    fflush(stdout);
}
