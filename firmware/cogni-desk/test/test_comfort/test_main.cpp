#include <unity.h>
#include <string.h>

extern "C" {
#include "comfort.h"
}

static comfort_t c;
static const uint32_t HOLD = COMFORT_HOLD_MS;

void setUp(void) { comfort_init(&c, nullptr); }
void tearDown(void) {}

/* Mantiene una lectura durante `ms` milisegundos (una muestra cada 2 s, como el DHT11). */
static uint32_t feed(uint32_t t, uint32_t ms, int16_t temp_x10, uint8_t hum)
{
    const uint32_t end = t + ms;
    for (; t <= end; t += 2000u) comfort_update(&c, t, true, temp_x10, hum);
    return t;
}

void test_comfortable_room_raises_nothing(void)
{
    feed(0u, 120000u, 250, 50);
    TEST_ASSERT_EQUAL_UINT8(0u, comfort_flags(&c));
    uint8_t f;
    TEST_ASSERT_FALSE(comfort_take_change(&c, &f));
    TEST_ASSERT_FALSE(comfort_notify_pending(&c));
}

void test_proposal_limits_are_the_defaults(void)
{
    const comfort_limits_t l = comfort_default_limits();
    TEST_ASSERT_EQUAL_INT16(150, l.temp_min_x10);
    TEST_ASSERT_EQUAL_INT16(350, l.temp_max_x10);
    TEST_ASSERT_EQUAL_UINT8(20u, l.hum_min);
    TEST_ASSERT_EQUAL_UINT8(80u, l.hum_max);
}

void test_classify_boundaries_are_inside_the_range(void)
{
    const comfort_limits_t l = comfort_default_limits();
    TEST_ASSERT_EQUAL_UINT8(0u, comfort_classify(&l, 150, 20u));
    TEST_ASSERT_EQUAL_UINT8(0u, comfort_classify(&l, 350, 80u));
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_LOW, comfort_classify(&l, 149, 50u));
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_HIGH, comfort_classify(&l, 351, 50u));
    TEST_ASSERT_EQUAL_UINT8(COMFORT_HUM_LOW, comfort_classify(&l, 250, 19u));
    TEST_ASSERT_EQUAL_UINT8(COMFORT_HUM_HIGH, comfort_classify(&l, 250, 81u));
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_HIGH | COMFORT_HUM_LOW, comfort_classify(&l, 400, 10u));
}

void test_a_brief_spike_does_not_alert(void)
{
    uint32_t t = feed(0u, 20000u, 250, 50);
    comfort_update(&c, t, true, 380, 50u);          /* una lectura suelta alta */
    comfort_update(&c, t + 2000u, true, 250, 50u);
    feed(t + 4000u, HOLD * 2u, 250, 50);
    TEST_ASSERT_EQUAL_UINT8(0u, comfort_flags(&c));
    TEST_ASSERT_FALSE(comfort_notify_pending(&c));
}

void test_sustained_heat_alerts_after_the_hold_time_only(void)
{
    uint32_t t = 0u;
    comfort_update(&c, t, true, 360, 50u);
    comfort_update(&c, t + HOLD - 1u, true, 360, 50u);
    TEST_ASSERT_EQUAL_UINT8(0u, comfort_flags(&c));
    comfort_update(&c, t + HOLD, true, 360, 50u);
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_HIGH, comfort_flags(&c));
    TEST_ASSERT_TRUE(comfort_notify_pending(&c));
    uint8_t f = 0u;
    TEST_ASSERT_TRUE(comfort_take_change(&c, &f));
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_HIGH, f);
    TEST_ASSERT_FALSE(comfort_take_change(&c, &f));     /* un solo aviso por cambio */
}

void test_cold_dry_and_humid_rooms(void)
{
    uint32_t t = feed(0u, HOLD, 100, 50);
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_LOW, comfort_flags(&c));
    t = feed(t, HOLD * 2u, 250, 10);
    TEST_ASSERT_EQUAL_UINT8(COMFORT_HUM_LOW, comfort_flags(&c));
    feed(t, HOLD * 2u, 250, 90);
    TEST_ASSERT_EQUAL_UINT8(COMFORT_HUM_HIGH, comfort_flags(&c));
}

void test_temperature_and_humidity_can_alert_together(void)
{
    feed(0u, HOLD, 380, 10);
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_HIGH | COMFORT_HUM_LOW, comfort_flags(&c));
}

void test_hysteresis_keeps_the_alert_until_the_value_is_well_inside(void)
{
    uint32_t t = feed(0u, HOLD, 360, 50);
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_HIGH, comfort_flags(&c));
    t = feed(t, HOLD * 2u, 345, 50);          /* bajo de 35.0 pero a menos de 1 C del limite */
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_HIGH, comfort_flags(&c));
    feed(t, HOLD * 2u, 340, 50);              /* 34.0: ya con el margen */
    TEST_ASSERT_EQUAL_UINT8(0u, comfort_flags(&c));
}

