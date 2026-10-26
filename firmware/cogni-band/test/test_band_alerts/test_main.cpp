// Pruebas del puente detectores -> protocolo, y de la cadena completa
// (senal del IMU -> detector -> paquete -> validacion -> JSON del robot).
// Correr con:  pio test -e native -f test_band_alerts
#include <unity.h>
#include <string.h>

#include "alba_codec.h"
#include "band_alerts.h"

void setUp(void) {}
void tearDown(void) {}

// ---- traduccion de eventos --------------------------------------------------
void test_fall_events_map_to_confirmation_states(void)
{
    fall_features_t f;
    memset(&f, 0, sizeof f);
    f.svm_max = 3.85f;
    band_alert_t a;

    TEST_ASSERT_TRUE(band_alert_from_fall(FALL_EVT_CANDIDATE, &f, &a));
    TEST_ASSERT_EQUAL_UINT8(ALBA_ALERTA_CAIDA_DETECTADA, a.alerta_tipo);
    TEST_ASSERT_EQUAL_UINT16(3850, a.valor_pico);
    TEST_ASSERT_EQUAL_UINT8(ALBA_CONF_PENDIENTE, a.confirmacion);

    TEST_ASSERT_TRUE(band_alert_from_fall(FALL_EVT_CONFIRMED, &f, &a));
    TEST_ASSERT_EQUAL_UINT8(ALBA_CONF_CONFIRMADA, a.confirmacion);

    TEST_ASSERT_TRUE(band_alert_from_fall(FALL_EVT_CANCELLED, &f, &a));
    TEST_ASSERT_EQUAL_UINT8(ALBA_CONF_CANCELADA, a.confirmacion);
}

void test_rejected_and_none_send_nothing(void)
{
    fall_features_t f;
    memset(&f, 0, sizeof f);
    band_alert_t a;
    TEST_ASSERT_FALSE(band_alert_from_fall(FALL_EVT_NONE, &f, &a));
    TEST_ASSERT_FALSE(band_alert_from_fall(FALL_EVT_REJECTED, &f, &a));   // falsa alarma: no molestar al cuidador
    TEST_ASSERT_FALSE(band_alert_from_wake(WAKE_EVT_NONE, 88, &a));
}

void test_peak_conversion_edge_cases(void)
{
    band_alert_t a;
    fall_features_t f;
    memset(&f, 0, sizeof f);

    f.svm_max = 100.0f;                                            // sensor saturado o dato corrupto
    band_alert_from_fall(FALL_EVT_CANDIDATE, &f, &a);
    TEST_ASSERT_EQUAL_UINT16(65535, a.valor_pico);

    f.svm_max = -1.0f;
    band_alert_from_fall(FALL_EVT_CANDIDATE, &f, &a);
    TEST_ASSERT_EQUAL_UINT16(0, a.valor_pico);

    band_alert_from_fall(FALL_EVT_CANDIDATE, NULL, &a);            // sin caracteristicas: no se cuelga
    TEST_ASSERT_EQUAL_UINT16(0, a.valor_pico);

    TEST_ASSERT_FALSE(band_alert_from_fall(FALL_EVT_CANDIDATE, &f, NULL));
}

void test_wake_trigger_maps_to_vigilia_event(void)
{
    band_alert_t a;
    TEST_ASSERT_TRUE(band_alert_from_wake(WAKE_EVT_TRIGGER, 88, &a));
    TEST_ASSERT_EQUAL_UINT8(ALBA_ALERTA_VIGILIA_TRIGGER, a.alerta_tipo);
    TEST_ASSERT_EQUAL_UINT16(88, a.valor_pico);
    TEST_ASSERT_EQUAL_UINT8(ALBA_CONF_CONFIRMADA, a.confirmacion);
}

// ---- compuerta de cancelacion ------------------------------------------------
static band_alert_t fall_alert(uint8_t conf)
{
    band_alert_t a;
    a.alerta_tipo = ALBA_ALERTA_CAIDA_DETECTADA;
    a.valor_pico = 3000;
    a.confirmacion = conf;
    return a;
}

