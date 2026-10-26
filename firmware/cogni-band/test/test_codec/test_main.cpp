// Pruebas del codec compartido (firmware/common/alba_codec.h):
// empaquetado (pulsera), validacion y JSON (robot).
// Correr con:  pio test -e native -f test_codec
#include <unity.h>
#include <string.h>

#include "alba_codec.h"

void setUp(void) {}
void tearDown(void) {}

// ---- ayudas ----------------------------------------------------------------
static alba_telemetry_t make_tel(void)
{
    alba_telemetry_t t;
    memset(&t, 0, sizeof t);
    alba_pack_telemetry(&t, 123, 3600000u, 62, 97, 84);
    return t;
}

static alba_event_t make_evt(void)
{
    alba_event_t e;
    memset(&e, 0, sizeof e);
    alba_pack_event(&e, 124, 3601234u, ALBA_ALERTA_CAIDA_DETECTADA, 3850, ALBA_CONF_PENDIENTE);
    return e;
}

static int validate_tel(const alba_telemetry_t &t) { return alba_validate((const uint8_t *)&t, sizeof t); }
static int validate_evt(const alba_event_t &e)     { return alba_validate((const uint8_t *)&e, sizeof e); }

// ---- empaquetado -----------------------------------------------------------
void test_pack_telemetry_fields(void)
{
    const alba_telemetry_t t = make_tel();
    TEST_ASSERT_EQUAL_UINT8(ALBA_PROTO_VERSION, t.hdr.version);
    TEST_ASSERT_EQUAL_UINT8(ALBA_MSG_TELEMETRIA, t.hdr.type);
    TEST_ASSERT_EQUAL_UINT16(123, t.hdr.seq);
    TEST_ASSERT_EQUAL_UINT32(3600000u, t.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT8(62, t.bpm);
    TEST_ASSERT_EQUAL_UINT8(97, t.spo2);
    TEST_ASSERT_EQUAL_UINT8(84, t.bateria_pct);
}

void test_pack_telemetry_clamps_battery(void)
{
    alba_telemetry_t t;
    alba_pack_telemetry(&t, 1, 0, 60, 97, 250);       // lectura de ADC fuera de rango
    TEST_ASSERT_EQUAL_UINT8(100, t.bateria_pct);
}

void test_pack_event_fields(void)
{
    const alba_event_t e = make_evt();
    TEST_ASSERT_EQUAL_UINT8(ALBA_MSG_EVENTO, e.hdr.type);
    TEST_ASSERT_EQUAL_UINT16(124, e.hdr.seq);
    TEST_ASSERT_EQUAL_UINT8(ALBA_ALERTA_CAIDA_DETECTADA, e.alerta_tipo);
    TEST_ASSERT_EQUAL_UINT16(3850, e.valor_pico);
    TEST_ASSERT_EQUAL_UINT8(ALBA_CONF_PENDIENTE, e.confirmacion);
}

// ---- validacion (lo que hace el robot con lo que llega por el aire) --------
void test_validate_accepts_good_messages(void)
{
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_TELEMETRIA, validate_tel(make_tel()));
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_EVENTO, validate_evt(make_evt()));
}

void test_validate_rejects_bad_lengths(void)
{
    const alba_telemetry_t t = make_tel();
    const alba_event_t e = make_evt();
    uint8_t big[32];
    memset(big, 0, sizeof big);
    memcpy(big, &e, sizeof e);

    TEST_ASSERT_EQUAL_INT(ALBA_ERR_LEN, alba_validate(NULL, 12));
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_LEN, alba_validate((const uint8_t *)&t, 3));                 // sin cabecera
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_LEN, alba_validate((const uint8_t *)&t, sizeof t - 1));      // truncado
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_LEN, alba_validate(big, sizeof e + 1));                      // sobra un byte
}

void test_validate_rejects_wrong_version_and_type(void)
{
    alba_event_t e = make_evt();
    e.hdr.version = ALBA_PROTO_VERSION + 1;                       // el robot es mas viejo o mas nuevo
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_VERSION, validate_evt(e));

    e = make_evt();
    e.hdr.type = 9;
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_TYPE, validate_evt(e));
}

