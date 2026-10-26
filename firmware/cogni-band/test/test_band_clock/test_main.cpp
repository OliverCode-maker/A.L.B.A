// Pruebas del reloj de la pulsera (band_clock): hora local a partir del
// mensaje HORA del robot y el contador de milisegundos.
// Correr con:  pio test -e native -f test_band_clock
#include <unity.h>

#include "band_clock.h"
#include "wake_detector.h"

static band_clock_t clk;

// 2026-10-25 12:00:00 UTC = 07:00 en Ecuador (UTC-5). (1792929600 = 2026-10-25T12:00:00Z)
static const uint32_t NOON_UTC = 1792929600u;

void setUp(void) { band_clock_init(&clk); }
void tearDown(void) {}

void test_unsynced_clock_reports_unknown(void)
{
    TEST_ASSERT_EQUAL_UINT8(WAKE_HOUR_UNKNOWN, band_clock_hour(&clk, 123456));
    TEST_ASSERT_EQUAL_UINT32(0, band_clock_epoch_s(&clk, 123456));
}

void test_local_hour_uses_utc_offset(void)
{
    TEST_ASSERT_TRUE(band_clock_sync(&clk, 5000, NOON_UTC, -300));
    TEST_ASSERT_EQUAL_UINT8(7, band_clock_hour(&clk, 5000));

    band_clock_init(&clk);
    TEST_ASSERT_TRUE(band_clock_sync(&clk, 5000, NOON_UTC, 0));
    TEST_ASSERT_EQUAL_UINT8(12, band_clock_hour(&clk, 5000));

    band_clock_init(&clk);
    TEST_ASSERT_TRUE(band_clock_sync(&clk, 5000, NOON_UTC, 330));       // India +5:30
    TEST_ASSERT_EQUAL_UINT8(17, band_clock_hour(&clk, 5000));
}

void test_hour_wraps_around_midnight_both_directions(void)
{
    // 02:00 UTC con offset -5 h -> 21:00 del dia anterior
    const uint32_t two_utc = NOON_UTC - 10u * 3600u;
    TEST_ASSERT_TRUE(band_clock_sync(&clk, 0, two_utc, -300));
    TEST_ASSERT_EQUAL_UINT8(21, band_clock_hour(&clk, 0));

    // 22:00 UTC con offset +5 h -> 03:00 del dia siguiente
    band_clock_init(&clk);
    TEST_ASSERT_TRUE(band_clock_sync(&clk, 0, NOON_UTC + 10u * 3600u, 300));
    TEST_ASSERT_EQUAL_UINT8(3, band_clock_hour(&clk, 0));
}

void test_clock_advances_with_millis(void)
{
    band_clock_sync(&clk, 1000, NOON_UTC, -300);                          // 07:00:00
    TEST_ASSERT_EQUAL_UINT8(7, band_clock_hour(&clk, 1000 + 59u * 60u * 1000u + 59000u));   // 07:59:59
    TEST_ASSERT_EQUAL_UINT8(8, band_clock_hour(&clk, 1000 + 60u * 60u * 1000u));            // 08:00:00
    TEST_ASSERT_EQUAL_UINT32(NOON_UTC + 90u, band_clock_epoch_s(&clk, 1000 + 90500u));
}

void test_millis_wraparound_between_sync_and_read(void)
{
    const uint32_t t0 = 0xFFFFFFFFu - 30000u;                              // 30 s antes del desborde
    band_clock_sync(&clk, t0, NOON_UTC, -300);
    TEST_ASSERT_EQUAL_UINT32(NOON_UTC + 60u, band_clock_epoch_s(&clk, 30000u));   // 60 s despues, ya desbordado
}

void test_stale_clock_becomes_unknown(void)
{
    band_clock_sync(&clk, 0, NOON_UTC, -300);
    TEST_ASSERT_EQUAL_UINT8(7, band_clock_hour(&clk, BAND_CLOCK_MAX_AGE_MS));          // justo en el limite
    TEST_ASSERT_EQUAL_UINT8(WAKE_HOUR_UNKNOWN, band_clock_hour(&clk, BAND_CLOCK_MAX_AGE_MS + 1u));

    band_clock_sync(&clk, BAND_CLOCK_MAX_AGE_MS + 5000u, NOON_UTC, -300);              // llega hora nueva: vuelve
    TEST_ASSERT_EQUAL_UINT8(7, band_clock_hour(&clk, BAND_CLOCK_MAX_AGE_MS + 5000u));
}

void test_bad_sync_is_rejected_and_keeps_previous_time(void)
{
    band_clock_sync(&clk, 0, NOON_UTC, -300);
    TEST_ASSERT_FALSE(band_clock_sync(&clk, 100, 0u, -300));               // robot sin hora real
    TEST_ASSERT_FALSE(band_clock_sync(&clk, 100, NOON_UTC, 900));          // zona horaria absurda
    TEST_ASSERT_FALSE(band_clock_sync(&clk, 100, NOON_UTC, -800));
    TEST_ASSERT_EQUAL_UINT8(7, band_clock_hour(&clk, 100));

    band_clock_t fresh;
    band_clock_init(&fresh);
    TEST_ASSERT_FALSE(band_clock_sync(&fresh, 0, 0u, 0));
    TEST_ASSERT_EQUAL_UINT8(WAKE_HOUR_UNKNOWN, band_clock_hour(&fresh, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_unsynced_clock_reports_unknown);
    RUN_TEST(test_local_hour_uses_utc_offset);
    RUN_TEST(test_hour_wraps_around_midnight_both_directions);
    RUN_TEST(test_clock_advances_with_millis);
    RUN_TEST(test_millis_wraparound_between_sync_and_read);
    RUN_TEST(test_stale_clock_becomes_unknown);
    RUN_TEST(test_bad_sync_is_rejected_and_keeps_previous_time);
    return UNITY_END();
}
