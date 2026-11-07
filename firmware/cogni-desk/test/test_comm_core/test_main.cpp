// Pruebas del cerebro de comunicaciones del robot (comm_core).
// Correr con:  pio test -e native -f test_comm_core
#include <unity.h>
#include <string.h>
#include <stdio.h>

#include "comm_core.h"

static comm_core_t c;
static uint8_t sent[8][32];
static size_t  sent_len[8];
static int     sent_n;
static bool    radio_ok;

static bool fake_send(const uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    if (!radio_ok) return false;
    if (sent_n < 8 && len <= 32) { memcpy(sent[sent_n], buf, len); sent_len[sent_n] = len; sent_n++; }
    return true;
}

void setUp(void) { sent_n = 0; radio_ok = true; comm_core_init(&c, fake_send, NULL); }
void tearDown(void) {}

static const uint64_t EPOCH_MS = 1793016003000ULL;
static const uint32_t EPOCH_S  = 1793016003u;

static void rx_tel(uint16_t seq, uint32_t t, uint8_t bpm, uint8_t spo2, uint8_t bat, uint32_t now)
{
    alba_telemetry_t p; memset(&p, 0, sizeof p);
    alba_pack_telemetry(&p, seq, t, bpm, spo2, bat);
    comm_core_on_packet(&c, (const uint8_t *)&p, sizeof p, now, EPOCH_MS);
}
static void rx_evt(uint16_t seq, uint32_t t, uint8_t alerta, uint16_t v, uint8_t conf, uint32_t now)
{
    alba_event_t p; memset(&p, 0, sizeof p);
    alba_pack_event(&p, seq, t, alerta, v, conf);
    comm_core_on_packet(&c, (const uint8_t *)&p, sizeof p, now, EPOCH_MS);
}

void test_telemetry_goes_to_outbox_in_thingsboard_format(void)
{
    rx_tel(1, 1000, 62, 97, 84, 5000);
    TEST_ASSERT_EQUAL_UINT(1, comm_core_outbox_count(&c));
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1793016003000,\"values\":{\"bpm\":62,\"spo2\":97,\"bat\":84}}",
                             comm_core_outbox_peek(&c));
}

void test_telemetry_updates_ui_values(void)
{
    rx_tel(1, 1000, 70, 95, 50, 5000);
    TEST_ASSERT_TRUE(c.have_vitals);
    TEST_ASSERT_EQUAL_UINT8(70, c.bpm);
    TEST_ASSERT_EQUAL_UINT8(95, c.spo2);
    TEST_ASSERT_EQUAL_UINT8(50, c.bat);
}

void test_duplicate_packet_is_not_published_twice(void)
{
    rx_tel(1, 1000, 62, 97, 84, 5000);
    rx_tel(1, 1000, 62, 97, 84, 5010);
    TEST_ASSERT_EQUAL_UINT(1, comm_core_outbox_count(&c));
}

void test_garbage_packet_is_ignored(void)
{
    const uint8_t junk[5] = {9, 9, 9, 9, 9};
    comm_core_on_packet(&c, junk, sizeof junk, 1000, EPOCH_MS);
    TEST_ASSERT_EQUAL_UINT(0, comm_core_outbox_count(&c));
    TEST_ASSERT_FALSE(c.seen_band);
}

void test_pending_fall_shows_prompt_and_publishes(void)
{
    rx_evt(5, 2000, ALBA_ALERTA_CAIDA_DETECTADA, 4000, ALBA_CONF_PENDIENTE, 10000);
    TEST_ASSERT_TRUE(comm_core_prompt_active(&c));
    TEST_ASSERT_EQUAL_UINT(1, comm_core_outbox_count(&c));
    TEST_ASSERT_NOT_NULL(strstr(comm_core_outbox_peek(&c), "\"confirm\":\"PENDIENTE\""));
}

void test_button_sends_cancel_with_fall_seq(void)
{
    rx_evt(5, 2000, ALBA_ALERTA_CAIDA_DETECTADA, 4000, ALBA_CONF_PENDIENTE, 10000);
    comm_core_button_ok(&c, 12000);
    TEST_ASSERT_EQUAL_INT(1, sent_n);
    TEST_ASSERT_EQUAL_UINT(sizeof(alba_cancel_t), sent_len[0]);
    alba_cancel_t m; memcpy(&m, sent[0], sizeof m);
    TEST_ASSERT_EQUAL_UINT8(ALBA_MSG_CANCELAR, m.hdr.type);
    TEST_ASSERT_EQUAL_UINT16(5, m.ref_seq);
    TEST_ASSERT_FALSE(comm_core_prompt_active(&c));
}

