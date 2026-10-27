// cogni-band: firmware de la pulsera (ESP32-S3 Super Mini).
//
// app_main() solo arranca la arquitectura FreeRTOS (src/band_tasks.cpp); de ahi
// en adelante todo lo hacen las tareas. Con BAND_SIM = 1 los sensores y el
// robot son simulados. Ver band_config.h.
#include "esp_log.h"

#include "alba_protocol.h"
#include "band_config.h"
#include "band_port.h"
#include "band_tasks.h"

static const char *TAG = "cogni-band";

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "ALBA cogni-band: arranque OK");
  ESP_LOGI(TAG,
           "Protocolo v%u | telemetria=%u B | evento=%u B | hora=%u B | cancelar=%u B",
           (unsigned)ALBA_PROTO_VERSION, (unsigned)sizeof(alba_telemetry_t),
           (unsigned)sizeof(alba_event_t), (unsigned)sizeof(alba_time_t),
           (unsigned)sizeof(alba_cancel_t));
  ESP_LOGI(TAG, "Modo: %s%s", BAND_SIM ? "SIMULACION" : "SENSORES REALES",
           (BAND_SIM && BAND_SIM_FAST) ? " (guion rapido)" : "");

  if (!band_tasks_start()) {
    ESP_LOGE(TAG, "No se pudieron crear las tareas (sin memoria?)");
    return;
  }
  port_start();
}
