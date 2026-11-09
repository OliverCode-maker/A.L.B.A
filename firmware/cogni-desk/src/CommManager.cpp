#include "CommManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_idf_version.h>
#include <string.h>
#include <time.h>

#include "CloudManager.h"
#include "DeskConfig.h"
#include "link_stats.h"
#include "robot_link.h"

namespace {
constexpr size_t RX_QUEUE_LENGTH = 4;

struct ReceivedPacket {
  uint8_t length;
  uint8_t data[ALBA_ESPNOW_MAX_PAYLOAD];
};

const uint8_t band_mac[ESP_NOW_ETH_ALEN] = DESK_BAND_STA_MAC;
const uint8_t expected_sta_mac[ESP_NOW_ETH_ALEN] = DESK_EXPECTED_STA_MAC;
QueueHandle_t rx_queue;
robot_link_t link_state;
link_stats_t stats;
uint32_t last_diag_ms;

// Envio de la HORA a la pulsera: solo cuenta como enviada si la radio de la pulsera devuelve el ACK.
constexpr uint32_t TIME_ACK_TIMEOUT_MS = 1000u;
constexpr uint32_t TIME_RETRY_GAP_MS = 5000u;
volatile bool time_awaiting;
volatile int8_t time_ack_result = -1;  // -1 sin resultado, 0 sin ACK, 1 con ACK
uint32_t time_attempt_ms;
uint32_t time_next_try_ms;
bool time_waiting_ntp_logged;
uint16_t robot_sequence;
bool communication_ready;
volatile uint32_t dropped_packets;
uint32_t reported_drops;

health_reading_t last_health;      // ultimos vitales validos recibidos
uint32_t last_health_ms;
bool health_seen;

bool checkEspResult(esp_err_t result, const char *operation) {
  if (result == ESP_OK) return true;
  Serial.printf("[esp-now] %s: %s\n", operation, esp_err_to_name(result));
  return false;
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  if (info == nullptr || info->src_addr == nullptr || data == nullptr ||
      length <= 0 || length > static_cast<int>(ALBA_ESPNOW_MAX_PAYLOAD) ||
      memcmp(info->src_addr, band_mac, ESP_NOW_ETH_ALEN) != 0) {
    return;
  }

  ReceivedPacket packet = {};
  packet.length = static_cast<uint8_t>(length);
  memcpy(packet.data, data, static_cast<size_t>(length));
  if (rx_queue == nullptr || xQueueSend(rx_queue, &packet, 0) != pdTRUE) {
    __atomic_fetch_add(&dropped_packets, 1u, __ATOMIC_RELAXED);
  }
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onSend(const esp_now_send_info_t *, esp_now_send_status_t status) {
#else
void onSend(const uint8_t *, esp_now_send_status_t status) {
#endif
  if (time_awaiting) time_ack_result = (status == ESP_NOW_SEND_SUCCESS) ? 1 : 0;
}

// Manda HORA a la pulsera cuando robot_link lo pide (al aparecer, tras reiniciarse y cada 6 h).
void actualizarHora(uint32_t now_ms) {
  if (!communication_ready) return;

  if (time_awaiting) {
    const int8_t result = time_ack_result;
    if (result < 0 && static_cast<uint32_t>(now_ms - time_attempt_ms) < TIME_ACK_TIMEOUT_MS) return;
    time_awaiting = false;
    if (result == 1) {
      robot_link_time_sent(&link_state, now_ms);
      Serial.println(F("[hora] HORA recibida por la pulsera (ACK)"));
    } else {
      Serial.println(F("[hora] la pulsera no confirmo la HORA; reintento en unos segundos"));
    }
    return;
  }

  if (!robot_link_need_time(&link_state, now_ms)) return;
  if (static_cast<int32_t>(now_ms - time_next_try_ms) < 0) return;
  time_next_try_ms = now_ms + TIME_RETRY_GAP_MS;

  const time_t epoch = time(nullptr);
  if (epoch < static_cast<time_t>(ALBA_EPOCH_MIN_S)) {
    if (!time_waiting_ntp_logged) {
      time_waiting_ntp_logged = true;
      Serial.println(F("[hora] la pulsera pide la hora, pero el robot aun no tiene hora NTP (necesita Wi-Fi)"));
    }
    return;
  }

  alba_time_t packet;
  alba_pack_time(&packet, robot_sequence++, static_cast<uint32_t>(epoch),
                 static_cast<int16_t>(DESK_UTC_OFFSET_MIN));
  time_ack_result = -1;
  time_awaiting = true;
  time_attempt_ms = now_ms;
  if (!checkEspResult(esp_now_send(band_mac, reinterpret_cast<const uint8_t *>(&packet),
                                   sizeof(packet)),
                      "enviar HORA")) {
    time_awaiting = false;
    return;
  }
  Serial.printf("[hora] HORA enviada: epoch=%lu zona=%d min\n", static_cast<unsigned long>(epoch),
                static_cast<int>(DESK_UTC_OFFSET_MIN));
}

void handleReceivedPacket(desk_app_t *app, const ReceivedPacket &packet, uint32_t now_ms) {
  robot_rx_msg_t message;
  if (robot_link_on_packet(&link_state, packet.data, packet.length, now_ms, &message) !=
      ROBOT_RX_NEW) {
    return;
  }

  link_stats_on_seq(&stats, message.type == ALBA_MSG_TELEMETRIA ? message.tel.hdr.seq
                                                                : message.evt.hdr.seq);

  if (message.type == ALBA_MSG_TELEMETRIA) {
    last_health.bpm_valid = message.tel.bpm != ALBA_BPM_INVALID;
    last_health.bpm = message.tel.bpm;
    last_health.spo2_valid = message.tel.spo2 != ALBA_SPO2_INVALID;
    last_health.spo2 = message.tel.spo2;
    last_health.bat_valid = message.tel.bateria_pct != ALBA_BAT_INVALID;
    last_health.bat_pct = message.tel.bateria_pct;
    last_health_ms = now_ms;
    health_seen = true;
    cloudOnTelemetry(&message.tel);
    Serial.printf("[pulsera] BPM=%u SpO2=%u bateria=%u%%\n",
                  static_cast<unsigned>(message.tel.bpm),
                  static_cast<unsigned>(message.tel.spo2),
                  static_cast<unsigned>(message.tel.bateria_pct));
    return;
  }

  Serial.printf("[pulsera] alerta=%u confirmacion=%u seq=%u\n",
                static_cast<unsigned>(message.evt.alerta_tipo),
                static_cast<unsigned>(message.evt.confirmacion),
                static_cast<unsigned>(message.evt.hdr.seq));
  cloudOnEvent(&message.evt);
  ejecutarAccionRobot(
      desk_app_on_fall_event(app, &message.evt, now_ms, nullptr), 0u);
}
}

bool initCommunication(void) {
  robot_link_init(&link_state);
  link_stats_init(&stats);
  rx_queue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(ReceivedPacket));
  if (rx_queue == nullptr) {
    Serial.println(F("[esp-now] no se pudo crear la cola de recepcion"));
    return false;
  }

  if (!WiFi.mode(WIFI_STA)) {
    Serial.println(F("[esp-now] no se pudo activar el modo Wi-Fi STA"));
    return false;
  }

  uint8_t local_mac[ESP_NOW_ETH_ALEN];
  if (!checkEspResult(esp_wifi_get_mac(WIFI_IF_STA, local_mac), "leer MAC STA")) return false;
  Serial.printf("[esp-now] MAC STA del escritorio: %02X:%02X:%02X:%02X:%02X:%02X\n",
                local_mac[0], local_mac[1], local_mac[2],
                local_mac[3], local_mac[4], local_mac[5]);
  if (memcmp(local_mac, expected_sta_mac, ESP_NOW_ETH_ALEN) != 0) {
    Serial.println(F("[esp-now] la MAC no coincide con DESK_EXPECTED_STA_MAC de la pulsera"));
    return false;
  }

  if (!checkEspResult(esp_wifi_set_channel(DESK_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE),
                      "configurar canal")) return false;
  if (!checkEspResult(esp_now_init(), "inicializar ESP-NOW")) return false;
  if (!checkEspResult(esp_now_register_recv_cb(onReceive), "registrar recepcion")) return false;
  if (!checkEspResult(esp_now_register_send_cb(onSend), "registrar envio")) return false;

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, band_mac, ESP_NOW_ETH_ALEN);
  peer.channel = 0;  // 0 = canal actual de la radio (cambia cuando el Wi-Fi se conecta al router)
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (!checkEspResult(esp_now_add_peer(&peer), "agregar Cogni-Band como peer")) return false;

