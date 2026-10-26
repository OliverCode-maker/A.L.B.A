// Pruebas del detector de vigilia con series de FC sinteticas.
// Correr con:  pio test -e native -f test_wake_detector
//
// El simulador respeta el periodo de medicion que pide el propio detector
// (60 s dormido, 15 s en sospecha, 300 s despierto), igual que lo hara la
// tarea de signos vitales en la pulsera.
//
// Estas pruebas verifican la LOGICA. Los umbrales hay que calibrarlos con
// registros nocturnos reales.
#include <unity.h>
#include <stdio.h>
#include <string.h>

#include "wake_detector.h"

static wake_detector_t w;
static uint32_t rng;

// Ruido pseudoaleatorio determinista en [-amp, +amp]
static float noise(float amp)
{
    rng = rng * 1664525u + 1013904223u;
    return ((float)((rng >> 8) & 0xFFFFu) / 65535.0f * 2.0f - 1.0f) * amp;
}

typedef float   (*hr_fn)(uint32_t t);
typedef uint8_t (*act_fn)(uint32_t t);

struct Run {
    int      triggers;
    uint32_t t_first_trigger;
    bool     rising_seen;
    uint16_t period_when_rising;
};

static Run run(hr_fn hr, act_fn act, uint8_t hour, uint32_t t_end, uint8_t sqi = 90)
{
    Run r;
    memset(&r, 0, sizeof r);
    uint32_t t = 0;
    while (t <= t_end) {
        wake_vital_t v;
        v.t_s     = t;
        v.bpm     = (uint8_t)(hr(t) + noise(2.0f) + 0.5f);     // +-2 bpm de ruido
        v.sqi     = sqi;
        v.act_pct = act(t);

        if (wake_detector_update(&w, &v, hour) == WAKE_EVT_TRIGGER) {
            if (r.triggers == 0) r.t_first_trigger = t;
            r.triggers++;
        }
        if (!r.rising_seen && wake_detector_state(&w) == WAKE_RISING) {
            r.rising_seen = true;
            r.period_when_rising = wake_detector_sampling_period_s(&w);
        }
        t += wake_detector_sampling_period_s(&w);
    }
    return r;
}

// ---- perfiles de FC ---------------------------------------------------------
static float    g_base, g_peak;
static uint32_t g_t0, g_t1, g_len;

// Subida lineal de g_base a g_peak entre g_t0 y g_t0+g_len, y se mantiene.
static float hr_ramp(uint32_t t)
{
    if (t < g_t0)         return g_base;
    if (t >= g_t0 + g_len) return g_peak;
    return g_base + (g_peak - g_base) * (float)(t - g_t0) / (float)g_len;
}
static uint8_t act_after_t0(uint32_t t) { return (t >= g_t0) ? 30 : 0; }
static uint8_t act_zero(uint32_t)       { return 0; }

// Un despertar completo: sube en 5 min, se mantiene 15 min, baja en 5 min.
static float one_wake(uint32_t t, uint32_t t0)
{
    if (t < t0)          return 55.0f;
    if (t < t0 + 300)    return 55.0f + 33.0f * (float)(t - t0) / 300.0f;
    if (t < t0 + 1200)   return 88.0f;
    if (t < t0 + 1500)   return 88.0f - 33.0f * (float)(t - (t0 + 1200)) / 300.0f;
    return 55.0f;
}
static float hr_two_wakes(uint32_t t)
{
    const float a = one_wake(t, g_t0), b = one_wake(t, g_t1);
    return a > b ? a : b;
}
static uint8_t act_two_wakes(uint32_t t)
{
    const bool a = (t >= g_t0 && t < g_t0 + 1200);
    const bool b = (t >= g_t1 && t < g_t1 + 1200);
    return (a || b) ? 30 : 0;
}

// Un solo pico de 20 s (pesadilla, tos): no es un despertar.
static float hr_spike(uint32_t t)    { return (t >= 1500 && t < 1520) ? 95.0f : 55.0f; }
static uint8_t act_spike(uint32_t t) { return (t >= 1500 && t < 1520) ? 40 : 0; }

void setUp(void)
{
    wake_detector_init(&w, NULL);
    rng = 777;
    g_base = 55.0f;  g_peak = 88.0f;  g_t0 = 1800;  g_len = 300;  g_t1 = 0;
}
void tearDown(void) {}

