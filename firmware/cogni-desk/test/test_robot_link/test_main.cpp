// Pruebas del enlace del robot con la pulsera: validacion, duplicados, perdidas,
// reinicio de la pulsera y reparto de la HORA.
// Correr con:  pio test -e native -f test_robot_link
#include <unity.h>
#include <string.h>

#include "robot_link.h"

static robot_link_t l;
static robot_rx_msg_t m;

void setUp(void) { robot_link_init(&l); memset(&m, 0, sizeof m); }
void tearDown(void) {}

// ---- ayudas ------------------------------------------------------------------
static robot_rx_result_t rx_tel(uint16_t seq, uint32_t t_ms, uint32_t now_ms)
{
    alba_telemetry_t t;
    alba_pack_telemetry(&t, seq, t_ms, 60, 97, 80);
    return robot_link_on_packet(&l, (const uint8_t *)&t, sizeof t, now_ms, &m);
}

static robot_rx_result_t rx_evt(uint16_t seq, uint32_t t_ms, uint32_t now_ms)
{
    alba_event_t e;
    alba_pack_event(&e, seq, t_ms, ALBA_ALERTA_CAIDA_DETECTADA, 3900, ALBA_CONF_PENDIENTE);
    return robot_link_on_packet(&l, (const uint8_t *)&e, sizeof e, now_ms, &m);
}

// ---- validacion --------------------------------------------------------------
void test_valid_telemetry_and_event_are_decoded(void)
{
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, rx_tel(5, 1000, 1000));
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_TELEMETRIA, m.type);
    TEST_ASSERT_EQUAL_UINT8(60, m.tel.bpm);
    TEST_ASSERT_EQUAL_UINT16(5, m.tel.hdr.seq);

    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, rx_evt(6, 1100, 1100));
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_EVENTO, m.type);
    TEST_ASSERT_EQUAL_UINT16(3900, m.evt.valor_pico);
}

void test_garbage_is_invalid(void)
{
    uint8_t junk[5] = { 1, 2, 3, 4, 5 };
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_INVALID, robot_link_on_packet(&l, junk, sizeof junk, 0, &m));
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_INVALID, robot_link_on_packet(&l, NULL, 0, 0, &m));
    TEST_ASSERT_EQUAL_UINT32(2, l.rx_invalid);
    TEST_ASSERT_FALSE(l.seen_any);                       // basura no cuenta como "la pulsera aparecio"
}

void test_robot_to_band_messages_are_not_valid_here(void)
{
    alba_time_t h;
    alba_pack_time(&h, 1, 1793016003u, -300);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_INVALID, robot_link_on_packet(&l, (const uint8_t *)&h, sizeof h, 0, &m));
    alba_cancel_t c;
    alba_pack_cancel(&c, 1, 7);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_INVALID, robot_link_on_packet(&l, (const uint8_t *)&c, sizeof c, 0, &m));
}

void test_unaligned_buffer_is_ok(void)
{
    alba_event_t e;
    alba_pack_event(&e, 3, 1000, ALBA_ALERTA_CAIDA_DETECTADA, 4000, ALBA_CONF_PENDIENTE);
    uint8_t raw[32];
    memset(raw, 0xAA, sizeof raw);
    memcpy(raw + 1, &e, sizeof e);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, robot_link_on_packet(&l, raw + 1, sizeof e, 0, &m));
    TEST_ASSERT_EQUAL_UINT16(4000, m.evt.valor_pico);
}

// ---- duplicados y perdidas ----------------------------------------------------------
void test_retransmitted_packet_is_duplicate(void)
{
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, rx_evt(10, 5000, 5000));
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_DUPLICATE, rx_evt(10, 5000, 5050));   // la pulsera reintento el envio
    TEST_ASSERT_EQUAL_UINT32(1, l.rx_new);
    TEST_ASSERT_EQUAL_UINT32(1, l.rx_duplicate);
}

void test_gaps_are_counted_as_lost(void)
{
    rx_tel(1, 1000, 1000);
    rx_tel(2, 2000, 2000);
    rx_tel(6, 6000, 6000);                                // faltan 3, 4 y 5
    TEST_ASSERT_EQUAL_UINT32(3, l.rx_lost);
}

void test_late_packet_is_accepted_once_and_fixes_the_loss_count(void)
{
    rx_tel(1, 1000, 1000);
    rx_tel(4, 4000, 4000);                                // faltan 2 y 3
    TEST_ASSERT_EQUAL_UINT32(2, l.rx_lost);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, rx_tel(3, 3900, 4100));         // llego desordenado
    TEST_ASSERT_EQUAL_UINT32(1, l.rx_lost);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_DUPLICATE, rx_tel(3, 3900, 4200));   // y repetido ya no cuenta
    TEST_ASSERT_EQUAL_UINT32(1, l.rx_lost);
}

void test_seq_wraparound_is_handled(void)
{
    rx_tel(65534, 1000, 1000);
    rx_tel(65535, 2000, 2000);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, rx_tel(0, 3000, 3000));         // 65535 -> 0
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, rx_tel(1, 4000, 4000));
    TEST_ASSERT_EQUAL_UINT32(0, l.rx_lost);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_DUPLICATE, rx_tel(65535, 2000, 4100));
    TEST_ASSERT_EQUAL_UINT32(0, l.band_reboots);
}

void test_big_jump_forward_clears_window(void)
{
    rx_tel(1, 1000, 1000);
    rx_tel(200, 2000, 2000);                              // se perdieron muchos
    TEST_ASSERT_EQUAL_UINT32(198, l.rx_lost);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_DUPLICATE, rx_tel(200, 2000, 2100));
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, rx_tel(201, 3000, 3000));
}

