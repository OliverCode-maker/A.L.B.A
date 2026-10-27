#include "band_config.h"

#if !BAND_SIM

#include "port_i2c.h"

#include "band_pins.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define PORT_I2C_TIMEOUT_MS 20u

static SemaphoreHandle_t g_i2c_mutex;
static i2c_master_bus_handle_t g_i2c_bus;
static struct {
    uint8_t address;
    i2c_master_dev_handle_t handle;
} g_devices[] = {
    { 0x57u, NULL },
    { 0x68u, NULL },
    { 0x69u, NULL },
};
static volatile int g_init_state;
static esp_err_t g_init_result = ESP_FAIL;

static esp_err_t get_device(uint8_t address, i2c_master_dev_handle_t *handle)
{
    for (size_t i = 0; i < sizeof g_devices / sizeof g_devices[0]; i++) {
        if (g_devices[i].address != address) continue;
        if (g_devices[i].handle == NULL) {
            const i2c_device_config_t config = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = address,
                .scl_speed_hz = BAND_I2C_HZ,
            };
            const esp_err_t err =
                i2c_master_bus_add_device(g_i2c_bus, &config, &g_devices[i].handle);
            if (err != ESP_OK) return err;
        }
        *handle = g_devices[i].handle;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t port_i2c_init(void)
{
    int expected = 0;
    if (__atomic_compare_exchange_n(&g_init_state, &expected, 1, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        g_i2c_mutex = xSemaphoreCreateMutex();
        if (g_i2c_mutex == NULL) {
            g_init_result = ESP_ERR_NO_MEM;
        } else {
            const i2c_master_bus_config_t config = {
                .i2c_port = I2C_NUM_0,
                .sda_io_num = BAND_PIN_I2C_SDA,
                .scl_io_num = BAND_PIN_I2C_SCL,
                .clk_source = I2C_CLK_SRC_DEFAULT,
                .glitch_ignore_cnt = 7u,
                .flags.enable_internal_pullup = true,
            };
            g_init_result = i2c_new_master_bus(&config, &g_i2c_bus);
        }
        __atomic_store_n(&g_init_state, 2, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&g_init_state, __ATOMIC_ACQUIRE) == 1) {
            vTaskDelay(1);
        }
    }
    return g_init_result;
}

esp_err_t port_i2c_read(uint8_t address, uint8_t reg, uint8_t *data, size_t len)
{
    if (data == NULL || len == 0u) return ESP_ERR_INVALID_ARG;

    esp_err_t err = port_i2c_init();
    if (err != ESP_OK) return err;
    if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(PORT_I2C_TIMEOUT_MS)) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    i2c_master_dev_handle_t device;
    err = get_device(address, &device);
    if (err == ESP_OK) {
        err = i2c_master_transmit_receive(device, &reg, 1, data, len,
                                          PORT_I2C_TIMEOUT_MS);
    }
    xSemaphoreGive(g_i2c_mutex);
    return err;
}

esp_err_t port_i2c_write(uint8_t address, uint8_t reg, uint8_t value)
{
    const uint8_t data[] = { reg, value };
    esp_err_t err = port_i2c_init();
    if (err != ESP_OK) return err;
    if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(PORT_I2C_TIMEOUT_MS)) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    i2c_master_dev_handle_t device;
    err = get_device(address, &device);
    if (err == ESP_OK)
        err = i2c_master_transmit(device, data, sizeof data, PORT_I2C_TIMEOUT_MS);
    xSemaphoreGive(g_i2c_mutex);
    return err;
}

#endif /* !BAND_SIM */