void test_validate_rejects_out_of_range_fields(void)
{
    alba_event_t e = make_evt();
    e.alerta_tipo = 0;                       TEST_ASSERT_EQUAL_INT(ALBA_ERR_FIELD, validate_evt(e));
    e.alerta_tipo = ALBA_ALERTA_BATERIA_BAJA + 1;
                                             TEST_ASSERT_EQUAL_INT(ALBA_ERR_FIELD, validate_evt(e));
    e = make_evt();
    e.confirmacion = ALBA_CONF_CANCELADA + 1;
                                             TEST_ASSERT_EQUAL_INT(ALBA_ERR_FIELD, validate_evt(e));

    alba_telemetry_t t = make_tel();
    t.spo2 = 101;                            TEST_ASSERT_EQUAL_INT(ALBA_ERR_FIELD, validate_tel(t));
    t = make_tel();
    t.bateria_pct = 101;                     TEST_ASSERT_EQUAL_INT(ALBA_ERR_FIELD, validate_tel(t));
}

void test_validate_accepts_sensor_invalid_markers(void)
{
    alba_telemetry_t t = make_tel();
    t.bpm  = ALBA_BPM_INVALID;
    t.spo2 = ALBA_SPO2_INVALID;              // "sin medicion" es un valor legitimo, no un error
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_TELEMETRIA, validate_tel(t));
}

void test_validate_works_on_unaligned_buffer(void)
{
    const alba_event_t e = make_evt();
    uint8_t raw[32];
    memset(raw, 0xAA, sizeof raw);
    memcpy(raw + 1, &e, sizeof e);           // empieza en direccion impar, como puede llegar de la radio
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_EVENTO, alba_validate(raw + 1, sizeof e));
}

// ---- mensajes robot -> pulsera ---------------------------------------------
void test_pack_time_and_cancel_fields(void)
{
    alba_time_t h;
    memset(&h, 0, sizeof h);
    alba_pack_time(&h, 5, 1792000000u, -300);
    TEST_ASSERT_EQUAL_UINT8(ALBA_MSG_HORA, h.hdr.type);
    TEST_ASSERT_EQUAL_UINT8(ALBA_PROTO_VERSION, h.hdr.version);
    TEST_ASSERT_EQUAL_UINT16(5, h.hdr.seq);
    TEST_ASSERT_EQUAL_UINT32(1792000000u, h.epoch_s);
    TEST_ASSERT_EQUAL_INT16(-300, h.utc_offset_min);

    alba_cancel_t c;
    memset(&c, 0, sizeof c);
    alba_pack_cancel(&c, 6, 124);
    TEST_ASSERT_EQUAL_UINT8(ALBA_MSG_CANCELAR, c.hdr.type);
    TEST_ASSERT_EQUAL_UINT16(6, c.hdr.seq);
    TEST_ASSERT_EQUAL_UINT16(124, c.ref_seq);
}

void test_validate_time_message(void)
{
    alba_time_t h;
    alba_pack_time(&h, 1, 1792000000u, -300);
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_HORA, alba_validate((const uint8_t *)&h, sizeof h));

    alba_pack_time(&h, 1, 0u, -300);                       // robot sin hora real
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_FIELD, alba_validate((const uint8_t *)&h, sizeof h));
    alba_pack_time(&h, 1, ALBA_EPOCH_MIN_S - 1u, -300);
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_FIELD, alba_validate((const uint8_t *)&h, sizeof h));
    alba_pack_time(&h, 1, ALBA_EPOCH_MIN_S, -300);         // justo el minimo: valido
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_HORA, alba_validate((const uint8_t *)&h, sizeof h));

    alba_pack_time(&h, 1, 1792000000u, -721);
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_FIELD, alba_validate((const uint8_t *)&h, sizeof h));
    alba_pack_time(&h, 1, 1792000000u, 841);
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_FIELD, alba_validate((const uint8_t *)&h, sizeof h));
    alba_pack_time(&h, 1, 1792000000u, 840);
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_HORA, alba_validate((const uint8_t *)&h, sizeof h));
    alba_pack_time(&h, 1, 1792000000u, -720);
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_HORA, alba_validate((const uint8_t *)&h, sizeof h));

    alba_pack_time(&h, 1, 1792000000u, -300);
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_LEN, alba_validate((const uint8_t *)&h, sizeof h - 1));
}