void test_very_old_packet_is_not_replayed(void)
{
    rx_tel(100, 1000, 1000);
    rx_tel(200, 2000, 2000);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_DUPLICATE, rx_tel(100, 1000, 2100));  // fuera de la ventana: no se reprocesa
}

// ---- reinicio de la pulsera ---------------------------------------------------------
void test_band_reboot_is_detected_even_with_small_seq(void)
{
    rx_tel(8, 600000, 600000);
    rx_tel(9, 630000, 630000);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, rx_tel(0, 2000, 640000));       // el reloj de la pulsera volvio a ~0
    TEST_ASSERT_EQUAL_UINT32(1, l.band_reboots);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, rx_tel(1, 32000, 670000));      // y su seq 1 NO es duplicado del anterior
    TEST_ASSERT_EQUAL_UINT32(0, l.rx_lost);
}

void test_small_clock_jitter_is_not_a_reboot(void)
{
    rx_tel(1, 60000, 60000);
    rx_evt(2, 59500, 60500);                              // evento "ligeramente antes" que la telemetria
    TEST_ASSERT_EQUAL_UINT32(0, l.band_reboots);
}

void test_clock_wrap_after_49_days_is_not_a_reboot(void)
{
    rx_tel(1, 0xFFFFFF00u, 1000);
    rx_tel(2, 0x00000100u, 2000);                         // el contador de 32 bits dio la vuelta
    TEST_ASSERT_EQUAL_UINT32(0, l.band_reboots);
}

// ---- reparto de la hora --------------------------------------------------------------
void test_no_time_needed_until_band_is_seen(void)
{
    TEST_ASSERT_FALSE(robot_link_need_time(&l, 1000));
}

void test_time_burst_after_first_contact(void)
{
    rx_tel(1, 1000, 1000);
    TEST_ASSERT_TRUE(robot_link_need_time(&l, 1000));     // de inmediato
    robot_link_time_sent(&l, 1000);
    TEST_ASSERT_FALSE(robot_link_need_time(&l, 3000));    // aun no pasaron 5 s
    TEST_ASSERT_TRUE(robot_link_need_time(&l, 6000));
    robot_link_time_sent(&l, 6000);
    TEST_ASSERT_FALSE(robot_link_need_time(&l, 20000));
    TEST_ASSERT_TRUE(robot_link_need_time(&l, 36000));    // 30 s despues del segundo
    robot_link_time_sent(&l, 36000);
    TEST_ASSERT_FALSE(robot_link_need_time(&l, 100000));  // rafaga terminada
}

void test_time_is_resent_every_six_hours(void)
{
    rx_tel(1, 1000, 1000);
    for (int i = 0; i < 3; i++) robot_link_time_sent(&l, 1000);          // rafaga ya enviada
    TEST_ASSERT_FALSE(robot_link_need_time(&l, 1000 + ROBOT_TIME_RESYNC_MS - 1));
    TEST_ASSERT_TRUE(robot_link_need_time(&l, 1000 + ROBOT_TIME_RESYNC_MS));
    robot_link_time_sent(&l, 1000 + ROBOT_TIME_RESYNC_MS);
    TEST_ASSERT_FALSE(robot_link_need_time(&l, 1000 + ROBOT_TIME_RESYNC_MS + 1000));
}

void test_reboot_restarts_the_time_burst(void)
{
    rx_tel(1, 600000, 600000);
    for (int i = 0; i < 3; i++) robot_link_time_sent(&l, 600000);
    TEST_ASSERT_FALSE(robot_link_need_time(&l, 700000));
    rx_tel(0, 1000, 710000);                              // se reinicio y ya no sabe la hora
    TEST_ASSERT_TRUE(robot_link_need_time(&l, 710000));
}

void test_time_clock_wrap_on_robot_side(void)
{
    rx_tel(1, 1000, 0xFFFFFF00u);
    for (int i = 0; i < 3; i++) robot_link_time_sent(&l, 0xFFFFFF00u);
    TEST_ASSERT_FALSE(robot_link_need_time(&l, 0x00001000u));            // el robot lleva 49 dias: no confundir con "pasaron 6 h"
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_telemetry_and_event_are_decoded);
    RUN_TEST(test_garbage_is_invalid);
    RUN_TEST(test_robot_to_band_messages_are_not_valid_here);
    RUN_TEST(test_unaligned_buffer_is_ok);
    RUN_TEST(test_retransmitted_packet_is_duplicate);
    RUN_TEST(test_gaps_are_counted_as_lost);
    RUN_TEST(test_late_packet_is_accepted_once_and_fixes_the_loss_count);
    RUN_TEST(test_seq_wraparound_is_handled);
    RUN_TEST(test_big_jump_forward_clears_window);
    RUN_TEST(test_very_old_packet_is_not_replayed);
    RUN_TEST(test_band_reboot_is_detected_even_with_small_seq);
    RUN_TEST(test_small_clock_jitter_is_not_a_reboot);
    RUN_TEST(test_clock_wrap_after_49_days_is_not_a_reboot);
    RUN_TEST(test_no_time_needed_until_band_is_seen);
    RUN_TEST(test_time_burst_after_first_contact);
    RUN_TEST(test_time_is_resent_every_six_hours);
    RUN_TEST(test_reboot_restarts_the_time_burst);
    RUN_TEST(test_time_clock_wrap_on_robot_side);
    return UNITY_END();
}
