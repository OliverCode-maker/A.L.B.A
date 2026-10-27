#include "band_config.h"

#if !BAND_SIM

#include "band_port.h"
#include "band_pins.h"
#include "port_i2c.h"

#include "esp_err.h"

void port_start(void)
{
    const esp_err_t err = port_i2c_init();
    if (err == ESP_OK) {
        port_log("[i2c] bus listo; conecta MPU6050 y MAX30102 en SDA=GPIO%d, SCL=GPIO%d",
                 BAND_PIN_I2C_SDA, BAND_PIN_I2C_SCL);
    } else {
        port_log("[i2c] no se pudo iniciar el bus: %s", esp_err_to_name(err));
    }
    port_log("[radio] ESP-NOW pendiente; port_send no enviara paquetes");
    port_log("[bateria] medicion pendiente; port_battery_mv devolvera 0");
}

bool port_send(const uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return false;
}

#endif