  communication_ready = true;
  Serial.printf("[esp-now] listo; pulsera=%02X:%02X:%02X:%02X:%02X:%02X canal=%u\n",
                band_mac[0], band_mac[1], band_mac[2],
                band_mac[3], band_mac[4], band_mac[5],
                static_cast<unsigned>(DESK_ESPNOW_CHANNEL));
  return true;
}

void procesarComunicacionesBackground(desk_app_t *app, uint32_t now_ms) {
  if (app == nullptr || rx_queue == nullptr) return;

  ReceivedPacket packet;
  while (xQueueReceive(rx_queue, &packet, 0) == pdTRUE) {
    handleReceivedPacket(app, packet, now_ms);
  }

  actualizarHora(now_ms);

  // Diagnostico cada 10 s: canal real de la radio y paquetes perdidos segun el seq de la pulsera.
  if (static_cast<uint32_t>(now_ms - last_diag_ms) >= 10000u) {
    last_diag_ms = now_ms;
    uint8_t primary = 0u;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primary, &second);
    Serial.printf("[diag] canal=%u wifi=%s nube=%s recibidos=%lu perdidos=%lu (%u%%) reinicios_pulsera=%lu\n",
                  static_cast<unsigned>(primary),
                  WiFi.status() == WL_CONNECTED ? "si" : "no",
                  cloudConectado() ? "si" : "no",
                  static_cast<unsigned long>(stats.received),
                  static_cast<unsigned long>(stats.lost),
                  static_cast<unsigned>(link_stats_loss_pct(&stats)),
                  static_cast<unsigned long>(stats.resets));
  }

  const uint32_t dropped = __atomic_load_n(&dropped_packets, __ATOMIC_RELAXED);
  if (dropped != reported_drops) {
    reported_drops = dropped;
    Serial.printf("[esp-now] paquetes descartados por cola llena: %lu\n",
                  static_cast<unsigned long>(reported_drops));
  }
}