void test_return_to_normal_is_reported_once_and_does_not_notify(void)
{
    uint32_t t = feed(0u, HOLD, 360, 50);
    uint8_t f;
    TEST_ASSERT_TRUE(comfort_take_change(&c, &f));
    comfort_take_notification(&c, t);
    t = feed(t, HOLD * 2u, 250, 50);
    TEST_ASSERT_TRUE(comfort_take_change(&c, &f));
    TEST_ASSERT_EQUAL_UINT8(0u, f);
    TEST_ASSERT_FALSE(comfort_notify_pending(&c));
}

void test_notification_waits_until_the_screen_accepts_it(void)
{
    feed(0u, HOLD, 360, 50);
    TEST_ASSERT_TRUE(comfort_notify_pending(&c));
    comfort_update(&c, HOLD + 2000u, true, 360, 50u);
    TEST_ASSERT_TRUE(comfort_notify_pending(&c));      /* sigue pendiente (por ejemplo, hay una partida) */
    comfort_take_notification(&c, HOLD + 3000u);
    TEST_ASSERT_FALSE(comfort_notify_pending(&c));
}

void test_alert_repeats_every_thirty_minutes_while_it_lasts(void)
{
    uint32_t t = feed(0u, HOLD, 360, 50);
    comfort_take_notification(&c, t);
    t = feed(t, COMFORT_RENOTIFY_MS - 4000u, 360, 50);
    TEST_ASSERT_FALSE(comfort_notify_pending(&c));
    feed(t, 10000u, 360, 50);
    TEST_ASSERT_TRUE(comfort_notify_pending(&c));
}

void test_invalid_readings_are_ignored(void)
{
    uint32_t t = feed(0u, HOLD, 360, 50);
    for (uint32_t i = 0u; i < 100u; i++) comfort_update(&c, t + i * 2000u, false, 0, 0u);
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_HIGH, comfort_flags(&c));   /* no se apaga por fallas del sensor */
}

void test_new_condition_while_alerting_notifies_again(void)
{
    uint32_t t = feed(0u, HOLD, 360, 50);
    comfort_take_notification(&c, t);
    feed(t, HOLD * 2u, 360, 10);
    TEST_ASSERT_EQUAL_UINT8(COMFORT_TEMP_HIGH | COMFORT_HUM_LOW, comfort_flags(&c));
    TEST_ASSERT_TRUE(comfort_notify_pending(&c));
}

void test_describe_text_for_the_cloud(void)
{
    char b[48];
    comfort_describe(0u, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("OK", b);
    comfort_describe(COMFORT_TEMP_HIGH, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("TEMP_ALTA", b);
    comfort_describe(COMFORT_TEMP_LOW, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("TEMP_BAJA", b);
    comfort_describe(COMFORT_TEMP_HIGH | COMFORT_HUM_LOW, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("TEMP_ALTA+HUM_BAJA", b);
    comfort_describe(COMFORT_HUM_HIGH, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("HUM_ALTA", b);
    char tiny[4];
    TEST_ASSERT_EQUAL_INT(-1, comfort_describe(COMFORT_TEMP_HIGH, tiny, sizeof tiny));
    TEST_ASSERT_EQUAL_INT(-1, comfort_describe(0u, nullptr, 0u));
}

void test_null_is_safe(void)
{
    comfort_update(nullptr, 0u, true, 250, 50u);
    TEST_ASSERT_EQUAL_UINT8(0u, comfort_flags(nullptr));
    TEST_ASSERT_FALSE(comfort_notify_pending(nullptr));
    TEST_ASSERT_FALSE(comfort_take_change(nullptr, nullptr));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_comfortable_room_raises_nothing);
    RUN_TEST(test_proposal_limits_are_the_defaults);
    RUN_TEST(test_classify_boundaries_are_inside_the_range);
    RUN_TEST(test_a_brief_spike_does_not_alert);
    RUN_TEST(test_sustained_heat_alerts_after_the_hold_time_only);
    RUN_TEST(test_cold_dry_and_humid_rooms);
    RUN_TEST(test_temperature_and_humidity_can_alert_together);
    RUN_TEST(test_hysteresis_keeps_the_alert_until_the_value_is_well_inside);
    RUN_TEST(test_return_to_normal_is_reported_once_and_does_not_notify);
    RUN_TEST(test_notification_waits_until_the_screen_accepts_it);
    RUN_TEST(test_alert_repeats_every_thirty_minutes_while_it_lasts);
    RUN_TEST(test_invalid_readings_are_ignored);
    RUN_TEST(test_new_condition_while_alerting_notifies_again);
    RUN_TEST(test_describe_text_for_the_cloud);
    RUN_TEST(test_null_is_safe);
    return UNITY_END();
}
