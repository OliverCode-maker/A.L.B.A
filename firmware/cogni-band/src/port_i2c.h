#ifndef PORT_I2C_H
#define PORT_I2C_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t port_i2c_init(void);
esp_err_t port_i2c_read(uint8_t address, uint8_t reg, uint8_t *data, size_t len);
esp_err_t port_i2c_write(uint8_t address, uint8_t reg, uint8_t value);

#endif /* PORT_I2C_H */
