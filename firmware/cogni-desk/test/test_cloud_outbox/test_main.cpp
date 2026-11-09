#include <unity.h>
#include <string.h>

extern "C" {
#include "alba_codec.h"
#include "cloud_outbox.h"
}

static cloud_outbox_t o;

void setUp(void) { cloud_outbox_init(&o); }
void tearDown(void) {}

static alba_telemetry_t tel(uint8_t bpm, uint8_t spo2, uint8_t bat)
{
    alba_telemetry_t t;
    alba_pack_telemetry(&t, 1u, 1000u, bpm, spo2, bat);
    return t;
}

void test_telemetry_without_epoch_is_flat_json(void)
{
    const alba_telemetry_t t = tel(62u, 97u, 84u);
    cloud_outbox_push_telemetry(&o, &t, 0u);
    TEST_ASSERT_EQUAL_STRING("{\"bpm\":62,\"spo2\":97,\"bat\":84}", cloud_outbox_peek(&o));
}

void test_telemetry_with_epoch_has_ts_and_values(void)
{
    const alba_telemetry_t t = tel(62u, 97u, 84u);
    cloud_outbox_push_telemetry(&o, &t, 1794117145646ull);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1794117145646,\"values\":{\"bpm\":62,\"spo2\":97,\"bat\":84}}",
                             cloud_outbox_peek(&o));
}

void test_invalid_vitals_are_not_sent(void)
{
    const alba_telemetry_t t = tel(ALBA_BPM_INVALID, 97u, ALBA_BAT_INVALID);
    cloud_outbox_push_telemetry(&o, &t, 0u);
    TEST_ASSERT_EQUAL_STRING("{\"spo2\":97}", cloud_outbox_peek(&o));

    const alba_telemetry_t none = tel(ALBA_BPM_INVALID, ALBA_SPO2_INVALID, ALBA_BAT_INVALID);
    cloud_outbox_push_telemetry(&o, &none, 0u);
    TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)cloud_outbox_count(&o));     /* no queda nada que mandar */
}

void test_event_json(void)
{
    alba_event_t e;
    alba_pack_event(&e, 5u, 2000u, ALBA_ALERTA_CAIDA_DETECTADA, 4000u, ALBA_CONF_PENDIENTE);
    cloud_outbox_push_event(&o, &e, 0u);
    TEST_ASSERT_EQUAL_STRING("{\"alert\":\"CAIDA_DETECTADA\",\"confirm\":\"PENDIENTE\",\"value\":4000}",
                             cloud_outbox_peek(&o));
}

void test_environment_json_and_invalid_values(void)
{
    cloud_outbox_push_env(&o, 0u, 253, 61u);
    TEST_ASSERT_EQUAL_STRING("{\"room_temp\":25.3,\"room_hum\":61}", cloud_outbox_peek(&o));
    cloud_outbox_pop(&o);

    cloud_outbox_push_env(&o, 0u, -25, CLOUD_ENV_INVALID_HUM);
    TEST_ASSERT_EQUAL_STRING("{\"room_temp\":-2.5}", cloud_outbox_peek(&o));
    cloud_outbox_pop(&o);

    cloud_outbox_push_env(&o, 0u, CLOUD_ENV_INVALID_TEMP, 40u);
    TEST_ASSERT_EQUAL_STRING("{\"room_hum\":40}", cloud_outbox_peek(&o));
    cloud_outbox_pop(&o);

    cloud_outbox_push_env(&o, 0u, CLOUD_ENV_INVALID_TEMP, CLOUD_ENV_INVALID_HUM);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)cloud_outbox_count(&o));
}

void test_comfort_state_is_published_as_room_alert(void)
{
    cloud_outbox_push_comfort(&o, 0u, "TEMP_ALTA+HUM_BAJA");
    TEST_ASSERT_EQUAL_STRING("{\"room_alert\":\"TEMP_ALTA+HUM_BAJA\"}", cloud_outbox_peek(&o));
    cloud_outbox_pop(&o);
    cloud_outbox_push_comfort(&o, 0u, "OK");
    TEST_ASSERT_EQUAL_STRING("{\"room_alert\":\"OK\"}", cloud_outbox_peek(&o));
    cloud_outbox_pop(&o);
}

void test_comfort_state_rejects_unsafe_text(void)
{
    cloud_outbox_push_comfort(&o, 0u, "x\"y");
    cloud_outbox_push_comfort(&o, 0u, "");
    cloud_outbox_push_comfort(&o, 0u, nullptr);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)cloud_outbox_count(&o));
}

void test_question_load_result_is_published(void)
{
    cloud_outbox_push_questions(&o, 0u, 12u, 3u);
    TEST_ASSERT_EQUAL_STRING("{\"q_count\":12,\"q_rejected\":3}", cloud_outbox_peek(&o));
}

