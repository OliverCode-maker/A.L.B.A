#include <unity.h>

extern "C" {
#include "link_stats.h"
}

static link_stats_t s;

void setUp(void) { link_stats_init(&s); }
void tearDown(void) {}

void test_consecutive_packets_no_loss(void)
{
    for (uint16_t i = 10u; i < 15u; i++) link_stats_on_seq(&s, i);
    TEST_ASSERT_EQUAL_UINT32(5u, s.received);
    TEST_ASSERT_EQUAL_UINT32(0u, s.lost);
    TEST_ASSERT_EQUAL_UINT8(0u, link_stats_loss_pct(&s));
}

void test_gap_counts_lost_packets(void)
{
    link_stats_on_seq(&s, 1u);
    link_stats_on_seq(&s, 4u);   /* perdio 2 y 3 */
    TEST_ASSERT_EQUAL_UINT32(2u, s.received);
    TEST_ASSERT_EQUAL_UINT32(2u, s.lost);
    TEST_ASSERT_EQUAL_UINT8(50u, link_stats_loss_pct(&s));
}

void test_first_packet_is_not_a_loss(void)
{
    link_stats_on_seq(&s, 5000u);
    TEST_ASSERT_EQUAL_UINT32(0u, s.lost);
}

void test_duplicate_is_ignored(void)
{
    link_stats_on_seq(&s, 7u);
    link_stats_on_seq(&s, 7u);
    TEST_ASSERT_EQUAL_UINT32(1u, s.received);
}

void test_wraparound_16_bits(void)
{
    link_stats_on_seq(&s, 65534u);
    link_stats_on_seq(&s, 65535u);
    link_stats_on_seq(&s, 0u);
    link_stats_on_seq(&s, 2u);   /* perdio el 1 */
    TEST_ASSERT_EQUAL_UINT32(4u, s.received);
    TEST_ASSERT_EQUAL_UINT32(1u, s.lost);
}

void test_band_restart_is_a_reset_not_a_loss(void)
{
    link_stats_on_seq(&s, 900u);
    link_stats_on_seq(&s, 0u);   /* hacia atras */
    link_stats_on_seq(&s, 1u);
    TEST_ASSERT_EQUAL_UINT32(1u, s.resets);
    TEST_ASSERT_EQUAL_UINT32(0u, s.lost);
    TEST_ASSERT_EQUAL_UINT32(3u, s.received);
}

void test_null_is_safe(void)
{
    link_stats_on_seq(nullptr, 1u);
    TEST_ASSERT_EQUAL_UINT8(0u, link_stats_loss_pct(nullptr));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_consecutive_packets_no_loss);
    RUN_TEST(test_gap_counts_lost_packets);
    RUN_TEST(test_first_packet_is_not_a_loss);
    RUN_TEST(test_duplicate_is_ignored);
    RUN_TEST(test_wraparound_16_bits);
    RUN_TEST(test_band_restart_is_a_reset_not_a_loss);
    RUN_TEST(test_null_is_safe);
    return UNITY_END();
}
