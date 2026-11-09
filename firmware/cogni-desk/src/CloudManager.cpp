#include "CloudManager.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include <time.h>

#include "alba_thingsboard.h"
#include "cloud_outbox.h"

#if __has_include("cloud_secrets.h")
#include "cloud_secrets.h"
#endif

#ifndef CLOUD_TB_HOST
#define CLOUD_TB_HOST ""
#endif
#ifndef CLOUD_TB_PORT
#define CLOUD_TB_PORT 1883
#endif
#ifndef CLOUD_TB_TLS
#define CLOUD_TB_TLS 0
#endif
#ifndef CLOUD_TB_TOKEN
#define CLOUD_TB_TOKEN ""
#endif

namespace {
constexpr uint32_t ENV_PERIOD_MS = 30000u;
constexpr uint32_t RECONNECT_MS = 5000u;
constexpr uint32_t LOCK_WAIT_MS = 20u;

#if CLOUD_TB_TLS
WiFiClientSecure net;
#else
WiFiClient net;
#endif
PubSubClient mqtt(net);

cloud_outbox_t outbox;
SemaphoreHandle_t lock;
bool enabled;

volatile bool connected_flag;
volatile uint32_t published_count;
volatile uint32_t failed_count;

uint32_t last_env_ms;
bool env_sent;
uint32_t last_game_revision;

// Hora Unix en ms, o 0 si el robot aun no sincronizo NTP (ClockManager); sin hora, ThingsBoard
// le pone la hora en que recibe el mensaje.
uint64_t epochMs() {
  const time_t now = time(nullptr);
  return now >= static_cast<time_t>(ALBA_EPOCH_MIN_S) ? static_cast<uint64_t>(now) * 1000ull : 0ull;
}

bool take() {
  return lock != nullptr && xSemaphoreTake(lock, pdMS_TO_TICKS(LOCK_WAIT_MS)) == pdTRUE;
}

void give() {
  xSemaphoreGive(lock);
}

void cloudTask(void *) {
  uint32_t next_try_ms = 0u;
  char buffer[CLOUD_JSON_MAX];

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (mqtt.connected()) mqtt.disconnect();
      connected_flag = false;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (!mqtt.connected()) {
      connected_flag = false;
      const uint32_t now = millis();
      if (static_cast<int32_t>(now - next_try_ms) < 0) {
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
      next_try_ms = now + RECONNECT_MS;
      Serial.printf("[nube] conectando a %s:%u ...\n", CLOUD_TB_HOST,
                    static_cast<unsigned>(CLOUD_TB_PORT));
      if (!mqtt.connect("alba-cogni-desk", CLOUD_TB_TOKEN, nullptr)) {
        Serial.printf("[nube] no se pudo conectar (MQTT rc=%d); reintento en %u s\n", mqtt.state(),
                      static_cast<unsigned>(RECONNECT_MS / 1000u));
        continue;
      }
      Serial.println(F("[nube] conectado a ThingsBoard"));
    }
    connected_flag = true;
    mqtt.loop();

    // Publica el mensaje mas antiguo; solo se quita de la cola si salio bien.
    bool have = false;
    if (take()) {
      const char *json = cloud_outbox_peek(&outbox);
      if (json != nullptr) {
        strncpy(buffer, json, sizeof(buffer) - 1u);
        buffer[sizeof(buffer) - 1u] = '\0';
        have = true;
      }
      give();
    }

    if (!have) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (mqtt.publish(ALBA_TB_TOPIC, buffer)) {
      if (take()) {
        const char *head = cloud_outbox_peek(&outbox);
        if (head != nullptr && strcmp(head, buffer) == 0) cloud_outbox_pop(&outbox);
        give();
      }
      ++published_count;
      Serial.printf("[nube] -> %s\n", buffer);
    } else {
      ++failed_count;
      Serial.println(F("[nube] publicacion fallida; se reintenta"));
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}
}

bool initCloud(void) {
  cloud_outbox_init(&outbox);
  lock = xSemaphoreCreateMutex();
  enabled = false;

  if (strlen(CLOUD_TB_HOST) == 0u || strlen(CLOUD_TB_TOKEN) == 0u || lock == nullptr) {
    Serial.println(F("[nube] desactivada: falta include/cloud_secrets.h (copiar cloud_secrets.example.h)"));
    return false;
  }

#if CLOUD_TB_TLS
  net.setInsecure();  // SOLO PRUEBAS: cifra, pero no verifica quien es el servidor
  Serial.println(F("[nube] AVISO: TLS sin verificar el certificado del servidor"));
#else
  Serial.println(F("[nube] AVISO: MQTT sin cifrar; el token viaja en claro"));
#endif
  net.setTimeout(5);
  mqtt.setServer(CLOUD_TB_HOST, CLOUD_TB_PORT);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);

  if (xTaskCreatePinnedToCore(cloudTask, "cloud", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println(F("[nube] no se pudo crear la tarea"));
    return false;
  }
  enabled = true;
  Serial.printf("[nube] activa: %s:%u\n", CLOUD_TB_HOST, static_cast<unsigned>(CLOUD_TB_PORT));
  return true;
}

void cloudOnTelemetry(const alba_telemetry_t *telemetry) {
  if (!enabled || telemetry == nullptr || !take()) return;
  cloud_outbox_push_telemetry(&outbox, telemetry, epochMs());
  give();
}

void cloudOnEvent(const alba_event_t *event) {
  if (!enabled || event == nullptr || !take()) return;
  cloud_outbox_push_event(&outbox, event, epochMs());
  give();
}

void cloudOnComfort(const char *text) {
  if (!enabled || text == nullptr || !take()) return;
  cloud_outbox_push_comfort(&outbox, epochMs(), text);
  give();
}

void actualizarCloud(uint32_t now_ms, const environment_reading_t *ambiente,
                     const game_result_t *juego) {
  if (!enabled) return;

  if (ambiente != nullptr && ambiente->valid &&
      (!env_sent || static_cast<uint32_t>(now_ms - last_env_ms) >= ENV_PERIOD_MS)) {
    if (take()) {
      cloud_outbox_push_env(&outbox, epochMs(),
                            static_cast<int16_t>(ambiente->temperature_c * 10.0f +
                                                 (ambiente->temperature_c < 0.0f ? -0.5f : 0.5f)),
                            static_cast<uint8_t>(ambiente->humidity_pct + 0.5f));
      give();
      last_env_ms = now_ms;
      env_sent = true;
    }
  }

  if (juego != nullptr && juego->revision != 0u && juego->revision != last_game_revision) {
    if (take()) {
      cloud_outbox_push_game(&outbox, epochMs(), juego->score, juego->total, juego->avg_ms);
      give();
      last_game_revision = juego->revision;
    }
  }
}

bool cloudConectado(void) {
  return connected_flag;
}

cloud_alert_show_t cloudEstadoAviso(void) {
  static cloud_alert_status_t last_status = CLOUD_ALERT_NONE;
  if (enabled && take()) {
    last_status = cloud_outbox_alert_status(&outbox);
    give();
  }
  return cloud_alert_show(last_status, enabled, connected_flag);
}

uint32_t cloudPublicados(void) {
  return published_count;
}