health_reading_t leerSalud(uint32_t now_ms) {
  health_reading_t reading = last_health;
  reading.online = health_seen &&
                   static_cast<uint32_t>(now_ms - last_health_ms) < DESK_HEALTH_STALE_MS;
  if (!reading.online) {
    reading.bpm_valid = false;
    reading.spo2_valid = false;
    reading.bat_valid = false;
  }
  return reading;
}

void ejecutarAccionRobot(robot_action_t action, uint16_t ref_seq) {
  if (action != ROBOT_ACT_SEND_CANCEL) return;
  if (!communication_ready) {
    Serial.println(F("[esp-now] CANCELAR no enviado: radio no disponible"));
    return;
  }

  alba_cancel_t cancel;
  alba_pack_cancel(&cancel, robot_sequence++, ref_seq);
  if (!checkEspResult(esp_now_send(band_mac, reinterpret_cast<const uint8_t *>(&cancel),
                                   sizeof(cancel)),
                      "enviar CANCELAR")) {
    Serial.printf("[esp-now] no se envio CANCELAR para la caida seq=%u\n",
                  static_cast<unsigned>(ref_seq));
    return;
  }
  Serial.printf("[esp-now] CANCELAR aceptado para transmision; caida seq=%u\n",
                static_cast<unsigned>(ref_seq));
}
