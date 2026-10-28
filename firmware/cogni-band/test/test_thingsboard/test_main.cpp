// Pruebas del formato de telemetria de ThingsBoard (firmware/common/alba_thingsboard.h).
// Correr con:  pio test -e native -f test_thingsboard
#include <unity.h>
#include <string.h>

#include "alba_thingsboard.h"

void setUp(void) {}
void tearDown(void) {}

static alba_telemetry_t tel(uint8_t bpm, uint8_t spo2, uint8_t bat)
{
    alba_telemetry_t t;
    memset(&t, 0, sizeof t);
    alba_pack_telemetry(&t, 1, 1000, bpm, spo2, bat);
    return t;
}

static alba_event_t evt(uint8_t alerta, uint16_t valor, uint8_t conf)
{
    alba_event_t e;
    memset(&e, 0, sizeof e);
    alba_pack_event(&e, 2, 2000, alerta, valor, conf);
    return e;
}

void test_topic_is_the_thingsboard_telemetry_topic(void)
{
    TEST_ASSERT_EQUAL_STRING("v1/devices/me/telemetry", ALBA_TB_TOPIC);
}

void test_telemetry_exact(void)
{
    char out[128];
    const alba_telemetry_t t = tel(62, 97, 84);
    const int n = alba_telemetry_to_tb(&t, 1793016003000ull, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1793016003000,\"values\":{\"bpm\":62,\"spo2\":97,\"bat\":84}}", out);
    TEST_ASSERT_EQUAL_INT((int)strlen(out), n);
}

void test_telemetry_without_timestamp_omits_ts(void)
{
    char out[128];
    const alba_telemetry_t t = tel(62, 97, 84);
    alba_telemetry_to_tb(&t, 0, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"values\":{\"bpm\":62,\"spo2\":97,\"bat\":84}}", out);
}

void test_telemetry_omits_invalid_keys(void)
{
    char out[128];
    alba_telemetry_t t = tel(ALBA_BPM_INVALID, 97, 84);
    alba_telemetry_to_tb(&t, 5, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":5,\"values\":{\"spo2\":97,\"bat\":84}}", out);

    t = tel(62, ALBA_SPO2_INVALID, ALBA_BAT_INVALID);
    alba_telemetry_to_tb(&t, 5, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":5,\"values\":{\"bpm\":62}}", out);

    t = tel(ALBA_BPM_INVALID, 97, ALBA_BAT_INVALID);
    alba_telemetry_to_tb(&t, 5, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":5,\"values\":{\"spo2\":97}}", out);
}

void test_telemetry_with_nothing_valid_is_not_sent(void)
{
    char out[128] = "basura";
    const alba_telemetry_t t = tel(ALBA_BPM_INVALID, ALBA_SPO2_INVALID, ALBA_BAT_INVALID);
    TEST_ASSERT_EQUAL_INT(-1, alba_telemetry_to_tb(&t, 5, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("", out);                       // ThingsBoard no debe recibir {"values":{}}
}

void test_event_exact(void)
{
    char out[160];
    const alba_event_t e = evt(ALBA_ALERTA_CAIDA_DETECTADA, 3850, ALBA_CONF_CONFIRMADA);
    const int n = alba_event_to_tb(&e, 1793016013270ull, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1793016013270,\"values\":{\"alert\":\"CAIDA_DETECTADA\",\"confirm\":\"CONFIRMADA\",\"value\":3850}}", out);
    TEST_ASSERT_EQUAL_INT((int)strlen(out), n);
}

void test_event_all_alerts_and_states(void)
{
    char out[160];
    alba_event_t e = evt(ALBA_ALERTA_VIGILIA_TRIGGER, 89, ALBA_CONF_CONFIRMADA);
    alba_event_to_tb(&e, 0, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"values\":{\"alert\":\"VIGILIA_TRIGGER\",\"confirm\":\"CONFIRMADA\",\"value\":89}}", out);

    e = evt(ALBA_ALERTA_BATERIA_BAJA, 15, ALBA_CONF_CONFIRMADA);
    alba_event_to_tb(&e, 0, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"alert\":\"BATERIA_BAJA\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"value\":15"));

    e = evt(ALBA_ALERTA_CAIDA_DETECTADA, 4000, ALBA_CONF_PENDIENTE);
    alba_event_to_tb(&e, 0, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"confirm\":\"PENDIENTE\""));

    e = evt(ALBA_ALERTA_CAIDA_DETECTADA, 4000, ALBA_CONF_CANCELADA);
    alba_event_to_tb(&e, 0, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"confirm\":\"CANCELADA\""));
}

