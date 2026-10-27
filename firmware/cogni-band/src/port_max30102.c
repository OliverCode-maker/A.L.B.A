#include "band_config.h"

#if !BAND_SIM

#include "band_port.h"

#include <math.h>

#include "alba_protocol.h"
#include "esp_err.h"
#include "port_i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX30102_ADDRESS          0x57u
#define MAX30102_REG_FIFO_WR_PTR  0x04u
#define MAX30102_REG_OVF_COUNTER  0x05u
#define MAX30102_REG_FIFO_RD_PTR  0x06u
#define MAX30102_REG_FIFO_DATA    0x07u
#define MAX30102_REG_FIFO_CONFIG  0x08u
#define MAX30102_REG_MODE_CONFIG  0x09u
#define MAX30102_REG_SPO2_CONFIG  0x0Au
#define MAX30102_REG_LED1_PA      0x0Cu
#define MAX30102_REG_LED2_PA      0x0Du
#define MAX30102_REG_PART_ID      0xFFu
#define MAX30102_MODE_SHUTDOWN    0x80u
#define MAX30102_MODE_SPO2        0x03u
#define MAX30102_SAMPLE_COUNT     500u
#define MAX30102_MAX_PEAKS        32u
#define MAX30102_FIFO_CAPACITY    32u
#define MAX30102_SAMPLE_PERIOD_MS 50u
#define MAX30102_TIMEOUT_MS       7000u
#define MAX30102_RETRY_MS         5000u

typedef struct {
    uint32_t red;
    uint32_t infrared;
} ppg_sample_t;

static ppg_sample_t g_samples[MAX30102_SAMPLE_COUNT];
static bool g_max_ready;
static bool g_max_attempted;
static uint32_t g_max_last_attempt_ms;

static bool max30102_write(uint8_t reg, uint8_t value)
{
    return port_i2c_write(MAX30102_ADDRESS, reg, value) == ESP_OK;
}

static bool max30102_reset_fifo(void)
{
    return max30102_write(MAX30102_REG_FIFO_WR_PTR, 0u) &&
           max30102_write(MAX30102_REG_OVF_COUNTER, 0u) &&
           max30102_write(MAX30102_REG_FIFO_RD_PTR, 0u);
}

static bool max30102_configure(void)
{
    uint8_t part_id;
    if (port_i2c_read(MAX30102_ADDRESS, MAX30102_REG_PART_ID, &part_id, 1) != ESP_OK ||
        part_id != 0x15u) {
        return false;
    }
    if (!max30102_write(MAX30102_REG_MODE_CONFIG, 0x40u)) return false;
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Single-sample averaging, FIFO rollover, 100 samples/s, 4096 uA range,
     * 411 us pulse width and low LED current to limit power consumption. */
    if (!max30102_write(MAX30102_REG_FIFO_CONFIG, 0x1Fu) ||
        !max30102_write(MAX30102_REG_SPO2_CONFIG, 0x27u) ||
        !max30102_write(MAX30102_REG_LED1_PA, 0x24u) ||
        !max30102_write(MAX30102_REG_LED2_PA, 0x24u) ||
        !max30102_reset_fifo() ||
        !max30102_write(MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SHUTDOWN)) {
        return false;
    }
    return true;
}

static bool max30102_ensure_ready(uint32_t now_ms)
{
    if (g_max_ready) return true;
    if (g_max_attempted &&
        (uint32_t)(now_ms - g_max_last_attempt_ms) < MAX30102_RETRY_MS) {
        return false;
    }

    g_max_attempted = true;
    g_max_last_attempt_ms = now_ms;
    g_max_ready = max30102_configure();
    if (g_max_ready) {
        port_log("[max30102] listo en 0x%02X", (unsigned)MAX30102_ADDRESS);
    } else {
        port_log("[max30102] no se detecta el sensor o fallo su configuracion");
    }
    return g_max_ready;
}

static bool max30102_collect(void)
{
    if (!max30102_reset_fifo() ||
        !max30102_write(MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SPO2)) {
        return false;
    }

    const uint32_t started_ms = port_millis();
    size_t collected = 0;
    while (collected < MAX30102_SAMPLE_COUNT &&
           (uint32_t)(port_millis() - started_ms) < MAX30102_TIMEOUT_MS) {
        uint8_t pointers[3];
        if (port_i2c_read(MAX30102_ADDRESS, MAX30102_REG_FIFO_WR_PTR,
                          pointers, sizeof pointers) != ESP_OK) {
            break;
        }

        const uint8_t write_ptr = pointers[0] & 0x1Fu;
        const uint8_t overflow = pointers[1] & 0x1Fu;
        const uint8_t read_ptr = pointers[2] & 0x1Fu;
        if (overflow != 0u) break;

        const uint8_t available =
            (uint8_t)((write_ptr - read_ptr) & (MAX30102_FIFO_CAPACITY - 1u));
        if (available != 0u) {
            uint8_t raw[MAX30102_FIFO_CAPACITY * 6u];
            const size_t bytes = (size_t)available * 6u;
            if (port_i2c_read(MAX30102_ADDRESS, MAX30102_REG_FIFO_DATA,
                              raw, bytes) != ESP_OK) {
                break;
            }

            for (uint8_t i = 0; i < available && collected < MAX30102_SAMPLE_COUNT; i++) {
                const uint8_t *frame = &raw[(size_t)i * 6u];
                g_samples[collected].red =
                    (((uint32_t)frame[0] << 16) | ((uint32_t)frame[1] << 8) | frame[2]) & 0x3FFFFu;
                g_samples[collected].infrared =
                    (((uint32_t)frame[3] << 16) | ((uint32_t)frame[4] << 8) | frame[5]) & 0x3FFFFu;
                collected++;
            }
        }
        if (collected < MAX30102_SAMPLE_COUNT)
            vTaskDelay(pdMS_TO_TICKS(MAX30102_SAMPLE_PERIOD_MS));
    }

    (void)max30102_write(MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SHUTDOWN);
    return collected == MAX30102_SAMPLE_COUNT;
}

