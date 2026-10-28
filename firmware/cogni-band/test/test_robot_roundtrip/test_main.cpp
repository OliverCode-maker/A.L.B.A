// Prueba de ida y vuelta PULSERA <-> ROBOT, en el PC y con los modulos reales de
// ambos lados (solo el "aire" de ESP-NOW es simulado):
//
//   senal del IMU -> fall_detector -> band_alerts -> alba_pack_event
//        -> [aire] -> robot_link -> robot_fall -> alba_pack_cancel
//        -> [aire] -> alba_validate -> band_cancel_gate -> fall_detector_cancel
//        -> CANCELADA -> [aire] -> robot -> alba_event_to_tb (lo que se publica a ThingsBoard)
//
// Correr con:  pio test -e native -f test_robot_roundtrip
// (el entorno native de cogni-band apunta a ../cogni-desk/lib con lib_extra_dirs)
#include <unity.h>
#include <string.h>

#include "alba_thingsboard.h"
#include "band_alerts.h"
#include "band_clock.h"
#include "fall_detector.h"
#include "robot_fall.h"
#include "robot_link.h"
#include "wake_detector.h"

// ---- estado de la pulsera --------------------------------------------------------
static fall_detector_t   fd;
static band_cancel_gate_t gate;
static band_clock_t      clk;
static uint32_t          T;            // reloj de la pulsera (ms)
static uint16_t          band_seq;

// ---- estado del robot --------------------------------------------------------------
static robot_link_t link_;
static robot_fall_t rf;
static uint32_t     R;                 // reloj del robot (ms): corre aparte del de la pulsera
static uint16_t     robot_seq;
static char         last_tb[200];      // ultimo JSON publicado a ThingsBoard
static int          prompts_shown, help_called, hidden;

void setUp(void)
{
    fall_detector_init(&fd, NULL);
    band_cancel_gate_init(&gate);
    band_clock_init(&clk);
    T = 1000;  band_seq = 0;
    robot_link_init(&link_);
    robot_fall_init(&rf);
    R = 500000;  robot_seq = 0;
    last_tb[0] = '\0';
    prompts_shown = help_called = hidden = 0;
}
void tearDown(void) {}

// ---- senal del IMU (igual que en las otras pruebas) ---------------------------------------
static fall_event_t feed(float ax, float ay, float az)
{
    fall_sample_t s;
    memset(&s, 0, sizeof s);
    s.t_ms = T;  s.ax = ax;  s.ay = ay;  s.az = az;
    T += 10;  R += 10;
    return fall_detector_process(&fd, &s);
}

static fall_event_t run_fall_until_event(void)
{
    fall_event_t ev = FALL_EVT_NONE;
    for (int i = 0; i < 200; i++) feed(1.0f, 0.0f, 0.0f);
    for (int i = 0; i < 25;  i++) feed(0.3f, 0.0f, 0.0f);
    feed(0.0f, 0.0f, 4.0f);  feed(0.0f, 0.0f, 2.8f);  feed(0.0f, 0.5f, 1.8f);
    for (int i = 0; i < 400 && ev == FALL_EVT_NONE; i++) ev = feed(0.0f, 0.0f, 1.0f);
    return ev;
}

// ---- "el aire" -------------------------------------------------------------------------------
// La pulsera envia un evento: lo arma, lo anota en la compuerta y lo "transmite".
// Devuelve el paquete (para poder repetirlo o perderlo).
struct Pkt { uint8_t b[ALBA_ESPNOW_MAX_PAYLOAD]; size_t n; uint16_t seq; };

static Pkt band_sends_fall_event(fall_event_t ev)
{
    Pkt p;
    memset(&p, 0, sizeof p);
    band_alert_t a;
    TEST_ASSERT_TRUE(band_alert_from_fall(ev, fall_detector_features(&fd), &a));
    alba_event_t e;
    p.seq = band_seq++;
    alba_pack_event(&e, p.seq, T, a.alerta_tipo, a.valor_pico, a.confirmacion);
    band_cancel_note_sent(&gate, &a, p.seq);
    memcpy(p.b, &e, sizeof e);
    p.n = sizeof e;
    return p;
}

