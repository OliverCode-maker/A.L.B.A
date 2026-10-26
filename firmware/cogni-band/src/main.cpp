// cogni-band: firmware de la pulsera (ESP32-C3).
//
// Por ahora solo comprueba que los modulos enlazan y que los tamanos del
// protocolo son los esperados. Las tareas de FreeRTOS (adquisicion I2C,
// procesamiento, comunicaciones) se agregan en la Tarea 1 completa.
#include <Arduino.h>

#include "alba_protocol.h"
#include "fall_detector.h"
#include "wake_detector.h"

// Declarados static: ocupan ~2.8 kB y no deben ir en el stack de una tarea.
static fall_detector_t g_fall;
static wake_detector_t g_wake;

void setup() {
  Serial.begin(115200);
  delay(2000);  // da tiempo a que el USB CDC levante antes del primer mensaje

  fall_detector_init(&g_fall, nullptr);   // configuracion por defecto
  wake_detector_init(&g_wake, nullptr);

  Serial.println("ALBA cogni-band: arranque OK");
  Serial.printf("Protocolo v%u | telemetria=%u B | evento=%u B\n",
                (unsigned)ALBA_PROTO_VERSION,
                (unsigned)sizeof(alba_telemetry_t),
                (unsigned)sizeof(alba_event_t));
  Serial.printf("Detector de caidas=%u B | detector de vigilia=%u B\n",
                (unsigned)sizeof(g_fall), (unsigned)sizeof(g_wake));
}

void loop() {
  delay(1000);
}
