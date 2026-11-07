/**
 * port_espnow.c
 * Radio ESP-NOW de la pulsera (ESP-IDF).
 *
 * Todo va por BROADCAST (FF:FF:FF:FF:FF:FF): no hace falta saber la MAC del robot ni
 * emparejar. Contras: no hay confirmacion de entrega ni reintentos de la radio (por eso la
 * pulsera reenvia sus alertas), y cualquier robot cercano oye a la pulsera. Se cambia a
 * unicast cuando se acuerde la MAC del robot.
 */
#include "band_config.h"
#include "band_radio.h"

#if BAND_USE_REAL_RADIO

#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "band_port.h"
#include "band_tasks.h"

static const uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool s_ready;

/* Corre en la tarea de WiFi: solo entrega el paquete a la cola de la pulsera. */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    (void)info;
    if (data == NULL || len <= 0) return;
    (void)band_on_receive(data, (size_t)len);
}

void port_radio_start(void)
{
    esp_err_t err = nvs_flash_init();                 /* WiFi guarda calibracion en NVS */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) { port_log("[radio] NVS fallo: %s", esp_err_to_name(err)); return; }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(BAND_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    err = esp_now_init();
    if (err != ESP_OK) { port_log("[radio] esp_now_init fallo: %s", esp_err_to_name(err)); return; }
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof peer);
    memcpy(peer.peer_addr, BROADCAST_MAC, ESP_NOW_ETH_ALEN);
    peer.channel = BAND_ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    err = esp_now_add_peer(&peer);
    if (err != ESP_OK) { port_log("[radio] add_peer fallo: %s", esp_err_to_name(err)); return; }

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    port_log("[radio] ESP-NOW listo | canal %d | MAC de la pulsera %02X:%02X:%02X:%02X:%02X:%02X | broadcast",
             BAND_ESPNOW_CHANNEL, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    s_ready = true;
}

bool port_send(const uint8_t *buf, size_t len)
{
    if (!s_ready || buf == NULL || len == 0u || len > ESP_NOW_MAX_DATA_LEN) return false;
    return esp_now_send(BROADCAST_MAC, buf, len) == ESP_OK;
}

#endif /* BAND_USE_REAL_RADIO */

#if !BAND_SIM

#include "esp_err.h"

#include "band_pins.h"
#include "port_i2c.h"

void port_start(void)
{
    const esp_err_t err = port_i2c_init();
    if (err == ESP_OK) {
        port_log("[i2c] bus listo; conecta MPU6050 y MAX30102 en SDA=GPIO%d, SCL=GPIO%d",
                 BAND_PIN_I2C_SDA, BAND_PIN_I2C_SCL);
    } else {
        port_log("[i2c] no se pudo iniciar el bus: %s", esp_err_to_name(err));
    }
    port_radio_start();
    port_log("[bateria] medicion pendiente; port_battery_mv devolvera 0");
}

#endif /* !BAND_SIM */
