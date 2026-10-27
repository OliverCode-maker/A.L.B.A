// Pruebas del acumulador de actividad entre la tarea de caidas y la de vitales.
// Correr con:  pio test -e native -f test_band_activity
#include <unity.h>

#include "band_activity.h"

static band_activity_t a;

void setUp(void) { band_activity_init(&a); }
void tearDown(void) {}

void test_empty_returns_zero(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, band_activity_take(&a));
}

void test_average_of_samples(void)
{
    band_activity_add(&a, 10);
    band_activity_add(&a, 20);
    band_activity_add(&a, 60);
    TEST_ASSERT_EQUAL_UINT8(30, band_activity_take(&a));
}

void test_take_resets(void)
{
    band_activity_add(&a, 80);
    TEST_ASSERT_EQUAL_UINT8(80, band_activity_take(&a));
    TEST_ASSERT_EQUAL_UINT8(0, band_activity_take(&a));
}

void test_rounds_to_nearest(void)
{
    band_activity_add(&a, 1);
    band_activity_add(&a, 2);                         // 1.5 -> 2
    TEST_ASSERT_EQUAL_UINT8(2, band_activity_take(&a));
    band_activity_add(&a, 1);
    band_activity_add(&a, 1);
    band_activity_add(&a, 2);                         // 1.33 -> 1
    TEST_ASSERT_EQUAL_UINT8(1, band_activity_take(&a));
}

void test_values_above_100_are_clamped(void)
{
    band_activity_add(&a, 250);
    TEST_ASSERT_EQUAL_UINT8(100, band_activity_take(&a));
}

void test_long_accumulation_does_not_overflow(void)
{
    for (int i = 0; i < 300 * 3600; i++) band_activity_add(&a, 100);   // 300 h de muestras a 1 Hz
    TEST_ASSERT_EQUAL_UINT8(100, band_activity_take(&a));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_returns_zero);
    RUN_TEST(test_average_of_samples);
    RUN_TEST(test_take_resets);
    RUN_TEST(test_rounds_to_nearest);
    RUN_TEST(test_values_above_100_are_clamped);
    RUN_TEST(test_long_accumulation_does_not_overflow);
    return UNITY_END();
}
