#include "band_config.h"

#if !BAND_SIM

#include "band_port.h"

#include "esp_err.h"
#include "port_i2c.h"

#define MPU6050_ADDR_LOW       0x68u
#define MPU6050_ADDR_HIGH      0x69u
#define MPU6050_REG_SMPLRT_DIV 0x19u
#define MPU6050_REG_CONFIG     0x1Au
#define MPU6050_REG_GYRO_CFG   0x1Bu
#define MPU6050_REG_ACCEL_CFG  0x1Cu
#define MPU6050_REG_ACCEL_XOUT 0x3Bu
#define MPU6050_REG_PWR_MGMT_1 0x6Bu
#define MPU6050_REG_WHO_AM_I   0x75u
#define MPU6050_RETRY_MS       5000u

static uint8_t g_mpu_address;
static bool g_mpu_ready;
static bool g_mpu_attempted;
static uint32_t g_mpu_last_attempt_ms;
static uint32_t g_mpu_samples;

static int16_t read_i16_be(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static bool mpu6050_configure(uint8_t address)
{
    uint8_t who_am_i;
    if (port_i2c_read(address, MPU6050_REG_WHO_AM_I, &who_am_i, 1) != ESP_OK ||
        (who_am_i & 0x7Eu) != 0x68u) {
        return false;
    }

    /* PLL X-gyro clock, 100 Hz output, +/-16 g and +/-2000 degrees/s. */
    if (port_i2c_write(address, MPU6050_REG_PWR_MGMT_1, 0x01u) != ESP_OK ||
        port_i2c_write(address, MPU6050_REG_CONFIG, 0x03u) != ESP_OK ||
        port_i2c_write(address, MPU6050_REG_SMPLRT_DIV, 9u) != ESP_OK ||
        port_i2c_write(address, MPU6050_REG_GYRO_CFG, 0x18u) != ESP_OK ||
        port_i2c_write(address, MPU6050_REG_ACCEL_CFG, 0x18u) != ESP_OK) {
        return false;
    }

    g_mpu_address = address;
    return true;
}

static bool mpu6050_ensure_ready(uint32_t now_ms)
{
    if (g_mpu_ready) return true;
    if (g_mpu_attempted &&
        (uint32_t)(now_ms - g_mpu_last_attempt_ms) < MPU6050_RETRY_MS) {
        return false;
    }

    g_mpu_attempted = true;
    g_mpu_last_attempt_ms = now_ms;
    g_mpu_ready = mpu6050_configure(MPU6050_ADDR_LOW) ||
                  mpu6050_configure(MPU6050_ADDR_HIGH);
    if (g_mpu_ready) {
        port_log("[mpu6050] listo en 0x%02X", (unsigned)g_mpu_address);
    } else {
        port_log("[mpu6050] no se detecta el sensor en 0x68 ni 0x69");
    }
    return g_mpu_ready;
}

bool port_imu_read(uint32_t now_ms, fall_sample_t *out)
{
    if (out == NULL || !mpu6050_ensure_ready(now_ms)) return false;

    uint8_t data[14];
    if (port_i2c_read(g_mpu_address, MPU6050_REG_ACCEL_XOUT, data, sizeof data) != ESP_OK) {
        g_mpu_ready = false;
        return false;
    }

    out->t_ms = now_ms;
    out->ax = (float)read_i16_be(&data[0]) / 2048.0f;
    out->ay = (float)read_i16_be(&data[2]) / 2048.0f;
    out->az = (float)read_i16_be(&data[4]) / 2048.0f;
    out->gx = (float)read_i16_be(&data[8]) / 16.4f;
    out->gy = (float)read_i16_be(&data[10]) / 16.4f;
    out->gz = (float)read_i16_be(&data[12]) / 16.4f;
    if (++g_mpu_samples % 100u == 0u) {
        port_log("[mpu6050] a=(%.2f,%.2f,%.2f) g, w=(%.1f,%.1f,%.1f) deg/s",
                 (double)out->ax, (double)out->ay, (double)out->az,
                 (double)out->gx, (double)out->gy, (double)out->gz);
    }
    return true;
}

#endif /* !BAND_SIM */