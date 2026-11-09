#include "CommManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

#include "DeskConfig.h"
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

void handleReceivedPacket(desk_app_t *app, const ReceivedPacket &packet, uint32_t now_ms) {
  robot_rx_msg_t message;
  if (robot_link_on_packet(&link_state, packet.data, packet.length, now_ms, &message) !=
      ROBOT_RX_NEW) {
    return;
  }

  if (message.type == ALBA_MSG_TELEMETRIA) {
    last_health.bpm_valid = message.tel.bpm != ALBA_BPM_INVALID;
    last_health.bpm = message.tel.bpm;
    last_health.spo2_valid = message.tel.spo2 != ALBA_SPO2_INVALID;
    last_health.spo2 = message.tel.spo2;
    last_health.bat_valid = message.tel.bateria_pct != ALBA_BAT_INVALID;
    last_health.bat_pct = message.tel.bateria_pct;
    last_health_ms = now_ms;
    health_seen = true;
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
  ejecutarAccionRobot(
      desk_app_on_fall_event(app, &message.evt, now_ms, nullptr), 0u);
}
}

bool initCommunication(void) {
  robot_link_init(&link_state);
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

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, band_mac, ESP_NOW_ETH_ALEN);
  peer.channel = DESK_ESPNOW_CHANNEL;
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
