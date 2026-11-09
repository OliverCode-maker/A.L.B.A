#include <unity.h>

extern "C" {
#include "band_channel.h"
}

static band_channel_t c;

void setUp(void) { band_channel_init(&c, 1u); }
void tearDown(void) {}

void test_init_clamps_invalid_channel(void)
{
    band_channel_init(&c, 0u);
    TEST_ASSERT_EQUAL_UINT8(1u, band_channel_current(&c));
    band_channel_init(&c, 99u);
    TEST_ASSERT_EQUAL_UINT8(1u, band_channel_current(&c));
    band_channel_init(&c, 6u);
    TEST_ASSERT_EQUAL_UINT8(6u, band_channel_current(&c));
}

void test_single_failure_does_not_trigger_hunt(void)
{
    band_channel_note_result(&c, false);
    TEST_ASSERT_FALSE(band_channel_should_hunt(&c));
    band_channel_note_result(&c, false);
    TEST_ASSERT_TRUE(band_channel_should_hunt(&c));
}

void test_ack_resets_failures(void)
{
    band_channel_note_result(&c, false);
    band_channel_note_result(&c, true);
    band_channel_note_result(&c, false);
    TEST_ASSERT_FALSE(band_channel_should_hunt(&c));
}

void test_scan_visits_every_other_channel_once_and_never_current(void)
{
    band_channel_init(&c, 6u);
    band_channel_note_result(&c, false);
    band_channel_note_result(&c, false);
    band_channel_hunt_begin(&c);
    bool seen[BAND_CH_MAX + 1u] = {false};
    unsigned count = 0u;
    uint8_t ch;
    while ((ch = band_channel_hunt_next(&c)) != 0u) {
        TEST_ASSERT_NOT_EQUAL_UINT8(6u, ch);
        TEST_ASSERT_TRUE(ch >= BAND_CH_MIN && ch <= BAND_CH_MAX);
        TEST_ASSERT_FALSE(seen[ch]);
        seen[ch] = true;
        count++;
    }
    TEST_ASSERT_EQUAL_UINT(BAND_CH_MAX - 1u, count);
}

void test_scan_tries_common_channels_first(void)
{
    band_channel_init(&c, 1u);
    band_channel_note_result(&c, false);
    band_channel_note_result(&c, false);
    band_channel_hunt_begin(&c);
    TEST_ASSERT_EQUAL_UINT8(6u, band_channel_hunt_next(&c));
    TEST_ASSERT_EQUAL_UINT8(11u, band_channel_hunt_next(&c));
}

void test_found_switches_channel_and_clears_failures(void)
{
    band_channel_note_result(&c, false);
    band_channel_note_result(&c, false);
    band_channel_hunt_begin(&c);
    (void)band_channel_hunt_next(&c);
    band_channel_hunt_found(&c, 6u);
    TEST_ASSERT_EQUAL_UINT8(6u, band_channel_current(&c));
    TEST_ASSERT_FALSE(band_channel_should_hunt(&c));
    TEST_ASSERT_EQUAL_UINT32(1u, c.changes_total);
    TEST_ASSERT_EQUAL_UINT32(1u, c.hunts_total);
}

void test_failed_scan_keeps_channel_and_waits_for_new_failures(void)
{
    band_channel_note_result(&c, false);
    band_channel_note_result(&c, false);
    band_channel_hunt_begin(&c);
    while (band_channel_hunt_next(&c) != 0u) {}
    TEST_ASSERT_EQUAL_UINT8(1u, band_channel_current(&c));
    TEST_ASSERT_FALSE(band_channel_should_hunt(&c));
    band_channel_note_result(&c, false);
    band_channel_note_result(&c, false);
    TEST_ASSERT_TRUE(band_channel_should_hunt(&c));
}

void test_no_hunt_request_while_hunting(void)
{
    band_channel_note_result(&c, false);
    band_channel_note_result(&c, false);
    band_channel_hunt_begin(&c);
    band_channel_note_result(&c, false);
    TEST_ASSERT_FALSE(band_channel_should_hunt(&c));
}

void test_next_without_begin_returns_zero(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, band_channel_hunt_next(&c));
}

void test_null_is_safe(void)
{
    band_channel_note_result(nullptr, true);
    band_channel_hunt_begin(nullptr);
    TEST_ASSERT_EQUAL_UINT8(0u, band_channel_hunt_next(nullptr));
    TEST_ASSERT_FALSE(band_channel_should_hunt(nullptr));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_clamps_invalid_channel);
    RUN_TEST(test_single_failure_does_not_trigger_hunt);
    RUN_TEST(test_ack_resets_failures);
    RUN_TEST(test_scan_visits_every_other_channel_once_and_never_current);
    RUN_TEST(test_scan_tries_common_channels_first);
    RUN_TEST(test_found_switches_channel_and_clears_failures);
    RUN_TEST(test_failed_scan_keeps_channel_and_waits_for_new_failures);
    RUN_TEST(test_no_hunt_request_while_hunting);
    RUN_TEST(test_next_without_begin_returns_zero);
    RUN_TEST(test_null_is_safe);
    return UNITY_END();
}
