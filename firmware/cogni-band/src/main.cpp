// cogni-band: firmware de la pulsera (ESP32-S3 Super Mini).
//
// setup() solo arranca la arquitectura FreeRTOS (src/band_tasks.cpp); de ahi
// en adelante todo lo hacen las tareas. Con BAND_SIM = 1 (por defecto) los
// sensores y el robot son simulados: abre el monitor serie y veras la caida,
// la cancelacion, el despertar y la bateria baja. Ver band_config.h.
#include <Arduino.h>

#include "alba_protocol.h"
#include "band_config.h"
#include "band_port.h"
#include "band_tasks.h"

void setup() {
  Serial.begin(115200);
  delay(2000);  // da tiempo a que el USB CDC levante antes del primer mensaje

  Serial.println("ALBA cogni-band: arranque OK");
  Serial.printf("Protocolo v%u | telemetria=%u B | evento=%u B | hora=%u B | cancelar=%u B\n",
                (unsigned)ALBA_PROTO_VERSION, (unsigned)sizeof(alba_telemetry_t),
                (unsigned)sizeof(alba_event_t), (unsigned)sizeof(alba_time_t),
                (unsigned)sizeof(alba_cancel_t));
  Serial.printf("Modo: %s%s\n", BAND_SIM ? "SIMULACION" : "SENSORES REALES",
                (BAND_SIM && BAND_SIM_FAST) ? " (guion rapido)" : "");

  if (!band_tasks_start()) {
    Serial.println("ERROR: no se pudieron crear las tareas (sin memoria?)");
    return;
  }
  port_start();
}

void loop() {
  // Todo el trabajo esta en las tareas; esta tarea de Arduino queda libre.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