// ----------------------------------------------------------------------------

void test_stable_sleep_learns_baseline_and_stays_quiet(void)
{
    g_peak = 55.0f;
    const Run r = run(hr_ramp, act_zero, 7, 3600);
    TEST_ASSERT_EQUAL_INT(0, r.triggers);
    TEST_ASSERT_EQUAL_INT(WAKE_SLEEP, wake_detector_state(&w));
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 55.0f, wake_detector_baseline(&w));
}

void test_real_wake_up_triggers_once(void)
{
    const Run r = run(hr_ramp, act_after_t0, 7, 3600);

    TEST_ASSERT_EQUAL_INT(1, r.triggers);
    TEST_ASSERT_EQUAL_INT(WAKE_AWAKE, wake_detector_state(&w));
    TEST_ASSERT_EQUAL_UINT16(300, wake_detector_sampling_period_s(&w));  // despierto: mide cada 5 min
    TEST_ASSERT_TRUE(r.rising_seen);
    TEST_ASSERT_EQUAL_UINT16(15, r.period_when_rising);                  // en sospecha: mide cada 15 s

    // Nunca antes de que la subida se sostenga 3 min; y dentro de un margen razonable.
    TEST_ASSERT_TRUE(r.t_first_trigger >= g_t0 + 180);
    TEST_ASSERT_TRUE(r.t_first_trigger <= g_t0 + 600);

    char msg[80];
    snprintf(msg, sizeof msg, "disparo %u s despues de iniciar la subida",
             (unsigned)(r.t_first_trigger - g_t0));
    TEST_MESSAGE(msg);
}

void test_transient_spike_is_not_a_wake_up(void)
{
    const Run r = run(hr_spike, act_spike, 7, 3600);
    TEST_ASSERT_EQUAL_INT(0, r.triggers);
    TEST_ASSERT_EQUAL_INT(WAKE_SLEEP, wake_detector_state(&w));
}

void test_high_basal_patient_small_rise_does_not_trigger(void)
{
    g_base = 72.0f;  g_peak = 85.0f;              // +13 bpm: pasa de 80 pero no es +20 sobre su basal
    const Run r = run(hr_ramp, act_after_t0, 7, 3600);
    TEST_ASSERT_EQUAL_INT(0, r.triggers);
}

void test_high_basal_patient_large_rise_triggers(void)
{
    g_base = 72.0f;  g_peak = 98.0f;
    const Run r = run(hr_ramp, act_after_t0, 7, 3600);
    TEST_ASSERT_EQUAL_INT(1, r.triggers);
    TEST_ASSERT_TRUE(r.t_first_trigger >= g_t0 + 180);
    TEST_ASSERT_TRUE(r.t_first_trigger <= g_t0 + 700);
}

void test_outside_morning_window_does_not_trigger(void)
{
    const Run r = run(hr_ramp, act_after_t0, 2, 3600);   // 02:00: ir al bano, no despertar
    TEST_ASSERT_EQUAL_INT(0, r.triggers);
    TEST_ASSERT_FALSE(r.rising_seen);                    // ni siquiera sube la frecuencia de medicion
}

void test_unknown_hour_skips_window_by_default(void)
{
    const Run r = run(hr_ramp, act_after_t0, WAKE_HOUR_UNKNOWN, 3600);   // pulsera sin hora todavia
    TEST_ASSERT_EQUAL_INT(1, r.triggers);
}

void test_unknown_hour_blocks_when_configured(void)
{
    wake_config_t cfg;
    wake_config_default(&cfg);
    cfg.allow_unknown_hour = false;
    wake_detector_init(&w, &cfg);

    const Run r = run(hr_ramp, act_after_t0, WAKE_HOUR_UNKNOWN, 3600);
    TEST_ASSERT_EQUAL_INT(0, r.triggers);
    TEST_ASSERT_FALSE(r.rising_seen);
}

void test_any_hour_above_23_counts_as_unknown(void)
{
    const Run r = run(hr_ramp, act_after_t0, 24, 3600);
    TEST_ASSERT_EQUAL_INT(1, r.triggers);
}

