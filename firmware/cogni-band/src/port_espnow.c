#include "band_config.h"

#if !BAND_SIM

#include <string.h>

#include "alba_codec.h"
#include "band_channel.h"
#include "band_port.h"
#include "band_pins.h"
#include "band_tasks.h"
#include "port_battery.h"
#include "port_i2c.h"
#include "port_wifi_debug.h"

#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

#if BAND_COMM_MODE == BAND_COMM_MODE_ESPNOW

static const uint8_t g_peer_mac[ESP_NOW_ETH_ALEN] = BAND_ESPNOW_PEER_MAC;
static bool g_espnow_ready;

/* Canal automatico: el robot sigue el canal del router/hotspot; la pulsera lo busca cuando
 * el robot deja de devolver ACK. Solo tComm llama a port_send(), asi que no hay carreras. */
#define BAND_ACK_WAIT_MS 120u

static band_channel_t    g_chan;
static SemaphoreHandle_t g_send_sem;
static volatile bool     g_send_ok;
static uint32_t          g_diag_last_ms;

static bool check_radio_result(esp_err_t err, const char *operation)
{
    if (err == ESP_OK) return true;
    port_log("[esp-now] %s fallo: %s", operation, esp_err_to_name(err));
    return false;
}

/* Resultado de cada esp_now_send: ESP_NOW_SEND_SUCCESS = el robot devolvio el ACK de la radio. */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void espnow_send_callback(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
#else
static void espnow_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    (void)mac_addr;
#endif
    g_send_ok = (status == ESP_NOW_SEND_SUCCESS);
    if (g_send_sem != NULL) (void)xSemaphoreGive(g_send_sem);
}

/* Envia y espera el resultado. true = el robot confirmo (ACK). */
static bool espnow_send_acked(const uint8_t *buf, size_t len)
{
    (void)xSemaphoreTake(g_send_sem, 0);   /* descarta un resultado viejo */
    g_send_ok = false;
    const esp_err_t err = esp_now_send(g_peer_mac, buf, len);
    if (err != ESP_OK) {
        port_log("[esp-now] esp_now_send fallo: %s", esp_err_to_name(err));
        return false;
    }
    if (xSemaphoreTake(g_send_sem, pdMS_TO_TICKS(BAND_ACK_WAIT_MS)) != pdTRUE) return false;
    return g_send_ok;
}

/* Prueba los demas canales con el mismo paquete hasta que el robot responda. */
static bool espnow_hunt_channel(const uint8_t *buf, size_t len)
{
    const uint8_t previous = band_channel_current(&g_chan);
    port_log("[radio] sin ACK del robot en canal %u; buscando canal...", (unsigned)previous);
    band_channel_hunt_begin(&g_chan);

    uint8_t ch;
    while ((ch = band_channel_hunt_next(&g_chan)) != 0u) {
        if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) continue;
        if (espnow_send_acked(buf, len)) {
            band_channel_hunt_found(&g_chan, ch);
            port_log("[radio] robot encontrado en canal %u (antes %u)", (unsigned)ch, (unsigned)previous);
            return true;
        }
    }
    (void)esp_wifi_set_channel(previous, WIFI_SECOND_CHAN_NONE);
    port_log("[radio] robot no encontrado en ningun canal; sigo en canal %u", (unsigned)previous);
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

    g_send_sem = xSemaphoreCreateBinary();
    if (g_send_sem == NULL) {
        port_log("[esp-now] no se pudo crear el semaforo de envio");
        return false;
    }
    err = esp_now_register_send_cb(espnow_send_callback);
    if (!check_radio_result(err, "registrar callback de envio")) return false;
    band_channel_init(&g_chan, BAND_ESPNOW_CHANNEL);

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, g_peer_mac, sizeof peer.peer_addr);
    peer.channel = 0;   /* 0 = usa el canal actual de la radio (cambia al buscar) */
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
    const esp_err_t battery_err = port_battery_init();
    if (battery_err == ESP_OK) {
        port_log("[bateria] ADC listo en GPIO%d; divisor %u:1",
                 BAND_PIN_BAT_ADC, BAND_BAT_DIVIDER);
    } else {
        port_log("[bateria] no se pudo inicializar el ADC: %s",
                 esp_err_to_name(battery_err));
    }

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

    bool acked = espnow_send_acked(buf, len);
    band_channel_note_result(&g_chan, acked);
    if (!acked && band_channel_should_hunt(&g_chan)) acked = espnow_hunt_channel(buf, len);

    const uint32_t now = port_millis();
    if (now - g_diag_last_ms >= 60000u) {
        g_diag_last_ms = now;
        port_log("[radio] canal=%u ok=%lu sin_ack=%lu busquedas=%lu cambios=%lu",
                 (unsigned)band_channel_current(&g_chan),
                 (unsigned long)g_chan.ok_total, (unsigned long)g_chan.fail_total,
                 (unsigned long)g_chan.hunts_total, (unsigned long)g_chan.changes_total);
    }
    return acked;
#endif
}

#endif