// El robot recibe un paquete y reacciona como lo hara el firmware real.
static robot_rx_result_t robot_receives(const Pkt &p)
{
    robot_rx_msg_t m;
    const robot_rx_result_t r = robot_link_on_packet(&link_, p.b, p.n, R, &m);
    if (r != ROBOT_RX_NEW) return r;

    if (m.type == ALBA_MSG_EVENTO) {
        uint16_t ref = 0;
        const robot_action_t a = robot_fall_on_event(&rf, &m.evt, R, &ref);
        if (a == ROBOT_ACT_SHOW_PROMPT)     prompts_shown++;
        if (a == ROBOT_ACT_SHOW_HELP_CALLED) help_called++;
        if (a == ROBOT_ACT_HIDE_PROMPT)     hidden++;
        alba_event_to_tb(&m.evt, 1793016013270ull, last_tb, sizeof last_tb);   // lo que iria a ThingsBoard
    }
    return r;
}

// El robot manda CANCELAR; devuelve el paquete.
static Pkt robot_sends_cancel(uint16_t ref)
{
    Pkt p;
    memset(&p, 0, sizeof p);
    alba_cancel_t c;
    p.seq = robot_seq++;
    alba_pack_cancel(&c, p.seq, ref);
    memcpy(p.b, &c, sizeof c);
    p.n = sizeof c;
    return p;
}

// La pulsera recibe un paquete del robot (como tComm: valida, y solo cancela si coincide la caida).
static bool band_receives_cancel(const Pkt &p)
{
    if (alba_validate(p.b, p.n) != ALBA_MSG_CANCELAR) return false;
    alba_cancel_t c;
    memcpy(&c, p.b, sizeof c);
    if (!band_cancel_accept(&gate, c.ref_seq)) return false;
    fall_detector_cancel(&fd);
    return true;
}

// ---- escenarios ------------------------------------------------------------------------------
void test_patient_says_fine_and_fall_is_cancelled_end_to_end(void)
{
    // 1) La pulsera detecta la caida y avisa; el robot pregunta.
    const fall_event_t ev = run_fall_until_event();
    TEST_ASSERT_EQUAL_INT(FALL_EVT_CANDIDATE, ev);
    const Pkt pending = band_sends_fall_event(ev);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, robot_receives(pending));
    TEST_ASSERT_EQUAL_INT(1, prompts_shown);
    TEST_ASSERT_NOT_NULL(strstr(last_tb, "\"confirm\":\"PENDIENTE\""));

    // 2) El paciente pulsa "estoy bien" 3 s despues.
    R += 3000;  T += 3000;
    uint16_t ref = 0;
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SEND_CANCEL, robot_fall_on_button_ok(&rf, R, &ref));
    TEST_ASSERT_EQUAL_UINT16(pending.seq, ref);

    // 3) La pulsera acepta el cancelar y lo resuelve.
    TEST_ASSERT_TRUE(band_receives_cancel(robot_sends_cancel(ref)));
    fall_event_t next = FALL_EVT_NONE;
    for (int i = 0; i < 5 && next == FALL_EVT_NONE; i++) next = feed(0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_INT(FALL_EVT_CANCELLED, next);

    // 4) El robot se entera y quita la pregunta.
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, robot_receives(band_sends_fall_event(next)));
    TEST_ASSERT_EQUAL_INT(1, hidden);
    TEST_ASSERT_NOT_NULL(strstr(last_tb, "\"confirm\":\"CANCELADA\""));
    TEST_ASSERT_EQUAL_INT(ROBOT_FALL_IDLE, robot_fall_state(&rf));

    // 5) Aunque pase la ventana completa, nunca llega CONFIRMADA.
    for (int i = 0; i < 2500; i++) TEST_ASSERT_NOT_EQUAL(FALL_EVT_CONFIRMED, feed(0.0f, 0.0f, 1.0f));
    TEST_ASSERT_EQUAL_UINT32(0, help_called);
}

void test_nobody_answers_and_fall_is_confirmed_end_to_end(void)
{
    const fall_event_t ev = run_fall_until_event();
    robot_receives(band_sends_fall_event(ev));
    TEST_ASSERT_EQUAL_INT(1, prompts_shown);

    fall_event_t next = FALL_EVT_NONE;                    // pasan los 20 s sin respuesta
    for (int i = 0; i < 2500 && next == FALL_EVT_NONE; i++) next = feed(0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_INT(FALL_EVT_CONFIRMED, next);

    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, robot_receives(band_sends_fall_event(next)));
    TEST_ASSERT_EQUAL_INT(1, help_called);
    TEST_ASSERT_NOT_NULL(strstr(last_tb, "\"confirm\":\"CONFIRMADA\""));
    TEST_ASSERT_NOT_NULL(strstr(last_tb, "\"alert\":\"CAIDA_DETECTADA\""));
}

