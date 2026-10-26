// Pruebas del contrato de mensajes (alba_protocol.h).
// Correr con:  pio test -e native -f test_protocol
#include <unity.h>
#include <string.h>

#include "alba_protocol.h"

void setUp(void) {}
void tearDown(void) {}

// Si alguien cambia un campo y se rompe el tamano, el receptor del robot
// dejaria de entender los paquetes: esta prueba lo avisa antes.
void test_sizes(void)
{
    TEST_ASSERT_EQUAL_UINT32(4,  sizeof(alba_hdr_t));
    TEST_ASSERT_EQUAL_UINT32(11, sizeof(alba_telemetry_t));
    TEST_ASSERT_EQUAL_UINT32(12, sizeof(alba_event_t));
    TEST_ASSERT_EQUAL_UINT32(10, sizeof(alba_time_t));
    TEST_ASSERT_EQUAL_UINT32(6,  sizeof(alba_cancel_t));
}

// El orden de bytes en el cable debe ser el que espera el receptor
// (little-endian en ambos ESP32).
void test_event_layout(void)
{
    alba_event_t e;
    memset(&e, 0, sizeof e);
    e.hdr.version  = ALBA_PROTO_VERSION;
    e.hdr.type     = ALBA_MSG_EVENTO;
    e.hdr.seq      = 0x0102;
    e.timestamp_ms = 0x0A0B0C0Du;
    e.alerta_tipo  = ALBA_ALERTA_CAIDA_DETECTADA;
    e.valor_pico   = 0x1234;
    e.confirmacion = ALBA_CONF_CONFIRMADA;

    const uint8_t expected[12] = { 1, 2, 0x02, 0x01, 0x0D, 0x0C, 0x0B, 0x0A, 1, 0x34, 0x12, 1 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, &e, sizeof expected);
}

void test_telemetry_layout(void)
{
    alba_telemetry_t t;
    memset(&t, 0, sizeof t);
    t.hdr.version  = ALBA_PROTO_VERSION;
    t.hdr.type     = ALBA_MSG_TELEMETRIA;
    t.hdr.seq      = 0x0304;
    t.timestamp_ms = 0x11223344u;
    t.bpm          = 62;
    t.spo2         = 97;
    t.bateria_pct  = 84;

    const uint8_t expected[11] = { 1, 1, 0x04, 0x03, 0x44, 0x33, 0x22, 0x11, 62, 97, 84 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, &t, sizeof expected);
}

void test_time_layout(void)
{
    alba_time_t h;
    memset(&h, 0, sizeof h);
    h.hdr.version    = ALBA_PROTO_VERSION;
    h.hdr.type       = ALBA_MSG_HORA;
    h.hdr.seq        = 0x0506;
    h.epoch_s        = 0x65A1B2C3u;
    h.utc_offset_min = -300;                       // Ecuador: 0xFED4

    const uint8_t expected[10] = { 1, 3, 0x06, 0x05, 0xC3, 0xB2, 0xA1, 0x65, 0xD4, 0xFE };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, &h, sizeof expected);
}

void test_cancel_layout(void)
{
    alba_cancel_t c;
    memset(&c, 0, sizeof c);
    c.hdr.version = ALBA_PROTO_VERSION;
    c.hdr.type    = ALBA_MSG_CANCELAR;
    c.hdr.seq     = 9;
    c.ref_seq     = 0x0A0B;

    const uint8_t expected[6] = { 1, 4, 9, 0, 0x0B, 0x0A };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, &c, sizeof expected);
}

void test_message_types_are_distinct(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, ALBA_MSG_TELEMETRIA);
    TEST_ASSERT_EQUAL_UINT8(2, ALBA_MSG_EVENTO);
    TEST_ASSERT_EQUAL_UINT8(3, ALBA_MSG_HORA);
    TEST_ASSERT_EQUAL_UINT8(4, ALBA_MSG_CANCELAR);
}

void test_enums_fit_in_one_byte(void)
{
    TEST_ASSERT_TRUE(ALBA_ALERTA_BATERIA_BAJA < 256);
    TEST_ASSERT_TRUE(ALBA_CONF_CANCELADA < 256);
    TEST_ASSERT_TRUE(ALBA_MSG_EVENTO < 256);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sizes);
    RUN_TEST(test_event_layout);
    RUN_TEST(test_telemetry_layout);
    RUN_TEST(test_time_layout);
    RUN_TEST(test_cancel_layout);
    RUN_TEST(test_message_types_are_distinct);
    RUN_TEST(test_enums_fit_in_one_byte);
    return UNITY_END();
}
