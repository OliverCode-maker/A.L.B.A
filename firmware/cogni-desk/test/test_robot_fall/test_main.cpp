// Pruebas del asistente de caidas del robot ("¿Estas bien?" y CANCELAR).
// Correr con:  pio test -e native -f test_robot_fall
#include <unity.h>
#include <string.h>

#include "robot_fall.h"

static robot_fall_t f;
static uint16_t ref;

void setUp(void) { robot_fall_init(&f); ref = 0xFFFF; }
void tearDown(void) {}

static alba_event_t ev(uint8_t alerta, uint8_t conf, uint16_t seq)
{
    alba_event_t e;
    memset(&e, 0, sizeof e);
    alba_pack_event(&e, seq, 1000, alerta, 3900, conf);
    return e;
}

static robot_action_t on(uint8_t alerta, uint8_t conf, uint16_t seq, uint32_t now)
{
    const alba_event_t e = ev(alerta, conf, seq);
    return robot_fall_on_event(&f, &e, now, &ref);
}

void test_pending_fall_shows_prompt(void)
{
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SHOW_PROMPT, on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 12, 1000));
    TEST_ASSERT_EQUAL_INT(ROBOT_FALL_ASKING, robot_fall_state(&f));
    TEST_ASSERT_EQUAL_UINT16(12, ref);
}

void test_button_sends_cancel_with_the_falls_seq(void)
{
    on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 12, 1000);
    ref = 0;
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SEND_CANCEL, robot_fall_on_button_ok(&f, 4000, &ref));
    TEST_ASSERT_EQUAL_UINT16(12, ref);
    TEST_ASSERT_EQUAL_INT(ROBOT_FALL_CANCELING, robot_fall_state(&f));
}

void test_button_with_no_fall_does_nothing(void)
{
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_on_button_ok(&f, 1000, &ref));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, ref);                // y no inventa un ref_seq
}

void test_cancel_is_repeated_three_times_then_stops(void)
{
    on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 12, 1000);
    robot_fall_on_button_ok(&f, 4000, &ref);                              // 1.o (inmediato)
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_tick(&f, 4200, &ref));          // muy pronto
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SEND_CANCEL, robot_fall_tick(&f, 4400, &ref));   // 2.o
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SEND_CANCEL, robot_fall_tick(&f, 4800, &ref));   // 3.o
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SEND_CANCEL, robot_fall_tick(&f, 5200, &ref));   // 4.o
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_tick(&f, 5600, &ref));          // ya no mas
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_tick(&f, 9000, &ref));
}

void test_pressing_twice_does_not_restart_the_cancel(void)
{
    on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 12, 1000);
    robot_fall_on_button_ok(&f, 4000, &ref);
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_on_button_ok(&f, 4100, &ref));
}

void test_cancelled_confirmation_hides_prompt(void)
{
    on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 12, 1000);
    robot_fall_on_button_ok(&f, 4000, &ref);
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_HIDE_PROMPT, on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_CANCELADA, 13, 4100));
    TEST_ASSERT_EQUAL_INT(ROBOT_FALL_IDLE, robot_fall_state(&f));
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_tick(&f, 4600, &ref));          // ya no reenvia
}

void test_confirmed_fall_tells_patient_help_was_called(void)
{
    on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 12, 1000);
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SHOW_HELP_CALLED, on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_CONFIRMADA, 13, 21000));
    TEST_ASSERT_EQUAL_INT(ROBOT_FALL_IDLE, robot_fall_state(&f));
}

void test_confirmed_without_seeing_pending_still_reacts(void)
{
    // El PENDIENTE se perdio por el aire: igual hay que avisar al paciente.
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SHOW_HELP_CALLED, on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_CONFIRMADA, 13, 21000));
}

void test_confirmed_while_canceling_means_the_cancel_was_lost(void)
{
    on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 12, 1000);
    robot_fall_on_button_ok(&f, 4000, &ref);
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SHOW_HELP_CALLED, on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_CONFIRMADA, 13, 21000));
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_tick(&f, 21400, &ref));         // sin reenvios fantasma
}

void test_cancelled_while_idle_is_ignored(void)
{
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_CANCELADA, 13, 1000));
}

void test_prompt_times_out_if_nothing_else_arrives(void)
{
    on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 12, 1000);
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_tick(&f, 1000 + ROBOT_FALL_PROMPT_TIMEOUT_MS - 1, &ref));
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_HIDE_PROMPT, robot_fall_tick(&f, 1000 + ROBOT_FALL_PROMPT_TIMEOUT_MS, &ref));
    TEST_ASSERT_EQUAL_INT(ROBOT_FALL_IDLE, robot_fall_state(&f));
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_on_button_ok(&f, 30000, &ref));  // un boton tardio ya no cancela nada
}

void test_new_fall_replaces_the_one_in_progress(void)
{
    on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 12, 1000);
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SHOW_PROMPT, on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 20, 9000));
    robot_fall_on_button_ok(&f, 10000, &ref);
    TEST_ASSERT_EQUAL_UINT16(20, ref);                    // cancela la caida nueva, no la vieja
}

void test_other_alerts_are_ignored(void)
{
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, on(ALBA_ALERTA_VIGILIA_TRIGGER, ALBA_CONF_CONFIRMADA, 5, 1000));
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, on(ALBA_ALERTA_BATERIA_BAJA, ALBA_CONF_CONFIRMADA, 6, 1000));
    TEST_ASSERT_EQUAL_INT(ROBOT_FALL_IDLE, robot_fall_state(&f));
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_on_event(&f, NULL, 0, &ref));
}

void test_timer_survives_clock_wrap(void)
{
    const uint32_t t0 = 0xFFFFFFFFu - 5000u;               // el reloj del robot da la vuelta durante la pregunta
    on(ALBA_ALERTA_CAIDA_DETECTADA, ALBA_CONF_PENDIENTE, 12, t0);
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_NONE, robot_fall_tick(&f, 10000u, &ref));        // 15 s despues
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_HIDE_PROMPT, robot_fall_tick(&f, 20000u, &ref)); // 25 s despues
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_pending_fall_shows_prompt);
    RUN_TEST(test_button_sends_cancel_with_the_falls_seq);
    RUN_TEST(test_button_with_no_fall_does_nothing);
    RUN_TEST(test_cancel_is_repeated_three_times_then_stops);
    RUN_TEST(test_pressing_twice_does_not_restart_the_cancel);
    RUN_TEST(test_cancelled_confirmation_hides_prompt);
    RUN_TEST(test_confirmed_fall_tells_patient_help_was_called);
    RUN_TEST(test_confirmed_without_seeing_pending_still_reacts);
    RUN_TEST(test_confirmed_while_canceling_means_the_cancel_was_lost);
    RUN_TEST(test_cancelled_while_idle_is_ignored);
    RUN_TEST(test_prompt_times_out_if_nothing_else_arrives);
    RUN_TEST(test_new_fall_replaces_the_one_in_progress);
    RUN_TEST(test_other_alerts_are_ignored);
    RUN_TEST(test_timer_survives_clock_wrap);
    return UNITY_END();
}