void test_event_rejects_unvalidated_values(void)
{
    char out[160] = "basura";
    alba_event_t e = evt(9, 1, ALBA_CONF_CONFIRMADA);                // alerta inexistente
    TEST_ASSERT_EQUAL_INT(-1, alba_event_to_tb(&e, 1, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("", out);
    e = evt(ALBA_ALERTA_CAIDA_DETECTADA, 1, 7);                      // confirmacion inexistente
    TEST_ASSERT_EQUAL_INT(-1, alba_event_to_tb(&e, 1, out, sizeof out));
}

void test_buffer_too_small_fails_cleanly(void)
{
    char out[160];
    const alba_telemetry_t t = tel(62, 97, 84);
    char ok[128];
    const int need = alba_telemetry_to_tb(&t, 1793016003000ull, ok, sizeof ok);
    for (int cap = 1; cap <= need; cap++) {                          // ninguna capacidad menor que la necesaria sirve
        memset(out, 'x', sizeof out);
        TEST_ASSERT_EQUAL_INT(-1, alba_telemetry_to_tb(&t, 1793016003000ull, out, (size_t)cap));
        TEST_ASSERT_EQUAL_CHAR('\0', out[0]);                        // y nunca queda JSON a medias
    }
    TEST_ASSERT_EQUAL_INT(need, alba_telemetry_to_tb(&t, 1793016003000ull, out, (size_t)need + 1));   // justo
}

void test_null_arguments(void)
{
    char out[64];
    const alba_telemetry_t t = tel(62, 97, 84);
    const alba_event_t e = evt(ALBA_ALERTA_CAIDA_DETECTADA, 1, ALBA_CONF_PENDIENTE);
    TEST_ASSERT_EQUAL_INT(-1, alba_telemetry_to_tb(NULL, 1, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, alba_telemetry_to_tb(&t, 1, NULL, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, alba_telemetry_to_tb(&t, 1, out, 0));
    TEST_ASSERT_EQUAL_INT(-1, alba_event_to_tb(NULL, 1, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, alba_event_to_tb(&e, 1, NULL, sizeof out));
}

void test_large_epoch_is_not_truncated(void)
{
    char out[128];
    const alba_telemetry_t t = tel(62, 97, 84);
    alba_telemetry_to_tb(&t, 4102444800000ull, out, sizeof out);     // ano 2100: no cabe en 32 bits
    TEST_ASSERT_NOT_NULL(strstr(out, "\"ts\":4102444800000,"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_topic_is_the_thingsboard_telemetry_topic);
    RUN_TEST(test_telemetry_exact);
    RUN_TEST(test_telemetry_without_timestamp_omits_ts);
    RUN_TEST(test_telemetry_omits_invalid_keys);
    RUN_TEST(test_telemetry_with_nothing_valid_is_not_sent);
    RUN_TEST(test_event_exact);
    RUN_TEST(test_event_all_alerts_and_states);
    RUN_TEST(test_event_rejects_unvalidated_values);
    RUN_TEST(test_buffer_too_small_fails_cleanly);
    RUN_TEST(test_null_arguments);
    RUN_TEST(test_large_epoch_is_not_truncated);
    return UNITY_END();
}