void test_game_result_json_rounds_average_to_seconds(void)
{
    cloud_outbox_push_game(&o, 0u, 4u, 5u, 6400u);
    TEST_ASSERT_EQUAL_STRING("{\"game_score\":4,\"game_total\":5,\"game_avg_s\":6}", cloud_outbox_peek(&o));
    cloud_outbox_pop(&o);
    cloud_outbox_push_game(&o, 1794117145646ull, 5u, 5u, 6500u);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1794117145646,\"values\":{\"game_score\":5,\"game_total\":5,\"game_avg_s\":7}}",
                             cloud_outbox_peek(&o));
}

void test_outbox_is_fifo_and_pop_removes_only_the_oldest(void)
{
    const alba_telemetry_t a = tel(60u, 95u, 80u), b = tel(61u, 96u, 79u);
    cloud_outbox_push_telemetry(&o, &a, 0u);
    cloud_outbox_push_telemetry(&o, &b, 0u);
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)cloud_outbox_count(&o));
    TEST_ASSERT_NOT_NULL(strstr(cloud_outbox_peek(&o), "\"bpm\":60"));
    cloud_outbox_pop(&o);
    TEST_ASSERT_NOT_NULL(strstr(cloud_outbox_peek(&o), "\"bpm\":61"));
    cloud_outbox_pop(&o);
    TEST_ASSERT_NULL(cloud_outbox_peek(&o));
    cloud_outbox_pop(&o);                               /* pop en cola vacia no rompe nada */
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)cloud_outbox_count(&o));
}

void test_full_outbox_drops_telemetry_before_alerts(void)
{
    alba_event_t e;
    alba_pack_event(&e, 1u, 1000u, ALBA_ALERTA_CAIDA_DETECTADA, 4000u, ALBA_CONF_CONFIRMADA);
    cloud_outbox_push_event(&o, &e, 0u);                 /* la alerta es la mas antigua */
    for (uint8_t i = 1u; i < CLOUD_OUTBOX_CAP; ++i) {
        const alba_telemetry_t t = tel((uint8_t)(50u + i), 97u, 80u);
        cloud_outbox_push_telemetry(&o, &t, 0u);
    }
    TEST_ASSERT_EQUAL_UINT32(CLOUD_OUTBOX_CAP, (uint32_t)cloud_outbox_count(&o));

    const alba_telemetry_t extra = tel(99u, 97u, 80u);
    cloud_outbox_push_telemetry(&o, &extra, 0u);          /* cola llena: sale la telemetria mas antigua */
    TEST_ASSERT_EQUAL_UINT32(1u, o.dropped);
    TEST_ASSERT_NOT_NULL(strstr(cloud_outbox_peek(&o), "CAIDA_DETECTADA"));   /* la alerta sigue primera */
}

void test_full_outbox_of_only_alerts_drops_the_oldest_alert(void)
{
    for (uint8_t i = 0u; i < CLOUD_OUTBOX_CAP; ++i) {
        alba_event_t e;
        alba_pack_event(&e, i, 1000u, ALBA_ALERTA_CAIDA_DETECTADA, (uint16_t)(1000u + i),
                        ALBA_CONF_PENDIENTE);
        cloud_outbox_push_event(&o, &e, 0u);
    }
    alba_event_t last;
    alba_pack_event(&last, 99u, 1000u, ALBA_ALERTA_CAIDA_DETECTADA, 7777u, ALBA_CONF_CONFIRMADA);
    cloud_outbox_push_event(&o, &last, 0u);
    TEST_ASSERT_EQUAL_UINT32(1u, o.dropped);
    TEST_ASSERT_NULL(strstr(cloud_outbox_peek(&o), "\"value\":1000"));     /* se fue la mas antigua */
}

void test_longest_messages_fit_in_the_buffer(void)
{
    cloud_outbox_push_game(&o, 1794117145646ull, 255u, 255u, 4294967295u);
    TEST_ASSERT_TRUE(strlen(cloud_outbox_peek(&o)) < CLOUD_JSON_MAX);
    TEST_ASSERT_EQUAL_CHAR('}', cloud_outbox_peek(&o)[strlen(cloud_outbox_peek(&o)) - 1u]);
}

static alba_event_t fall(uint16_t seq, alba_confirmacion_t conf)
{
    alba_event_t e;
    alba_pack_event(&e, seq, 5000u, ALBA_ALERTA_CAIDA_DETECTADA, 40u, conf);
    return e;
}

void test_alert_status_none_until_a_confirmed_fall(void)
{
    TEST_ASSERT_EQUAL_INT(CLOUD_ALERT_NONE, cloud_outbox_alert_status(&o));
    const alba_event_t pending = fall(1u, ALBA_CONF_PENDIENTE);
    cloud_outbox_push_event(&o, &pending, 0u);
    TEST_ASSERT_EQUAL_INT(CLOUD_ALERT_NONE, cloud_outbox_alert_status(&o));
}