void test_cancel_is_repeated_by_tick(void)
{
    rx_evt(5, 2000, ALBA_ALERTA_CAIDA_DETECTADA, 4000, ALBA_CONF_PENDIENTE, 10000);
    comm_core_button_ok(&c, 12000);
    for (uint32_t t = 12100; t < 14000; t += 100) comm_core_tick(&c, t, 0, -300);
    TEST_ASSERT_EQUAL_INT(1 + (int)ROBOT_CANCEL_REPEATS, sent_n);
}

void test_button_without_fall_sends_nothing(void)
{
    comm_core_button_ok(&c, 1000);
    TEST_ASSERT_EQUAL_INT(0, sent_n);
}

void test_confirmed_fall_sets_help_called_once(void)
{
    rx_evt(5, 2000, ALBA_ALERTA_CAIDA_DETECTADA, 4000, ALBA_CONF_PENDIENTE, 10000);
    rx_evt(6, 22000, ALBA_ALERTA_CAIDA_DETECTADA, 4000, ALBA_CONF_CONFIRMADA, 30000);
    TEST_ASSERT_TRUE(comm_core_take_help_called(&c));
    TEST_ASSERT_FALSE(comm_core_take_help_called(&c));
    TEST_ASSERT_FALSE(comm_core_prompt_active(&c));
}

void test_time_is_sent_only_with_real_clock(void)
{
    comm_core_tick(&c, 1000, 0, -300);                 /* sin hora real */
    TEST_ASSERT_EQUAL_INT(0, sent_n);
    rx_tel(1, 1000, 62, 97, 84, 1000);                 /* aparece la pulsera */
    comm_core_tick(&c, 1100, EPOCH_S, -300);
    TEST_ASSERT_EQUAL_INT(1, sent_n);
    alba_time_t h; memcpy(&h, sent[0], sizeof h);
    TEST_ASSERT_EQUAL_UINT8(ALBA_MSG_HORA, h.hdr.type);
    TEST_ASSERT_EQUAL_UINT32(EPOCH_S, h.epoch_s);
    TEST_ASSERT_EQUAL_INT16(-300, h.utc_offset_min);
}

void test_time_failed_send_is_counted(void)
{
    rx_tel(1, 1000, 62, 97, 84, 1000);
    radio_ok = false;
    comm_core_tick(&c, 1100, EPOCH_S, -300);
    TEST_ASSERT_EQUAL_UINT32(1, c.send_fail);
}

void test_outbox_is_fifo_and_pop_removes_oldest(void)
{
    rx_tel(1, 1000, 60, 97, 80, 1000);
    rx_tel(2, 2000, 61, 97, 80, 2000);
    TEST_ASSERT_NOT_NULL(strstr(comm_core_outbox_peek(&c), "\"bpm\":60"));
    comm_core_outbox_pop(&c);
    TEST_ASSERT_NOT_NULL(strstr(comm_core_outbox_peek(&c), "\"bpm\":61"));
    comm_core_outbox_pop(&c);
    TEST_ASSERT_NULL(comm_core_outbox_peek(&c));
    comm_core_outbox_pop(&c);                          /* vacia: no rompe */
    TEST_ASSERT_EQUAL_UINT(0, comm_core_outbox_count(&c));
}

void test_full_outbox_drops_telemetry_before_alerts(void)
{
    rx_evt(1, 1000, ALBA_ALERTA_BATERIA_BAJA, 15, ALBA_CONF_CONFIRMADA, 1000);   /* alerta al frente */
    for (uint16_t i = 0; i < COMM_OUTBOX_CAP + 4u; i++)
        rx_tel((uint16_t)(2 + i), 2000 + i, 60, 97, 80, 2000 + i);
    TEST_ASSERT_EQUAL_UINT(COMM_OUTBOX_CAP, comm_core_outbox_count(&c));
    TEST_ASSERT_NOT_NULL(strstr(comm_core_outbox_peek(&c), "BATERIA_BAJA"));     /* la alerta sobrevive */
    TEST_ASSERT_EQUAL_UINT32(5, c.out_dropped);   /* 1 alerta + 20 telemetrias = 21 en una cola de 16 */
}

