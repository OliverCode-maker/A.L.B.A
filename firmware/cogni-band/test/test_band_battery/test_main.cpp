// Pruebas de la conversion voltaje -> % de bateria y del aviso de bateria baja.
// Correr con:  pio test -e native -f test_band_battery
#include <unity.h>

#include "alba_protocol.h"
#include "band_battery.h"

static band_battery_t b;

void setUp(void) { band_battery_init(&b); }
void tearDown(void) {}

void test_no_measurement_is_invalid(void)
{
    TEST_ASSERT_EQUAL_UINT8(ALBA_BAT_INVALID, band_battery_pct_from_mv(0));
}

void test_curve_points_and_limits(void)
{
    TEST_ASSERT_EQUAL_UINT8(0,   band_battery_pct_from_mv(3000));   // por debajo de la curva
    TEST_ASSERT_EQUAL_UINT8(0,   band_battery_pct_from_mv(3300));
    TEST_ASSERT_EQUAL_UINT8(15,  band_battery_pct_from_mv(3600));
    TEST_ASSERT_EQUAL_UINT8(55,  band_battery_pct_from_mv(3800));
    TEST_ASSERT_EQUAL_UINT8(100, band_battery_pct_from_mv(4200));
    TEST_ASSERT_EQUAL_UINT8(100, band_battery_pct_from_mv(4350));   // cargador: nunca mas de 100
}

void test_interpolates_between_points(void)
{
    TEST_ASSERT_EQUAL_UINT8(25, band_battery_pct_from_mv(3650));    // mitad entre 15 y 35
    TEST_ASSERT_EQUAL_UINT8(96, band_battery_pct_from_mv(4150));    // mitad entre 92 y 100
}

void test_curve_is_monotonic_and_never_invalid(void)
{
    uint8_t prev = 0;
    for (unsigned mv = 1; mv <= 4400; mv++) {
        const uint8_t p = band_battery_pct_from_mv((uint16_t)mv);
        TEST_ASSERT_TRUE(p <= 100);                                  // nunca confundible con 0xFF
        TEST_ASSERT_TRUE(p >= prev);
        prev = p;
    }
}

void test_low_alert_fires_once(void)
{
    TEST_ASSERT_FALSE(band_battery_low_alert(&b, 80));
    TEST_ASSERT_FALSE(band_battery_low_alert(&b, 16));
    TEST_ASSERT_TRUE(band_battery_low_alert(&b, 15));               // justo en el umbral
    TEST_ASSERT_FALSE(band_battery_low_alert(&b, 14));
    TEST_ASSERT_FALSE(band_battery_low_alert(&b, 5));
}

void test_hysteresis_rearms_only_after_recharge(void)
{
    TEST_ASSERT_TRUE(band_battery_low_alert(&b, 10));
    TEST_ASSERT_FALSE(band_battery_low_alert(&b, 20));              // oscila: aun no se rearma
    TEST_ASSERT_FALSE(band_battery_low_alert(&b, 12));              // y no avisa de nuevo
    TEST_ASSERT_FALSE(band_battery_low_alert(&b, 25));              // cargada: se rearma
    TEST_ASSERT_TRUE(band_battery_low_alert(&b, 14));               // y puede avisar otra vez
}

void test_invalid_never_alerts_or_rearms(void)
{
    TEST_ASSERT_FALSE(band_battery_low_alert(&b, ALBA_BAT_INVALID));
    TEST_ASSERT_TRUE(band_battery_low_alert(&b, 10));
    TEST_ASSERT_FALSE(band_battery_low_alert(&b, ALBA_BAT_INVALID)); // sin medicion no rearma
    TEST_ASSERT_FALSE(band_battery_low_alert(&b, 10));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_measurement_is_invalid);
    RUN_TEST(test_curve_points_and_limits);
    RUN_TEST(test_interpolates_between_points);
    RUN_TEST(test_curve_is_monotonic_and_never_invalid);
    RUN_TEST(test_low_alert_fires_once);
    RUN_TEST(test_hysteresis_rearms_only_after_recharge);
    RUN_TEST(test_invalid_never_alerts_or_rearms);
    return UNITY_END();
}