void test_first_cancel_is_lost_in_the_air_but_the_repeat_gets_through(void)
{
    const fall_event_t ev = run_fall_until_event();
    robot_receives(band_sends_fall_event(ev));

    uint16_t ref = 0;
    robot_fall_on_button_ok(&rf, R, &ref);
    const Pkt lost = robot_sends_cancel(ref);              // este paquete NO llega
    (void)lost;

    R += 400;
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SEND_CANCEL, robot_fall_tick(&rf, R, &ref));   // el robot reenvia
    TEST_ASSERT_TRUE(band_receives_cancel(robot_sends_cancel(ref)));               // este si
    fall_event_t next = FALL_EVT_NONE;
    for (int i = 0; i < 5 && next == FALL_EVT_NONE; i++) next = feed(0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_INT(FALL_EVT_CANCELLED, next);
}

void test_repeated_cancels_after_acceptance_are_harmless(void)
{
    const fall_event_t ev = run_fall_until_event();
    robot_receives(band_sends_fall_event(ev));
    uint16_t ref = 0;
    robot_fall_on_button_ok(&rf, R, &ref);

    TEST_ASSERT_TRUE(band_receives_cancel(robot_sends_cancel(ref)));      // el 1.o se acepta
    TEST_ASSERT_FALSE(band_receives_cancel(robot_sends_cancel(ref)));     // los reenvios se ignoran
    TEST_ASSERT_FALSE(band_receives_cancel(robot_sends_cancel(ref)));
}

void test_a_cancel_for_another_fall_is_ignored(void)
{
    const fall_event_t ev = run_fall_until_event();
    const Pkt pending = band_sends_fall_event(ev);
    robot_receives(pending);
    TEST_ASSERT_FALSE(band_receives_cancel(robot_sends_cancel((uint16_t)(pending.seq + 1))));   // seq equivocado
    TEST_ASSERT_FALSE(band_receives_cancel(robot_sends_cancel((uint16_t)(pending.seq + 40))));
}

void test_retransmitted_pending_does_not_ask_twice(void)
{
    const fall_event_t ev = run_fall_until_event();
    const Pkt pending = band_sends_fall_event(ev);
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, robot_receives(pending));
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_DUPLICATE, robot_receives(pending));   // la pulsera reintento el envio
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_DUPLICATE, robot_receives(pending));
    TEST_ASSERT_EQUAL_INT(1, prompts_shown);
}

void test_robot_gives_the_time_to_a_band_that_just_booted(void)
{
    // La pulsera arranca y manda su primera telemetria.
    alba_telemetry_t t;
    alba_pack_telemetry(&t, band_seq++, T, 58, 97, ALBA_BAT_INVALID);
    robot_rx_msg_t m;
    TEST_ASSERT_EQUAL_INT(ROBOT_RX_NEW, robot_link_on_packet(&link_, (const uint8_t *)&t, sizeof t, R, &m));

    // El robot ve que debe mandar la hora (07:00 en Ecuador = 12:00 UTC).
    TEST_ASSERT_TRUE(robot_link_need_time(&link_, R));
    alba_time_t h;
    alba_pack_time(&h, robot_seq++, 1793016000u, -300);
    robot_link_time_sent(&link_, R);

    // La pulsera la valida y la usa; antes no sabia la hora.
    TEST_ASSERT_EQUAL_UINT8(WAKE_HOUR_UNKNOWN, band_clock_hour(&clk, T));
    TEST_ASSERT_EQUAL_INT(ALBA_MSG_HORA, alba_validate((const uint8_t *)&h, sizeof h));
    TEST_ASSERT_TRUE(band_clock_sync(&clk, T, h.epoch_s, h.utc_offset_min));
    TEST_ASSERT_EQUAL_UINT8(7, band_clock_hour(&clk, T));

    // La telemetria sin bateria (aun sin divisor) sale a ThingsBoard sin la clave "bat".
    char tb[128];
    TEST_ASSERT_TRUE(alba_telemetry_to_tb(&m.tel, 1793016000000ull, tb, sizeof tb) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1793016000000,\"values\":{\"bpm\":58,\"spo2\":97}}", tb);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_patient_says_fine_and_fall_is_cancelled_end_to_end);
    RUN_TEST(test_nobody_answers_and_fall_is_confirmed_end_to_end);
    RUN_TEST(test_first_cancel_is_lost_in_the_air_but_the_repeat_gets_through);
    RUN_TEST(test_repeated_cancels_after_acceptance_are_harmless);
    RUN_TEST(test_a_cancel_for_another_fall_is_ignored);
    RUN_TEST(test_retransmitted_pending_does_not_ask_twice);
    RUN_TEST(test_robot_gives_the_time_to_a_band_that_just_booted);
    return UNITY_END();
}