void test_full_outbox_of_alerts_drops_the_oldest_alert(void)
{
    for (uint16_t i = 0; i < COMM_OUTBOX_CAP + 1u; i++)
        rx_evt((uint16_t)(1 + i), 1000u + i, ALBA_ALERTA_BATERIA_BAJA, (uint16_t)(10 + i), ALBA_CONF_CONFIRMADA, 1000u + i);
    TEST_ASSERT_EQUAL_UINT(COMM_OUTBOX_CAP, comm_core_outbox_count(&c));
    TEST_ASSERT_NOT_NULL(strstr(comm_core_outbox_peek(&c), "\"value\":11"));      /* se fue la del 10 */
}

void test_env_formats_temperature_and_humidity(void)
{
    comm_core_push_env(&c, EPOCH_MS, 235, 61);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1793016003000,\"values\":{\"room_temp\":23.5,\"room_hum\":61}}",
                             comm_core_outbox_peek(&c));
}

void test_env_negative_and_partial_values(void)
{
    comm_core_push_env(&c, 0, -25, COMM_ENV_INVALID_HUM);
    TEST_ASSERT_EQUAL_STRING("{\"room_temp\":-2.5}", comm_core_outbox_peek(&c));
    comm_core_outbox_pop(&c);
    comm_core_push_env(&c, 0, COMM_ENV_INVALID_TEMP, 40);
    TEST_ASSERT_EQUAL_STRING("{\"room_hum\":40}", comm_core_outbox_peek(&c));
}

void test_env_with_nothing_valid_is_not_queued(void)
{
    comm_core_push_env(&c, EPOCH_MS, COMM_ENV_INVALID_TEMP, COMM_ENV_INVALID_HUM);
    comm_core_push_env(&c, EPOCH_MS, 200, 101);        /* humedad fuera de rango: solo temperatura */
    TEST_ASSERT_EQUAL_UINT(1, comm_core_outbox_count(&c));
    TEST_ASSERT_NULL(strstr(comm_core_outbox_peek(&c), "room_hum"));
}

void test_band_online_and_offline(void)
{
    TEST_ASSERT_FALSE(comm_core_band_online(&c, 1000));
    rx_tel(1, 1000, 62, 97, 84, 5000);
    TEST_ASSERT_TRUE(comm_core_band_online(&c, 5000 + COMM_BAND_OFFLINE_MS - 1));
    TEST_ASSERT_FALSE(comm_core_band_online(&c, 5000 + COMM_BAND_OFFLINE_MS));
}

void test_push_raw_accepts_json_and_rejects_bad_input(void)
{
    TEST_ASSERT_TRUE(comm_core_push_raw(&c, "{\"med_taken\":true}", true));
    TEST_ASSERT_EQUAL_STRING("{\"med_taken\":true}", comm_core_outbox_peek(&c));
    TEST_ASSERT_FALSE(comm_core_push_raw(&c, NULL, true));
    TEST_ASSERT_FALSE(comm_core_push_raw(&c, "no es json", true));
    char big[COMM_JSON_MAX + 10];
    memset(big, 'a', sizeof big); big[0] = '{'; big[sizeof big - 1] = '\0';
    TEST_ASSERT_FALSE(comm_core_push_raw(&c, big, true));
    TEST_ASSERT_EQUAL_UINT(1, comm_core_outbox_count(&c));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_goes_to_outbox_in_thingsboard_format);
    RUN_TEST(test_telemetry_updates_ui_values);
    RUN_TEST(test_duplicate_packet_is_not_published_twice);
    RUN_TEST(test_garbage_packet_is_ignored);
    RUN_TEST(test_pending_fall_shows_prompt_and_publishes);
    RUN_TEST(test_button_sends_cancel_with_fall_seq);
    RUN_TEST(test_cancel_is_repeated_by_tick);
    RUN_TEST(test_button_without_fall_sends_nothing);
    RUN_TEST(test_confirmed_fall_sets_help_called_once);
    RUN_TEST(test_time_is_sent_only_with_real_clock);
    RUN_TEST(test_time_failed_send_is_counted);
    RUN_TEST(test_outbox_is_fifo_and_pop_removes_oldest);
    RUN_TEST(test_full_outbox_drops_telemetry_before_alerts);
    RUN_TEST(test_full_outbox_of_alerts_drops_the_oldest_alert);
    RUN_TEST(test_env_formats_temperature_and_humidity);
    RUN_TEST(test_env_negative_and_partial_values);
    RUN_TEST(test_env_with_nothing_valid_is_not_queued);
    RUN_TEST(test_band_online_and_offline);
    RUN_TEST(test_push_raw_accepts_json_and_rejects_bad_input);
    return UNITY_END();
}