void test_cancel_gate_accepts_only_matching_pending_fall(void)
{
    band_cancel_gate_t g;
    band_cancel_gate_init(&g);
    TEST_ASSERT_FALSE(band_cancel_accept(&g, 0));                 // nada pendiente (ni siquiera seq 0)

    band_alert_t pend = fall_alert(ALBA_CONF_PENDIENTE);
    band_cancel_note_sent(&g, &pend, 124);
    TEST_ASSERT_FALSE(band_cancel_accept(&g, 123));               // de otra caida
    TEST_ASSERT_FALSE(band_cancel_accept(&g, 125));
    TEST_ASSERT_TRUE(band_cancel_accept(&g, 124));
    TEST_ASSERT_FALSE(band_cancel_accept(&g, 124));               // repetido: ya se uso
}

void test_cancel_gate_closes_when_fall_is_resolved(void)
{
    band_cancel_gate_t g;
    band_cancel_gate_init(&g);
    band_alert_t pend = fall_alert(ALBA_CONF_PENDIENTE);
    band_cancel_note_sent(&g, &pend, 10);

    band_alert_t conf = fall_alert(ALBA_CONF_CONFIRMADA);
    band_cancel_note_sent(&g, &conf, 11);                         // paso la ventana: ya es tarde
    TEST_ASSERT_FALSE(band_cancel_accept(&g, 10));

    band_cancel_note_sent(&g, &pend, 20);
    band_alert_t canc = fall_alert(ALBA_CONF_CANCELADA);
    band_cancel_note_sent(&g, &canc, 21);
    TEST_ASSERT_FALSE(band_cancel_accept(&g, 20));
}

void test_cancel_gate_ignores_other_alerts_and_null(void)
{
    band_cancel_gate_t g;
    band_cancel_gate_init(&g);
    band_alert_t pend = fall_alert(ALBA_CONF_PENDIENTE);
    band_cancel_note_sent(&g, &pend, 30);

    band_alert_t wake = { ALBA_ALERTA_VIGILIA_TRIGGER, 88, ALBA_CONF_CONFIRMADA };
    band_cancel_note_sent(&g, &wake, 31);                         // una vigilia no cierra la caida pendiente
    band_cancel_note_sent(&g, NULL, 32);
    band_cancel_note_sent(NULL, &pend, 33);
    TEST_ASSERT_TRUE(band_cancel_accept(&g, 30));
    TEST_ASSERT_FALSE(band_cancel_accept(NULL, 30));
}

// ---- cadena completa ---------------------------------------------------------
static fall_detector_t fd;
static uint32_t T;

static fall_event_t feed(float ax, float ay, float az)
{
    fall_sample_t s;
    memset(&s, 0, sizeof s);
    s.t_ms = T;  s.ax = ax;  s.ay = ay;  s.az = az;
    T += 10;
    return fall_detector_process(&fd, &s);
}

// Alimenta una caida sintetica y devuelve el primer evento que no sea NONE.
static fall_event_t run_fall_until_event(void)
{
    fall_event_t ev = FALL_EVT_NONE;
    for (int i = 0; i < 200; i++) feed(1.0f, 0.0f, 0.0f);               // brazo colgando
    for (int i = 0; i < 25;  i++) feed(0.3f, 0.0f, 0.0f);               // caida libre
    feed(0.0f, 0.0f, 4.0f);  feed(0.0f, 0.0f, 2.8f);  feed(0.0f, 0.5f, 1.8f);   // impacto
    for (int i = 0; i < 400 && ev == FALL_EVT_NONE; i++) ev = feed(0.0f, 0.0f, 1.0f);
    return ev;
}