void test_known_hour_edges_of_window(void)
{
    TEST_ASSERT_EQUAL_INT(1, run(hr_ramp, act_after_t0, 4, 3600).triggers);    // primer minuto de la ventana
    wake_detector_init(&w, NULL);  rng = 777;
    TEST_ASSERT_EQUAL_INT(1, run(hr_ramp, act_after_t0, 11, 3600).triggers);   // ultimo
    wake_detector_init(&w, NULL);  rng = 777;
    TEST_ASSERT_EQUAL_INT(0, run(hr_ramp, act_after_t0, 12, 3600).triggers);   // fuera
    wake_detector_init(&w, NULL);  rng = 777;
    TEST_ASSERT_EQUAL_INT(0, run(hr_ramp, act_after_t0, 3, 3600).triggers);
}

void test_no_movement_means_no_trigger_and_sampling_relaxes(void)
{
    const Run r = run(hr_ramp, act_zero, 7, 3600);       // FC sube pero sin actigrafia que lo corrobore
    TEST_ASSERT_EQUAL_INT(0, r.triggers);
    TEST_ASSERT_TRUE(r.rising_seen);
    TEST_ASSERT_EQUAL_INT(WAKE_SLEEP, wake_detector_state(&w));   // vencio el timeout de sospecha
}

void test_bad_quality_readings_are_discarded(void)
{
    for (int i = 0; i < 20; i++) {
        wake_vital_t v = { (uint32_t)(i * 60), 150, 30, 0 };      // BPM absurdo con calidad baja
        TEST_ASSERT_EQUAL_INT(WAKE_EVT_NONE, wake_detector_update(&w, &v, 7));
    }
    TEST_ASSERT_EQUAL_UINT32(20, w.n_rejected);
    TEST_ASSERT_FALSE(w.baseline_ready);
    TEST_ASSERT_EQUAL_INT(WAKE_SLEEP, wake_detector_state(&w));
}

void test_out_of_range_bpm_is_discarded_even_with_good_quality(void)
{
    wake_vital_t v = { 0, 200, 95, 0 };
    wake_detector_update(&w, &v, 7);
    v.bpm = 20;
    wake_detector_update(&w, &v, 7);
    TEST_ASSERT_EQUAL_UINT32(2, w.n_rejected);
}

void test_refractory_blocks_second_wake_within_six_hours(void)
{
    g_t1 = 6000;                                          // segundo despertar ~1 h despues
    const Run r = run(hr_two_wakes, act_two_wakes, 7, 6000 + 1500);
    TEST_ASSERT_EQUAL_INT(1, r.triggers);
}

void test_wake_allowed_again_after_refractory(void)
{
    g_t1 = g_t0 + 7u * 3600u;                             // 7 h despues
    const Run r = run(hr_two_wakes, act_two_wakes, 7, g_t1 + 1500);
    TEST_ASSERT_EQUAL_INT(2, r.triggers);
}

void test_default_sampling_period_is_sleep_period(void)
{
    TEST_ASSERT_EQUAL_UINT16(60, wake_detector_sampling_period_s(&w));
    TEST_ASSERT_EQUAL_INT(WAKE_SLEEP, wake_detector_state(&w));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_stable_sleep_learns_baseline_and_stays_quiet);
    RUN_TEST(test_real_wake_up_triggers_once);
    RUN_TEST(test_transient_spike_is_not_a_wake_up);
    RUN_TEST(test_high_basal_patient_small_rise_does_not_trigger);
    RUN_TEST(test_high_basal_patient_large_rise_triggers);
    RUN_TEST(test_outside_morning_window_does_not_trigger);
    RUN_TEST(test_unknown_hour_skips_window_by_default);
    RUN_TEST(test_unknown_hour_blocks_when_configured);
    RUN_TEST(test_any_hour_above_23_counts_as_unknown);
    RUN_TEST(test_known_hour_edges_of_window);
    RUN_TEST(test_no_movement_means_no_trigger_and_sampling_relaxes);
    RUN_TEST(test_bad_quality_readings_are_discarded);
    RUN_TEST(test_out_of_range_bpm_is_discarded_even_with_good_quality);
    RUN_TEST(test_refractory_blocks_second_wake_within_six_hours);
    RUN_TEST(test_wake_allowed_again_after_refractory);
    RUN_TEST(test_default_sampling_period_is_sleep_period);
    return UNITY_END();
}
