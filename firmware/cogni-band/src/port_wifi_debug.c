#include "band_config.h"

#if !BAND_SIM

#include <stdio.h>
#include <string.h>

#include "band_port.h"
#include "port_wifi_debug.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

enum {
    DEBUG_MESSAGE_SIZE = 192,
    DEBUG_QUEUE_LENGTH = 8,
    DEBUG_TASK_STACK = 3072,
};

static QueueHandle_t debug_queue;
static const char *TAG = "wifi-debug";

static void debug_sender_task(void *argument)
{
    (void)argument;
    const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0) {
        port_log("[%s] no se pudo crear el socket UDP", TAG);
        vTaskDelete(NULL);
        return;
    }

    int broadcast_enabled = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_enabled,
                   sizeof broadcast_enabled) < 0) {
        port_log("[%s] no se pudo habilitar UDP broadcast", TAG);
        close(socket_fd);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in destination;
    memset(&destination, 0, sizeof destination);
    destination.sin_family = AF_INET;
    destination.sin_port = htons(BAND_WIFI_DEBUG_UDP_PORT);
    destination.sin_addr.s_addr = inet_addr("192.168.4.255");

    uint32_t send_errors = 0;
    char message[DEBUG_MESSAGE_SIZE];
    for (;;) {
        if (xQueueReceive(debug_queue, message, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const size_t length = strnlen(message, sizeof message);
        if (sendto(socket_fd, message, length, 0,
                   (struct sockaddr *)&destination, sizeof destination) < 0) {
            send_errors++;
            if (send_errors == 1u || send_errors % 100u == 0u) {
                port_log("[%s] error enviando UDP (%lu errores)",
                         TAG, (unsigned long)send_errors);
            }
        }
    }
}

void port_wifi_debug_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        port_log("[%s] no se pudo inicializar NVS: %s",
                 TAG, esp_err_to_name(err));
        return;
    }

    err = esp_netif_init();
    if (err != ESP_OK) {
        port_log("[%s] no se pudo inicializar la interfaz de red: %s",
                 TAG, esp_err_to_name(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        port_log("[%s] no se pudo crear el event loop: %s",
                 TAG, esp_err_to_name(err));
        return;
    }
    if (esp_netif_create_default_wifi_ap() == NULL) {
        port_log("[%s] no se pudo crear la interfaz del punto de acceso", TAG);
        return;
    }

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        port_log("[%s] no se pudo inicializar Wi-Fi: %s",
                 TAG, esp_err_to_name(err));
        return;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        port_log("[%s] no se pudo configurar almacenamiento Wi-Fi: %s",
                 TAG, esp_err_to_name(err));
        return;
    }
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        port_log("[%s] no se pudo activar modo AP: %s",
                 TAG, esp_err_to_name(err));
        return;
    }

    wifi_config_t ap_config = {0};
    memcpy(ap_config.ap.ssid, BAND_WIFI_DEBUG_SSID,
           sizeof BAND_WIFI_DEBUG_SSID);
    ap_config.ap.ssid_len = (uint8_t)(sizeof BAND_WIFI_DEBUG_SSID - 1u);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 2;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        port_log("[%s] no se pudo configurar el punto de acceso: %s",
                 TAG, esp_err_to_name(err));
        return;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        port_log("[%s] no se pudo iniciar el punto de acceso: %s",
                 TAG, esp_err_to_name(err));
        return;
    }

    debug_queue = xQueueCreate(DEBUG_QUEUE_LENGTH, DEBUG_MESSAGE_SIZE);
    if (debug_queue == NULL) {
        port_log("[%s] no se pudo crear la cola de resultados", TAG);
        return;
    }
    if (xTaskCreate(debug_sender_task, "wifiDebug", DEBUG_TASK_STACK,
                    NULL, 2, NULL) != pdPASS) {
        vQueueDelete(debug_queue);
        debug_queue = NULL;
        port_log("[%s] no se pudo crear la tarea UDP", TAG);
        return;
    }

    port_log("[%s] AP \"%s\" listo; UDP broadcast %u, receptor 192.168.4.255:%u",
             TAG, BAND_WIFI_DEBUG_SSID, BAND_WIFI_DEBUG_UDP_PORT,
             BAND_WIFI_DEBUG_UDP_PORT);
}

bool port_wifi_debug_send(const char *message)
{
    if (message == NULL || debug_queue == NULL) return false;
    char item[DEBUG_MESSAGE_SIZE];
    const int length = snprintf(item, sizeof item, "%s", message);
    if (length < 0) return false;
    if (xQueueSend(debug_queue, item, 0) != pdTRUE) return false;
    return true;
}

#else

#include "port_wifi_debug.h"

void port_wifi_debug_start(void) {}

bool port_wifi_debug_send(const char *message)
{
    (void)message;
    return true;
}

#endif