void test_end_to_end_fall_reaches_robot_as_json(void)
{
    fall_detector_init(&fd, NULL);
    T = 1000;

    // 1) Pulsera: el detector marca una caida candidata.
    const fall_event_t ev = run_fall_until_event();
    TEST_ASSERT_EQUAL_INT(FALL_EVT_CANDIDATE, ev);

    // 2) Pulsera: se arma el paquete.
    band_alert_t a;
    TEST_ASSERT_TRUE(band_alert_from_fall(ev, fall_detector_features(&fd), &a));
    alba_event_t pkt;
    alba_pack_event(&pkt, 7, T, a.alerta_tipo, a.valor_pico, a.confirmacion);

    // 3) "Por el aire": solo bytes.
    uint8_t air[ALBA_ESPNOW_MAX_PAYLOAD];
    memcpy(air, &pkt, sizeof pkt);

    // 4) Robot: valida y convierte a JSON para MQTT.
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_EVENTO, alba_validate(air, sizeof pkt));
    alba_event_t rx;
    memcpy(&rx, air, sizeof rx);
    char json[240];
    TEST_ASSERT_TRUE(alba_event_to_json(&rx, "2026-10-25T07:15:00-05:00", json, sizeof json) > 0);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"type\":\"event\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"seq\":7"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"alert\":\"CAIDA_DETECTADA\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"peak\":4000"));              // 4.0 g = 4000 mili-g
    TEST_ASSERT_NOT_NULL(strstr(json, "\"confirm\":\"PENDIENTE\""));
}

// Robot manda CANCELAR por el aire -> la pulsera lo valida -> el detector cancela.
void test_end_to_end_robot_cancels_fall(void)
{
    fall_detector_init(&fd, NULL);
    T = 1000;
    band_cancel_gate_t gate;
    band_cancel_gate_init(&gate);

    // Pulsera: caida candidata, se envia PENDIENTE con seq 7.
    const fall_event_t ev = run_fall_until_event();
    TEST_ASSERT_EQUAL_INT(FALL_EVT_CANDIDATE, ev);
    band_alert_t a;
    TEST_ASSERT_TRUE(band_alert_from_fall(ev, fall_detector_features(&fd), &a));
    band_cancel_note_sent(&gate, &a, 7);

    // Robot: el paciente pulso "estoy bien" -> manda CANCELAR con ref_seq 7.
    alba_cancel_t tx;
    alba_pack_cancel(&tx, 1, 7);
    uint8_t air[ALBA_ESPNOW_MAX_PAYLOAD];
    memcpy(air, &tx, sizeof tx);

    // Pulsera: valida, comprueba que es de esta caida y cancela.
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_CANCELAR, alba_validate(air, sizeof tx));
    alba_cancel_t rx;
    memcpy(&rx, air, sizeof rx);
    TEST_ASSERT_TRUE(band_cancel_accept(&gate, rx.ref_seq));
    fall_detector_cancel(&fd);

    fall_event_t next = FALL_EVT_NONE;
    for (int i = 0; i < 5 && next == FALL_EVT_NONE; i++) next = feed(0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_INT(FALL_EVT_CANCELLED, next);
    TEST_ASSERT_TRUE(band_alert_from_fall(next, fall_detector_features(&fd), &a));
    TEST_ASSERT_EQUAL_UINT8(ALBA_CONF_CANCELADA, a.confirmacion);

    // Nunca llega a CONFIRMADA aunque pase la ventana completa.
    for (int i = 0; i < 2500; i++) TEST_ASSERT_NOT_EQUAL(FALL_EVT_CONFIRMED, feed(0.0f, 0.0f, 1.0f));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_fall_events_map_to_confirmation_states);
    RUN_TEST(test_rejected_and_none_send_nothing);
    RUN_TEST(test_peak_conversion_edge_cases);
    RUN_TEST(test_wake_trigger_maps_to_vigilia_event);
    RUN_TEST(test_cancel_gate_accepts_only_matching_pending_fall);
    RUN_TEST(test_cancel_gate_closes_when_fall_is_resolved);
    RUN_TEST(test_cancel_gate_ignores_other_alerts_and_null);
    RUN_TEST(test_end_to_end_fall_reaches_robot_as_json);
    RUN_TEST(test_end_to_end_robot_cancels_fall);
    return UNITY_END();
}
