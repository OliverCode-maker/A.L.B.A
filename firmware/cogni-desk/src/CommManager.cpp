#include "CommManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <sys/time.h>
#include <time.h>

#include "alba_thingsboard.h"
#include "comm_core.h"

#if __has_include("comm_secrets.h")
#include "comm_secrets.h"
#else
// Sin credenciales el robot compila igual (para no romper el trabajo de los demas), pero
// no se conecta a WiFi ni a la nube. Copia include/comm_secrets.example.h a include/comm_secrets.h.
#warning "Falta include/comm_secrets.h: sin WiFi ni nube hasta que lo crees (ver comm_secrets.example.h)."
#define COMM_WIFI_SSID  ""
#define COMM_WIFI_PASS  ""
#define COMM_TB_HOST    "localhost"
#define COMM_TB_PORT    8883
#define COMM_TB_TOKEN   ""
#endif

// Opciones para la PRUEBA DE ENLACE con la pulsera (ver platformio.ini, env linktest):
//   COMM_WIFI_DISABLED=1   no se conecta al router ni a la nube; solo ESP-NOW
//   COMM_ESPNOW_CHANNEL=N  fija el canal de radio (debe ser el mismo de la pulsera)
#ifndef COMM_WIFI_DISABLED
#define COMM_WIFI_DISABLED 0
#endif
#ifndef COMM_ESPNOW_CHANNEL
#define COMM_ESPNOW_CHANNEL 0       // 0 = no tocar el canal (usa el del router)
#endif

#ifndef COMM_UTC_OFFSET_MIN
#define COMM_UTC_OFFSET_MIN (-300)
#endif

namespace {

constexpr uint32_t TICK_MS            = 100;
constexpr uint32_t WIFI_RETRY_MS      = 10000;
constexpr uint32_t MQTT_RETRY_MS      = 30000;   // cada intento puede bloquear unos segundos
constexpr uint32_t ENV_PERIOD_MS      = 60000;
constexpr uint8_t  MAX_PUBLISH_PER_CALL = 4;
constexpr size_t   RX_MAX             = 64;      // los paquetes de la pulsera miden ~12 bytes
constexpr uint8_t  RX_QUEUE_LEN       = 8;

struct RxPacket {
    uint8_t len;
    uint8_t data[RX_MAX];
};

comm_core_t        core;
QueueHandle_t      rxQueue = nullptr;
WiFiClientSecure   tlsClient;
PubSubClient       mqtt(tlsClient);
bool               started = false;
uint32_t           lastTick = 0, lastWifiTry = 0, lastMqttTry = 0, lastEnv = 0;
bool               haveEnvSent = false;

const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---- hora real (NTP) --------------------------------------------------------
uint32_t epochSeconds() {
#ifdef COMM_FAKE_EPOCH_S
    // Solo para la prueba de enlace sin internet: una hora inventada que avanza con el reloj.
    return (uint32_t)COMM_FAKE_EPOCH_S + millis() / 1000u;
#else
    const time_t t = time(nullptr);
    return (t >= (time_t)ALBA_EPOCH_MIN_S) ? (uint32_t)t : 0u;   // 0 = aun no hay hora real
#endif
}

uint64_t epochMillis() {
    if (epochSeconds() == 0u) return 0u;
#ifdef COMM_FAKE_EPOCH_S
    return (uint64_t)COMM_FAKE_EPOCH_S * 1000ULL + (uint64_t)millis();
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
#endif
}

// ---- ESP-NOW ----------------------------------------------------------------
bool sendToBand(const uint8_t *buf, size_t len, void *) {
    const bool ok = esp_now_send(BROADCAST, buf, len) == ESP_OK;
    alba_hdr_t h;
    if (len >= sizeof h) {
        memcpy(&h, buf, sizeof h);
        Serial.printf("[comm] TX a la pulsera: tipo=%u seq=%u (%u B) %s\n", (unsigned)h.type, (unsigned)h.seq,
                      (unsigned)len, ok ? "enviado" : "RECHAZADO por la radio");
    }
    return ok;
}

// El callback corre en la tarea de WiFi: SOLO copia a la cola. Lo demas lo hace loop().
void enqueuePacket(const uint8_t *data, int len) {
    if (rxQueue == nullptr || data == nullptr || len <= 0 || (size_t)len > RX_MAX) return;
    RxPacket p;
    p.len = (uint8_t)len;
    memcpy(p.data, data, (size_t)len);
    xQueueSend(rxQueue, &p, 0);          // cola llena = se descarta (la pulsera reenvia lo critico)
}

#if ESP_IDF_VERSION_MAJOR >= 5
void onEspNowRecv(const esp_now_recv_info_t *, const uint8_t *data, int len) { enqueuePacket(data, len); }
#else
void onEspNowRecv(const uint8_t *, const uint8_t *data, int len) { enqueuePacket(data, len); }
#endif

bool startEspNow() {
    if (esp_now_init() != ESP_OK) return false;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST, 6);
    peer.channel = 0;                    // 0 = el canal actual del WiFi (debe ser el mismo que usa la pulsera)
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) return false;
    return esp_now_register_recv_cb(onEspNowRecv) == ESP_OK;
}

