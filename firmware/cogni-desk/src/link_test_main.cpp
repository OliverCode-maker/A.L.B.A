// PRUEBA DE ENLACE pulsera <-> robot (solo ESP-NOW, sin WiFi, sin nube, sin pantallas).
// Se compila con:  pio run -e linktest -t upload     (ver docs/prueba-enlace.md)
//
// El boton BOOT de la placa (GPIO0) hace de boton "estoy bien" del robot.
#include <Arduino.h>

#include "CommManager.h"

static const uint8_t BTN_OK_PIN_TEST = 0;   // boton BOOT
static uint32_t lastReport = 0;
static bool lastBtn = true;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=== ALBA: prueba de enlace (robot) ===");
    pinMode(BTN_OK_PIN_TEST, INPUT_PULLUP);
    initCommunication();
    Serial.println("Esperando a la pulsera. Pulsa BOOT cuando aparezca '¿Estas bien?'.");
}

void loop() {
    procesarComunicacionesBackground();
    sincronizarDatosDashboardBackground();   // sin WiFi no hace nada

    const bool btn = digitalRead(BTN_OK_PIN_TEST);
    if (lastBtn && !btn) {                   // flanco de bajada = pulsado
        if (commPromptCaidaActivo()) {
            Serial.println("[test] BOTON OK pulsado -> se enviara CANCELAR a la pulsera");
            commBotonEstoyBien();
        } else {
            Serial.println("[test] BOTON OK pulsado, pero no hay una caida que atender");
        }
        delay(30);                           // antirrebote simple
    }
    lastBtn = btn;

    static bool prompted = false;
    const bool p = commPromptCaidaActivo();
    if (p && !prompted) Serial.println("[test] >>> ¿ESTAS BIEN? (tienes 20 s: pulsa BOOT)");
    prompted = p;
    if (commTomarAyudaAvisada()) Serial.println("[test] >>> Caida CONFIRMADA: 'ya avise a tu cuidador'");

    if (millis() - lastReport >= 5000) {
        lastReport = millis();
        Serial.printf("[test] pulsera %s | bpm=%u spo2=%u bat=%u | WiFi %s | nube %s | publicados=%lu en cola=%lu\n",
                      commPulseraOnline() ? "EN LINEA" : "sin senal",
                      (unsigned)commBpm(), (unsigned)commSpo2(), (unsigned)commBateria(),
                      commWifiConectado() ? "si" : "no", commNubeConectada() ? "si" : "no",
                      (unsigned long)commMensajesPublicados(), (unsigned long)commMensajesEnCola());
    }
}
