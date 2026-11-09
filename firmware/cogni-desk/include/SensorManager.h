#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;
    float temperature_c;
    float humidity_pct;
    uint32_t sampled_at_ms;
} environment_reading_t;

void initSensors(void);
bool actualizarSensores(uint32_t now_ms);
environment_reading_t leerAmbiente(void);

#endif