// ---- WiFi / MQTT ------------------------------------------------------------
void ensureWifi(uint32_t now) {
    if (COMM_WIFI_DISABLED) return;
    if (WiFi.status() == WL_CONNECTED) return;
    if (lastWifiTry != 0 && now - lastWifiTry < WIFI_RETRY_MS) return;
    lastWifiTry = now ? now : 1;
    WiFi.begin(COMM_WIFI_SSID, COMM_WIFI_PASS);   // no bloquea: el estado se revisa en la siguiente vuelta
}

void ensureMqtt(uint32_t now) {
    if (COMM_WIFI_DISABLED) return;
    if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
    if (lastMqttTry != 0 && now - lastMqttTry < MQTT_RETRY_MS) return;
    lastMqttTry = now ? now : 1;
    if (!mqtt.connect("alba-robot", COMM_TB_TOKEN, nullptr)) {   // usuario = token del dispositivo
        Serial.printf("[comm] MQTT no conecto (estado %d)\n", mqtt.state());
    } else {
        Serial.println("[comm] MQTT conectado a ThingsBoard");
    }
}

}  // namespace

// =============================================================================

void initCommunication() {
    comm_core_init(&core, sendToBand, nullptr);
    rxQueue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxPacket));

    WiFi.mode(WIFI_STA);
#if !COMM_WIFI_DISABLED
    WiFi.begin(COMM_WIFI_SSID, COMM_WIFI_PASS);
    lastWifiTry = millis() ? millis() : 1;
#endif
#if COMM_ESPNOW_CHANNEL != 0
    esp_wifi_set_channel(COMM_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
#endif

    if (!startEspNow()) Serial.println("[comm] ERROR: no se pudo iniciar ESP-NOW");
    else Serial.printf("[comm] ESP-NOW listo | MAC del robot %s | broadcast\n", WiFi.macAddress().c_str());
    configTime(0, 0, "pool.ntp.org", "time.google.com");   // hora UTC; el desfase local va en el mensaje HORA

#ifdef COMM_TB_CA_CERT
    tlsClient.setCACert(COMM_TB_CA_CERT);
#else
    tlsClient.setInsecure();   // SOLO PRUEBAS: cifra, pero no verifica quien es el servidor
    Serial.println("[comm] AVISO: TLS sin verificar el certificado del servidor (define COMM_TB_CA_CERT)");
#endif
    tlsClient.setTimeout(5);   // segundos
    mqtt.setServer(COMM_TB_HOST, COMM_TB_PORT);
    mqtt.setBufferSize(384);
    mqtt.setSocketTimeout(5);
    started = true;
}

void procesarComunicacionesBackground() {
    if (!started) return;
    const uint32_t now = millis();

    RxPacket p;
    while (xQueueReceive(rxQueue, &p, 0) == pdTRUE) {
        alba_hdr_t h;
        if (p.len >= sizeof h) {
            memcpy(&h, p.data, sizeof h);
            Serial.printf("[comm] RX de la pulsera: tipo=%u seq=%u (%u B)\n", (unsigned)h.type, (unsigned)h.seq,
                          (unsigned)p.len);
        }
        comm_core_on_packet(&core, p.data, p.len, now, epochMillis());
    }

    if (now - lastTick >= TICK_MS) {
        lastTick = now;
        comm_core_tick(&core, now, epochSeconds(), (int16_t)COMM_UTC_OFFSET_MIN);
    }
}

void sincronizarDatosDashboardBackground() {
    if (!started) return;
    const uint32_t now = millis();
    ensureWifi(now);
    ensureMqtt(now);
    if (!mqtt.connected()) return;

    mqtt.loop();
    for (uint8_t i = 0; i < MAX_PUBLISH_PER_CALL; i++) {
        const char *json = comm_core_outbox_peek(&core);
        if (json == nullptr) break;
        if (!mqtt.publish(ALBA_TB_TOPIC, json)) break;   // no se quita de la cola: se reintenta
        comm_core_outbox_pop(&core);
    }
}

void commBotonEstoyBien() {
    if (started) comm_core_button_ok(&core, millis());
}
bool commPromptCaidaActivo() { return started && comm_core_prompt_active(&core); }
bool commTomarAyudaAvisada() { return started && comm_core_take_help_called(&core); }

bool    commHayDatosPulsera() { return started && core.have_vitals; }
uint8_t commBpm()             { return core.bpm; }
uint8_t commSpo2()            { return core.spo2; }
uint8_t commBateria()         { return core.bat; }
bool    commPulseraOnline()   { return started && comm_core_band_online(&core, millis()); }

void commPublicarAmbiente(int16_t temp_c_x10, uint8_t hum_pct) {
    if (!started) return;
    const uint32_t now = millis();
    if (haveEnvSent && now - lastEnv < ENV_PERIOD_MS) return;
    lastEnv = now;
    haveEnvSent = true;
    comm_core_push_env(&core, epochMillis(), temp_c_x10, hum_pct);
}

void registrarTomaEnDashboard() {
    if (!started) return;
    char json[COMM_JSON_MAX];
    const uint64_t ms = epochMillis();
    if (ms != 0u) snprintf(json, sizeof json, "{\"ts\":%llu,\"values\":{\"med_taken\":true}}", (unsigned long long)ms);
    else          snprintf(json, sizeof json, "{\"values\":{\"med_taken\":true}}");
    comm_core_push_raw(&core, json, true);
}
