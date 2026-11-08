#include "band_config.h"

#if !BAND_SIM

#include <stdbool.h>

#include "band_port.h"
#include "band_pins.h"
#include "port_battery.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#define PORT_BATTERY_SAMPLE_COUNT 16u

#if BAND_BAT_DIVIDER < 1
#error "BAND_BAT_DIVIDER debe ser un entero positivo"
#endif

#if BAND_PIN_BAT_ADC == 1
#define PORT_BATTERY_ADC_CHANNEL ADC_CHANNEL_0
#else
#error "Mapee BAND_PIN_BAT_ADC al canal ADC1 correspondiente"
#endif

static adc_oneshot_unit_handle_t g_adc_handle;
static adc_cali_handle_t g_adc_cali_handle;
static bool g_battery_ready;

esp_err_t port_battery_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_config, &g_adc_handle);
    if (err != ESP_OK) return err;

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(g_adc_handle, PORT_BATTERY_ADC_CHANNEL,
                                     &channel_config);
    if (err != ESP_OK) goto fail;

    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = ADC_UNIT_1,
        .chan = PORT_BATTERY_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&calibration_config,
                                              &g_adc_cali_handle);
    if (err != ESP_OK) goto fail;

    g_battery_ready = true;
    return ESP_OK;

fail:
    (void)adc_oneshot_del_unit(g_adc_handle);
    g_adc_handle = NULL;
    return err;
}

uint16_t port_battery_mv(void)
{
    if (!g_battery_ready) return 0u;

    uint32_t voltage_sum_mv = 0u;
    for (unsigned i = 0; i < PORT_BATTERY_SAMPLE_COUNT; i++) {
        int raw = 0;
        int pin_mv = 0;
        esp_err_t err = adc_oneshot_read(g_adc_handle, PORT_BATTERY_ADC_CHANNEL,
                                         &raw);
        if (err == ESP_OK)
            err = adc_cali_raw_to_voltage(g_adc_cali_handle, raw, &pin_mv);
        if (err != ESP_OK) {
            port_log("[bateria] fallo al leer/calibrar ADC: %s",
                     esp_err_to_name(err));
            return 0u;
        }
        voltage_sum_mv += (uint32_t)pin_mv;
    }

    const uint32_t pin_mv =
        (voltage_sum_mv + PORT_BATTERY_SAMPLE_COUNT / 2u) /
        PORT_BATTERY_SAMPLE_COUNT;
    const uint32_t battery_mv = pin_mv * BAND_BAT_DIVIDER;
    return battery_mv <= UINT16_MAX ? (uint16_t)battery_mv : 0u;
}

#endif /* !BAND_SIM */