void test_validate_cancel_message(void)
{
    alba_cancel_t c;
    alba_pack_cancel(&c, 1, 124);
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_CANCELAR, alba_validate((const uint8_t *)&c, sizeof c));
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_LEN, alba_validate((const uint8_t *)&c, sizeof c - 1));
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_LEN, alba_validate((const uint8_t *)&c, sizeof c + 1));
}

void test_unknown_battery_marker(void)
{
    alba_telemetry_t t;
    alba_pack_telemetry(&t, 1, 1000, 62, 97, ALBA_BAT_INVALID);
    TEST_ASSERT_EQUAL_UINT8(ALBA_BAT_INVALID, t.bateria_pct);      // no se confunde con "demasiado alta"
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_TELEMETRIA, validate_tel(t));

    char json[160];
    TEST_ASSERT_TRUE(alba_telemetry_to_json(&t, NULL, json, sizeof json) > 0);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"bat\":null"));

    alba_pack_telemetry(&t, 1, 1000, 62, 97, 254);                 // cualquier otro valor alto se limita
    TEST_ASSERT_EQUAL_UINT8(100, t.bateria_pct);

    t.bateria_pct = 200;                                            // llegado por el aire: sigue siendo invalido
    TEST_ASSERT_EQUAL_INT(ALBA_ERR_FIELD, validate_tel(t));
}

// ---- JSON -------------------------------------------------------------------
void test_telemetry_json_exact(void)
{
    char out[200];
    const alba_telemetry_t t = make_tel();
    const int n = alba_telemetry_to_json(&t, NULL, out, sizeof out);
    const char *expected =
        "{\"v\":1,\"dev\":\"cogni-band\",\"type\":\"telemetry\",\"seq\":123,"
        "\"t_ms\":3600000,\"bpm\":62,\"spo2\":97,\"bat\":84}";
    TEST_ASSERT_EQUAL_STRING(expected, out);
    TEST_ASSERT_EQUAL_INT((int)strlen(expected), n);
}

void test_telemetry_json_with_timestamp(void)
{
    char out[240];
    const alba_telemetry_t t = make_tel();
    alba_telemetry_to_json(&t, "2026-10-25T07:15:00-05:00", out, sizeof out);
    const char *expected =
        "{\"v\":1,\"dev\":\"cogni-band\",\"type\":\"telemetry\",\"seq\":123,"
        "\"t_ms\":3600000,\"bpm\":62,\"spo2\":97,\"bat\":84,\"ts\":\"2026-10-25T07:15:00-05:00\"}";
    TEST_ASSERT_EQUAL_STRING(expected, out);
}

void test_telemetry_json_invalid_readings_become_null(void)
{
    char out[200];
    alba_telemetry_t t = make_tel();
    t.bpm = ALBA_BPM_INVALID;
    t.spo2 = ALBA_SPO2_INVALID;
    alba_telemetry_to_json(&t, NULL, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"bpm\":null,\"spo2\":null"));
}

void test_event_json_exact(void)
{
    char out[240];
    const alba_event_t e = make_evt();
    const int n = alba_event_to_json(&e, NULL, out, sizeof out);
    const char *expected =
        "{\"v\":1,\"dev\":\"cogni-band\",\"type\":\"event\",\"seq\":124,\"t_ms\":3601234,"
        "\"alert\":\"CAIDA_DETECTADA\",\"peak\":3850,\"confirm\":\"PENDIENTE\"}";
    TEST_ASSERT_EQUAL_STRING(expected, out);
    TEST_ASSERT_EQUAL_INT((int)strlen(expected), n);
}

void test_event_json_all_names(void)
{
    char out[240];
    alba_event_t e = make_evt();
    e.alerta_tipo = ALBA_ALERTA_VIGILIA_TRIGGER;  e.confirmacion = ALBA_CONF_CONFIRMADA;
    alba_event_to_json(&e, NULL, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"alert\":\"VIGILIA_TRIGGER\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"confirm\":\"CONFIRMADA\""));

    e.alerta_tipo = ALBA_ALERTA_BATERIA_BAJA;  e.confirmacion = ALBA_CONF_CANCELADA;
    alba_event_to_json(&e, NULL, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"alert\":\"BATERIA_BAJA\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"confirm\":\"CANCELADA\""));
}

