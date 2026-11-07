#include "band_config.h"

#if !BAND_SIM

#include <string.h>

#include "alba_codec.h"
#include "band_port.h"
#include "band_pins.h"
#include "band_tasks.h"
#include "port_i2c.h"
#include "port_wifi_debug.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#if BAND_COMM_MODE == BAND_COMM_MODE_ESPNOW

static const uint8_t g_peer_mac[ESP_NOW_ETH_ALEN] = BAND_ESPNOW_PEER_MAC;
static bool g_espnow_ready;

static bool check_radio_result(esp_err_t err, const char *operation)
{
    if (err == ESP_OK) return true;
    port_log("[esp-now] %s fallo: %s", operation, esp_err_to_name(err));
    return false;
}

static void espnow_receive_callback(const esp_now_recv_info_t *info,
                                    const uint8_t *data, int data_len)
{
    if (info == NULL || info->src_addr == NULL || data == NULL ||
        data_len <= 0 || memcmp(info->src_addr, g_peer_mac, ESP_NOW_ETH_ALEN) != 0) {
        return;
    }

    (void)band_on_receive(data, (size_t)data_len);
}

static bool espnow_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (!check_radio_result(err, "inicializar NVS")) return false;

    err = esp_netif_init();
    if (!check_radio_result(err, "inicializar interfaces de red")) return false;

    err = esp_event_loop_create_default();
    if (!check_radio_result(err, "crear event loop")) return false;

    if (esp_netif_create_default_wifi_sta() == NULL) {
        port_log("[esp-now] no se pudo crear la interfaz Wi-Fi STA");
        return false;
    }

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (!check_radio_result(err, "inicializar Wi-Fi")) return false;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (!check_radio_result(err, "configurar almacenamiento Wi-Fi")) return false;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (!check_radio_result(err, "activar modo STA")) return false;

    err = esp_wifi_start();
    if (!check_radio_result(err, "iniciar Wi-Fi")) return false;

    err = esp_wifi_set_channel(BAND_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (!check_radio_result(err, "configurar canal")) return false;

    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (!check_radio_result(err, "desactivar ahorro de energia")) return false;

    err = esp_now_init();
    if (!check_radio_result(err, "inicializar ESP-NOW")) return false;

    err = esp_now_register_recv_cb(espnow_receive_callback);
    if (!check_radio_result(err, "registrar callback de recepcion")) return false;

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, g_peer_mac, sizeof peer.peer_addr);
    peer.channel = BAND_ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    err = esp_now_add_peer(&peer);
    if (!check_radio_result(err, "agregar el robot como peer")) return false;

    uint8_t local_mac[ESP_NOW_ETH_ALEN];
    err = esp_wifi_get_mac(WIFI_IF_STA, local_mac);
    if (!check_radio_result(err, "leer MAC local")) return false;

    g_espnow_ready = true;
    port_log("[esp-now] listo; STA=%02X:%02X:%02X:%02X:%02X:%02X, "
             "robot=%02X:%02X:%02X:%02X:%02X:%02X, canal=%u",
             local_mac[0], local_mac[1], local_mac[2],
             local_mac[3], local_mac[4], local_mac[5],
             g_peer_mac[0], g_peer_mac[1], g_peer_mac[2],
             g_peer_mac[3], g_peer_mac[4], g_peer_mac[5],
             (unsigned)BAND_ESPNOW_CHANNEL);
    return true;
}

#endif

void port_start(void)
{
    const esp_err_t err = port_i2c_init();
    if (err == ESP_OK) {
        port_log("[i2c] bus listo; conecta MPU6050 y MAX30102 en SDA=GPIO%d, SCL=GPIO%d",
                 BAND_PIN_I2C_SDA, BAND_PIN_I2C_SCL);
    } else {
        port_log("[i2c] no se pudo iniciar el bus: %s", esp_err_to_name(err));
    }
#if BAND_COMM_MODE == BAND_COMM_MODE_WIFI_DEBUG
    port_wifi_debug_start();
    port_log("[radio] modo Wi-Fi de depuracion; telemetria y alertas por UDP");
#else
    (void)espnow_start();
#endif
    port_log("[bateria] medicion pendiente; port_battery_mv devolvera 0");
}

bool port_send(const uint8_t *buf, size_t len)
{
#if BAND_COMM_MODE == BAND_COMM_MODE_WIFI_DEBUG
    const int type = alba_validate(buf, len);
    char message[192];
    int length = -1;

    if (type == ALBA_MSG_TELEMETRIA) {
        alba_telemetry_t telemetry;
        memcpy(&telemetry, buf, sizeof telemetry);
        length = alba_telemetry_to_json(&telemetry, NULL, message,
                                        sizeof message);
    } else if (type == ALBA_MSG_EVENTO) {
        alba_event_t event;
        memcpy(&event, buf, sizeof event);
        length = alba_event_to_json(&event, NULL, message, sizeof message);
    } else {
        port_log("[wifi-debug] paquete saliente invalido o no compatible (%d)",
                 type);
        return false;
    }

    if (length < 0) {
        port_log("[wifi-debug] no se pudo convertir el paquete a JSON");
        return false;
    }
    return port_wifi_debug_send(message);
#else
    if (!g_espnow_ready) {
        port_log("[esp-now] envio rechazado: radio no inicializada");
        return false;
    }
    if (buf == NULL || len == 0u || len > ALBA_ESPNOW_MAX_PAYLOAD) {
        port_log("[esp-now] envio rechazado: longitud invalida (%u)",
                 (unsigned)len);
        return false;
    }

    const int type = alba_validate(buf, len);
    if (type != ALBA_MSG_TELEMETRIA && type != ALBA_MSG_EVENTO) {
        port_log("[esp-now] envio rechazado: paquete invalido o no compatible (%d)",
                 type);
        return false;
    }

    const esp_err_t err = esp_now_send(g_peer_mac, buf, len);
    if (err != ESP_OK) {
        port_log("[esp-now] esp_now_send fallo: %s", esp_err_to_name(err));
        return false;
    }
    return true;
#endif
}

#endif