void test_confirmed_fall_is_pending_then_sent_when_published(void)
{
    const alba_event_t pending = fall(1u, ALBA_CONF_PENDIENTE);
    const alba_event_t confirmed = fall(2u, ALBA_CONF_CONFIRMADA);
    cloud_outbox_push_event(&o, &pending, 0u);
    cloud_outbox_push_event(&o, &confirmed, 0u);
    TEST_ASSERT_EQUAL_INT(CLOUD_ALERT_PENDING, cloud_outbox_alert_status(&o));
    cloud_outbox_pop(&o);   /* sale el PENDIENTE: el aviso confirmado sigue en cola */
    TEST_ASSERT_EQUAL_INT(CLOUD_ALERT_PENDING, cloud_outbox_alert_status(&o));
    cloud_outbox_pop(&o);   /* sale el aviso confirmado */
    TEST_ASSERT_EQUAL_INT(CLOUD_ALERT_SENT, cloud_outbox_alert_status(&o));
}

void test_new_confirmed_fall_after_sent_goes_back_to_pending(void)
{
    const alba_event_t confirmed = fall(2u, ALBA_CONF_CONFIRMADA);
    cloud_outbox_push_event(&o, &confirmed, 0u);
    cloud_outbox_pop(&o);
    TEST_ASSERT_EQUAL_INT(CLOUD_ALERT_SENT, cloud_outbox_alert_status(&o));
    const alba_event_t again = fall(9u, ALBA_CONF_CONFIRMADA);
    cloud_outbox_push_event(&o, &again, 0u);
    TEST_ASSERT_EQUAL_INT(CLOUD_ALERT_PENDING, cloud_outbox_alert_status(&o));
}

void test_other_alerts_do_not_change_the_fall_status(void)
{
    alba_event_t low_battery;
    alba_pack_event(&low_battery, 3u, 1000u, ALBA_ALERTA_BATERIA_BAJA, 10u, ALBA_CONF_CONFIRMADA);
    cloud_outbox_push_event(&o, &low_battery, 0u);
    TEST_ASSERT_EQUAL_INT(CLOUD_ALERT_NONE, cloud_outbox_alert_status(&o));
}

void test_pending_fall_dropped_from_a_full_queue_is_lost(void)
{
    const alba_event_t confirmed = fall(2u, ALBA_CONF_CONFIRMADA);
    cloud_outbox_push_event(&o, &confirmed, 0u);
    for (unsigned i = 0u; i < CLOUD_OUTBOX_CAP; i++) {
        alba_event_t other = fall((uint16_t)(10u + i), ALBA_CONF_PENDIENTE);
        cloud_outbox_push_event(&o, &other, 0u);   /* todo son alertas: se pierde la mas antigua */
    }
    TEST_ASSERT_EQUAL_INT(CLOUD_ALERT_LOST, cloud_outbox_alert_status(&o));
}

void test_alert_show_combines_status_cloud_and_network(void)
{
    TEST_ASSERT_EQUAL_INT(CLOUD_SHOW_CLOUD_OFF, cloud_alert_show(CLOUD_ALERT_SENT, false, true));
    TEST_ASSERT_EQUAL_INT(CLOUD_SHOW_NOT_SENT, cloud_alert_show(CLOUD_ALERT_NONE, true, true));
    TEST_ASSERT_EQUAL_INT(CLOUD_SHOW_NOT_SENT, cloud_alert_show(CLOUD_ALERT_LOST, true, true));
    TEST_ASSERT_EQUAL_INT(CLOUD_SHOW_SENDING, cloud_alert_show(CLOUD_ALERT_PENDING, true, true));
    TEST_ASSERT_EQUAL_INT(CLOUD_SHOW_NO_NETWORK, cloud_alert_show(CLOUD_ALERT_PENDING, true, false));
    TEST_ASSERT_EQUAL_INT(CLOUD_SHOW_SENT, cloud_alert_show(CLOUD_ALERT_SENT, true, false));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_without_epoch_is_flat_json);
    RUN_TEST(test_telemetry_with_epoch_has_ts_and_values);
    RUN_TEST(test_invalid_vitals_are_not_sent);
    RUN_TEST(test_event_json);
    RUN_TEST(test_environment_json_and_invalid_values);
    RUN_TEST(test_game_result_json_rounds_average_to_seconds);
    RUN_TEST(test_outbox_is_fifo_and_pop_removes_only_the_oldest);
    RUN_TEST(test_full_outbox_drops_telemetry_before_alerts);
    RUN_TEST(test_full_outbox_of_only_alerts_drops_the_oldest_alert);
    RUN_TEST(test_longest_messages_fit_in_the_buffer);
    RUN_TEST(test_alert_status_none_until_a_confirmed_fall);
    RUN_TEST(test_confirmed_fall_is_pending_then_sent_when_published);
    RUN_TEST(test_new_confirmed_fall_after_sent_goes_back_to_pending);
    RUN_TEST(test_other_alerts_do_not_change_the_fall_status);
    RUN_TEST(test_pending_fall_dropped_from_a_full_queue_is_lost);
    RUN_TEST(test_alert_show_combines_status_cloud_and_network);
    RUN_TEST(test_comfort_state_is_published_as_room_alert);
    RUN_TEST(test_comfort_state_rejects_unsafe_text);
    RUN_TEST(test_question_load_result_is_published);
    return UNITY_END();
}