static float signal_mean(const bool infrared)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < MAX30102_SAMPLE_COUNT; i++)
        sum += infrared ? g_samples[i].infrared : g_samples[i].red;
    return (float)sum / (float)MAX30102_SAMPLE_COUNT;
}

static float signal_deviation(const bool infrared, const float mean)
{
    double sum = 0.0;
    for (size_t i = 0; i < MAX30102_SAMPLE_COUNT; i++) {
        const float value = (float)(infrared ? g_samples[i].infrared : g_samples[i].red);
        const float delta = value - mean;
        sum += (double)delta * delta;
    }
    return sqrtf((float)(sum / (double)MAX30102_SAMPLE_COUNT));
}

static uint8_t estimate_pulse(const float mean_ir, const float deviation_ir,
                              uint8_t *sqi)
{
    if (mean_ir < 1000.0f || deviation_ir < mean_ir * 0.001f) return 0u;

    const float threshold = deviation_ir * 0.25f;
    size_t peaks[MAX30102_MAX_PEAKS];
    float peak_strength[MAX30102_MAX_PEAKS];
    size_t peak_count = 0;

    for (size_t i = 2; i + 2 < MAX30102_SAMPLE_COUNT; i++) {
        const uint32_t value = g_samples[i].infrared;
        if (value < g_samples[i - 1u].infrared || value <= g_samples[i + 1u].infrared ||
            value < g_samples[i - 2u].infrared || value <= g_samples[i + 2u].infrared) {
            continue;
        }

        const size_t first = (i > 20u) ? i - 20u : 0u;
        size_t last = i + 20u;
        if (last >= MAX30102_SAMPLE_COUNT) last = MAX30102_SAMPLE_COUNT - 1u;
        uint64_t local_sum = 0;
        for (size_t j = first; j <= last; j++) local_sum += g_samples[j].infrared;
        const float strength = (float)value - (float)local_sum / (float)(last - first + 1u);
        if (strength < threshold) continue;

        if (peak_count != 0u && i - peaks[peak_count - 1u] < 30u) {
            if (strength > peak_strength[peak_count - 1u]) {
                peaks[peak_count - 1u] = i;
                peak_strength[peak_count - 1u] = strength;
            }
        } else if (peak_count < sizeof peaks / sizeof peaks[0]) {
            peaks[peak_count] = i;
            peak_strength[peak_count] = strength;
            peak_count++;
        }
    }

    uint32_t interval_sum = 0;
    uint32_t valid_intervals = 0;
    for (size_t i = 1; i < peak_count; i++) {
        const size_t interval = peaks[i] - peaks[i - 1u];
        if (interval >= 30u && interval <= 200u) {
            interval_sum += (uint32_t)interval;
            valid_intervals++;
        }
    }
    if (valid_intervals == 0u) return 0u;

    const float mean_interval = (float)interval_sum / (float)valid_intervals;
    float deviation = 0.0f;
    for (size_t i = 1; i < peak_count; i++) {
        const size_t interval = peaks[i] - peaks[i - 1u];
        if (interval >= 30u && interval <= 200u)
            deviation += fabsf((float)interval - mean_interval);
    }
    deviation /= (float)valid_intervals;

    float quality = (deviation_ir / mean_ir) * 20000.0f;
    if (quality > 100.0f) quality = 100.0f;
    const float regularity = 100.0f - (deviation / mean_interval) * 200.0f;
    if (quality > regularity) quality = regularity;
    if (quality < 0.0f) quality = 0.0f;
    *sqi = (uint8_t)(quality + 0.5f);

    const float bpm = 6000.0f / mean_interval;
    return (uint8_t)(bpm + 0.5f);
}

bool port_ppg_read(uint8_t *bpm, uint8_t *spo2, uint8_t *sqi)
{
    if (bpm == NULL || spo2 == NULL || sqi == NULL) return false;
    *bpm = 0u;
    *spo2 = ALBA_SPO2_INVALID;
    *sqi = 0u;

    if (!max30102_ensure_ready(port_millis())) return false;
    if (!max30102_collect()) {
        g_max_ready = false;
        port_log("[max30102] error al leer muestras FIFO");
        return false;
    }

    const float mean_ir = signal_mean(true);
    const float mean_red = signal_mean(false);
    const float deviation_ir = signal_deviation(true, mean_ir);
    const float deviation_red = signal_deviation(false, mean_red);
    *bpm = estimate_pulse(mean_ir, deviation_ir, sqi);
    if (*bpm == 0u) {
        port_log("[max30102] sin pulso valido (IR=%.0f, ruido=%.0f)",
                 (double)mean_ir, (double)deviation_ir);
        return false;
    }

    /* Ratio-of-ratios estimate; validate/calibrate against a reference oximeter. */
    if (mean_ir > 0.0f && mean_red > 0.0f && deviation_ir > 0.0f) {
        const float ratio = (deviation_red / mean_red) / (deviation_ir / mean_ir);
        const float estimate = 110.0f - 25.0f * ratio;
        if (ratio >= 0.2f && ratio <= 1.6f && estimate >= 70.0f && estimate <= 100.0f)
            *spo2 = (uint8_t)(estimate + 0.5f);
    }
    port_log("[max30102] BPM=%u SpO2=%u SQI=%u",
             (unsigned)*bpm, (unsigned)*spo2, (unsigned)*sqi);
    return true;
}

#endif /* !BAND_SIM */