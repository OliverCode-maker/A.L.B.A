/**
 * port_idf.c
 * Tiempo y log en la placa (ESP-IDF).
 */
#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "band_port.h"

static const char *TAG = "band";

uint32_t port_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void port_log(const char *fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    const uint32_t ms = port_millis();
    ESP_LOGI(TAG, "[%4lu.%03lu] %s", (unsigned long)(ms / 1000u),
             (unsigned long)(ms % 1000u), line);
}