void test_json_buffer_too_small_fails_cleanly(void)
{
    char tiny[20];
    const alba_event_t e = make_evt();
    memset(tiny, 'X', sizeof tiny);
    TEST_ASSERT_EQUAL_INT(-1, alba_event_to_json(&e, NULL, tiny, sizeof tiny));
    TEST_ASSERT_EQUAL_CHAR('\0', tiny[0]);                        // nunca deja un JSON a medias
    TEST_ASSERT_EQUAL_INT(-1, alba_event_to_json(&e, NULL, tiny, 0));
    TEST_ASSERT_EQUAL_INT(-1, alba_event_to_json(NULL, NULL, tiny, sizeof tiny));
    TEST_ASSERT_EQUAL_INT(-1, alba_event_to_json(&e, NULL, NULL, 100));
}

void test_json_exact_fit_boundary(void)
{
    char out[240];
    const alba_event_t e = make_evt();
    const int n = alba_event_to_json(&e, NULL, out, sizeof out);
    char exact[240];
    TEST_ASSERT_EQUAL_INT(n, alba_event_to_json(&e, NULL, exact, (size_t)n + 1));   // cabe justo
    TEST_ASSERT_EQUAL_INT(-1, alba_event_to_json(&e, NULL, exact, (size_t)n));      // falta el '\0'
}

void test_unsafe_timestamp_is_omitted(void)
{
    char with_ts[240], without[240];
    const alba_event_t e = make_evt();
    alba_event_to_json(&e, NULL, without, sizeof without);

    const char *bad[] = { "2026\"}", "a\\b", "line\nbreak", "", "x234567890123456789012345678901234567890123" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        alba_event_to_json(&e, bad[i], with_ts, sizeof with_ts);
        TEST_ASSERT_EQUAL_STRING(without, with_ts);               // el JSON no se rompe ni se inyecta nada
    }
}

void test_event_json_rejects_unvalidated_values(void)
{
    char out[240];
    alba_event_t e = make_evt();
    e.alerta_tipo = 77;
    TEST_ASSERT_EQUAL_INT(-1, alba_event_to_json(&e, NULL, out, sizeof out));
}

void test_round_trip_pack_validate_json(void)
{
    // Lo que hace la pulsera...
    alba_event_t sent;
    alba_pack_event(&sent, 9, 5000u, ALBA_ALERTA_VIGILIA_TRIGGER, 88, ALBA_CONF_CONFIRMADA);
    uint8_t air[ALBA_ESPNOW_MAX_PAYLOAD];
    memcpy(air, &sent, sizeof sent);

    // ...y lo que hace el robot.
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_EVENTO, alba_validate(air, sizeof sent));
    alba_event_t got;
    memcpy(&got, air, sizeof got);
    char out[240];
    TEST_ASSERT_TRUE(alba_event_to_json(&got, NULL, out, sizeof out) > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"alert\":\"VIGILIA_TRIGGER\",\"peak\":88"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_pack_telemetry_fields);
    RUN_TEST(test_pack_telemetry_clamps_battery);
    RUN_TEST(test_pack_event_fields);
    RUN_TEST(test_validate_accepts_good_messages);
    RUN_TEST(test_validate_rejects_bad_lengths);
    RUN_TEST(test_validate_rejects_wrong_version_and_type);
    RUN_TEST(test_validate_rejects_out_of_range_fields);
    RUN_TEST(test_validate_accepts_sensor_invalid_markers);
    RUN_TEST(test_validate_works_on_unaligned_buffer);
    RUN_TEST(test_pack_time_and_cancel_fields);
    RUN_TEST(test_validate_time_message);
    RUN_TEST(test_validate_cancel_message);
    RUN_TEST(test_unknown_battery_marker);
    RUN_TEST(test_telemetry_json_exact);
    RUN_TEST(test_telemetry_json_with_timestamp);
    RUN_TEST(test_telemetry_json_invalid_readings_become_null);
    RUN_TEST(test_event_json_exact);
    RUN_TEST(test_event_json_all_names);
    RUN_TEST(test_json_buffer_too_small_fails_cleanly);
    RUN_TEST(test_json_exact_fit_boundary);
    RUN_TEST(test_unsafe_timestamp_is_omitted);
    RUN_TEST(test_event_json_rejects_unvalidated_values);
    RUN_TEST(test_round_trip_pack_validate_json);
    return UNITY_END();
}
