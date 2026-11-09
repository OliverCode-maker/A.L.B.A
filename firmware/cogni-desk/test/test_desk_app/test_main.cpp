#include <unity.h>

#include "DeskApp.h"

static desk_app_t app;

void setUp(void)
{
    desk_app_init(&app, 0u);
}

void tearDown(void) {}

void test_splash_transitions_to_idle_without_blocking(void)
{
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_SPLASH, desk_app_view(&app).screen);
    desk_app_tick(&app, DESK_SPLASH_DURATION_MS - 1u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_SPLASH, desk_app_view(&app).screen);
    desk_app_tick(&app, DESK_SPLASH_DURATION_MS, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_IDLE, desk_app_view(&app).screen);
}

void test_idle_eyes_blink_without_blocking(void)
{
    desk_app_tick(&app, DESK_SPLASH_DURATION_MS, nullptr);
    TEST_ASSERT_FALSE(desk_app_view(&app).eyes_closed);

    desk_app_tick(&app, DESK_SPLASH_DURATION_MS + DESK_BLINK_INTERVAL_MS - 1u, nullptr);
    TEST_ASSERT_FALSE(desk_app_view(&app).eyes_closed);
    desk_app_tick(&app, DESK_SPLASH_DURATION_MS + DESK_BLINK_INTERVAL_MS, nullptr);
    TEST_ASSERT_TRUE(desk_app_view(&app).eyes_closed);

    desk_app_tick(&app, DESK_SPLASH_DURATION_MS + DESK_BLINK_INTERVAL_MS +
                            DESK_BLINK_DURATION_MS - 1u, nullptr);
    TEST_ASSERT_TRUE(desk_app_view(&app).eyes_closed);
    desk_app_tick(&app, DESK_SPLASH_DURATION_MS + DESK_BLINK_INTERVAL_MS +
                            DESK_BLINK_DURATION_MS, nullptr);
    TEST_ASSERT_FALSE(desk_app_view(&app).eyes_closed);
}

void test_menu_navigation_opens_environment_and_feature_placeholder(void)
{
    TEST_ASSERT_EQUAL_UINT8(4u, DESK_MENU_ITEM_COUNT);
    desk_app_tick(&app, DESK_SPLASH_DURATION_MS, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_MENU, desk_app_view(&app).screen);

    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS + 1u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_ENVIRONMENT, desk_app_view(&app).screen);
    desk_app_on_input(&app, DESK_INPUT_LEFT, DESK_SPLASH_DURATION_MS + 2u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_MENU, desk_app_view(&app).screen);

    desk_app_on_input(&app, DESK_INPUT_LEFT, DESK_SPLASH_DURATION_MS + 1u, nullptr);
    TEST_ASSERT_EQUAL_UINT8(DESK_MENU_ITEM_COUNT - 1u, desk_app_view(&app).menu_selection);
    desk_app_on_input(&app, DESK_INPUT_RIGHT, DESK_SPLASH_DURATION_MS + 3u, nullptr);
    TEST_ASSERT_EQUAL_UINT8(0u, desk_app_view(&app).menu_selection);

    desk_app_on_input(&app, DESK_INPUT_RIGHT, DESK_SPLASH_DURATION_MS + 4u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS + 5u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_HEALTH, desk_app_view(&app).screen);   // opcion 2: Salud
    desk_app_on_input(&app, DESK_INPUT_LEFT, DESK_SPLASH_DURATION_MS + 6u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_MENU, desk_app_view(&app).screen);

    desk_app_on_input(&app, DESK_INPUT_RIGHT, DESK_SPLASH_DURATION_MS + 7u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS + 8u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_GAME, desk_app_view(&app).screen);     // opcion 3: CogniGame
    TEST_ASSERT_EQUAL_INT(GAME_STATE_INTRO, desk_app_view(&app).game.state);
    desk_app_on_input(&app, DESK_INPUT_LEFT, DESK_SPLASH_DURATION_MS + 9u, nullptr);   // IZQ en la intro: volver
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_MENU, desk_app_view(&app).screen);
}

void test_cogni_game_round_runs_inside_the_desk_app(void)
{
    uint32_t t = DESK_SPLASH_DURATION_MS;
    desk_app_tick(&app, t, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, ++t, nullptr);      // IDLE -> MENU
    desk_app_on_input(&app, DESK_INPUT_RIGHT, ++t, nullptr);
    desk_app_on_input(&app, DESK_INPUT_RIGHT, ++t, nullptr);   // opcion 3
    desk_app_on_input(&app, DESK_INPUT_OK, ++t, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_GAME, desk_app_view(&app).screen);
    desk_app_on_input(&app, DESK_INPUT_OK, ++t, nullptr);      // empezar
    TEST_ASSERT_EQUAL_INT(GAME_STATE_QUESTION, desk_app_view(&app).game.state);

    for (uint8_t i = 0u; i < GAME_QUESTIONS_PER_ROUND; ++i) {
        const game_view_t v = desk_app_view(&app).game;
        TEST_ASSERT_EQUAL_INT(GAME_STATE_QUESTION, v.state);
        desk_app_on_input(&app, DESK_INPUT_LEFT, t += 100u, nullptr);   // responde A
        t += GAME_FEEDBACK_MS;
        desk_app_tick(&app, t, nullptr);                                // el aviso se cierra solo
    }
    TEST_ASSERT_EQUAL_INT(GAME_STATE_RESULT, desk_app_view(&app).game.state);
    TEST_ASSERT_EQUAL_UINT32(1u, desk_app_game_result(&app).revision);
    desk_app_on_input(&app, DESK_INPUT_OK, ++t, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_MENU, desk_app_view(&app).screen);
}

void test_fall_prompt_interrupts_the_game(void)
{
    uint32_t t = DESK_SPLASH_DURATION_MS;
    desk_app_tick(&app, t, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, ++t, nullptr);
    desk_app_on_input(&app, DESK_INPUT_RIGHT, ++t, nullptr);
    desk_app_on_input(&app, DESK_INPUT_RIGHT, ++t, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, ++t, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, ++t, nullptr);
    TEST_ASSERT_EQUAL_INT(GAME_STATE_QUESTION, desk_app_view(&app).game.state);

    alba_event_t event;
    uint16_t ref_seq = 0u;
    alba_pack_event(&event, 12u, t, ALBA_ALERTA_CAIDA_DETECTADA, 3900u, ALBA_CONF_PENDIENTE);
    desk_app_on_fall_event(&app, &event, t, &ref_seq);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_FALL_PROMPT, desk_app_view(&app).screen);
}

void test_clock_menu_edits_and_saves_three_alarm_fields(void)
{
    desk_app_tick(&app, DESK_SPLASH_DURATION_MS, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS, nullptr);
    desk_app_on_input(&app, DESK_INPUT_LEFT, DESK_SPLASH_DURATION_MS + 1u, nullptr);
    TEST_ASSERT_EQUAL_UINT8(3u, desk_app_view(&app).menu_selection);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS + 2u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_CLOCK, desk_app_view(&app).screen);
    TEST_ASSERT_FALSE(desk_app_view(&app).clock_valid);

    desk_app_on_input(&app, DESK_INPUT_RIGHT, DESK_SPLASH_DURATION_MS + 3u, nullptr);
    TEST_ASSERT_EQUAL_UINT8(1u, desk_app_view(&app).selected_alarm);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS + 4u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_ALARM_EDIT, desk_app_view(&app).screen);

    desk_app_on_input(&app, DESK_INPUT_RIGHT, DESK_SPLASH_DURATION_MS + 5u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS + 6u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_RIGHT, DESK_SPLASH_DURATION_MS + 7u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS + 8u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_RIGHT, DESK_SPLASH_DURATION_MS + 9u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS + 10u, nullptr);

    const desk_view_t view = desk_app_view(&app);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_CLOCK, view.screen);
    TEST_ASSERT_EQUAL_UINT8(9u, view.alarms[1].hour);
    TEST_ASSERT_EQUAL_UINT8(1u, view.alarms[1].minute);
    TEST_ASSERT_TRUE(view.alarms[1].enabled);
    TEST_ASSERT_EQUAL_UINT32(1u, desk_app_alarm_revision(&app));
}

void test_alarm_settings_wrap_and_invalid_values_are_ignored(void)
{
    desk_alarm_t alarm = {0u, 0u, false};
    desk_app_set_alarm(&app, 0u, &alarm);
    desk_app_tick(&app, DESK_SPLASH_DURATION_MS, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS, nullptr);
    desk_app_on_input(&app, DESK_INPUT_LEFT, DESK_SPLASH_DURATION_MS + 1u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS + 2u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, DESK_SPLASH_DURATION_MS + 3u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_LEFT, DESK_SPLASH_DURATION_MS + 4u, nullptr);
    TEST_ASSERT_EQUAL_UINT8(23u, desk_app_view(&app).alarms[0].hour);

    alarm.hour = 24u;
    desk_app_set_alarm(&app, 0u, &alarm);
    TEST_ASSERT_EQUAL_UINT8(23u, desk_app_view(&app).alarms[0].hour);
    desk_app_set_alarm(&app, DESK_ALARM_COUNT, &alarm);
    TEST_ASSERT_EQUAL_UINT8(23u, desk_app_view(&app).alarms[0].hour);
}

void test_fall_prompt_uses_ok_to_request_cancel(void)
{
    alba_event_t event;
    uint16_t ref_seq = 0u;
    alba_pack_event(&event, 12u, 1000u, ALBA_ALERTA_CAIDA_DETECTADA, 3900u,
                    ALBA_CONF_PENDIENTE);

    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SHOW_PROMPT,
                          desk_app_on_fall_event(&app, &event, 1000u, &ref_seq));
    TEST_ASSERT_EQUAL_UINT16(12u, ref_seq);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_FALL_PROMPT, desk_app_view(&app).screen);
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SEND_CANCEL,
                          desk_app_on_input(&app, DESK_INPUT_OK, 2000u, &ref_seq));
    TEST_ASSERT_EQUAL_UINT16(12u, ref_seq);
    TEST_ASSERT_EQUAL_INT(ROBOT_FALL_CANCELING, robot_fall_state(&app.fall));

    ref_seq = 0u;
    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SEND_CANCEL,
                          desk_app_tick(&app, 2400u, &ref_seq));
    TEST_ASSERT_EQUAL_UINT16(12u, ref_seq);
}

void test_confirmed_fall_is_visible_until_ok(void)
{
    alba_event_t event;
    alba_pack_event(&event, 13u, 21000u, ALBA_ALERTA_CAIDA_DETECTADA, 3900u,
                    ALBA_CONF_CONFIRMADA);

    TEST_ASSERT_EQUAL_INT(ROBOT_ACT_SHOW_HELP_CALLED,
                          desk_app_on_fall_event(&app, &event, 21000u, nullptr));
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_FALL_CONFIRMED, desk_app_view(&app).screen);
    desk_app_on_input(&app, DESK_INPUT_OK, 22000u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_IDLE, desk_app_view(&app).screen);
}

void test_medication_alarm_has_dedicated_screen_and_ok_acknowledges(void)
{
    TEST_ASSERT_TRUE(desk_app_show_medication_alarm(&app, 1u));
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_MEDICATION_ALERT, desk_app_view(&app).screen);
    TEST_ASSERT_EQUAL_UINT8(1u, desk_app_view(&app).medication_alarm_index);
    desk_app_on_input(&app, DESK_INPUT_OK, 1000u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_IDLE, desk_app_view(&app).screen);
}

void test_medication_alarm_does_not_interrupt_fall_prompt(void)
{
    alba_event_t event;
    alba_pack_event(&event, 12u, 1000u, ALBA_ALERTA_CAIDA_DETECTADA, 3900u,
                    ALBA_CONF_PENDIENTE);
    desk_app_on_fall_event(&app, &event, 1000u, nullptr);

    TEST_ASSERT_FALSE(desk_app_show_medication_alarm(&app, 0u));
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_FALL_PROMPT, desk_app_view(&app).screen);
}

static void go_idle(void)
{
    desk_app_tick(&app, DESK_SPLASH_DURATION_MS, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_IDLE, desk_app_view(&app).screen);
}

void test_wake_shows_good_morning_and_ok_starts_the_game_with_orientation(void)
{
    go_idle();
    desk_app_set_clock(&app, true, 7u, 30u);
    desk_app_set_date(&app, true, 1u, 9u, 11u);   // lunes 9 de noviembre
    TEST_ASSERT_TRUE(desk_app_on_wake(&app, 10000u));
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_WAKE, desk_app_view(&app).screen);

    desk_app_on_input(&app, DESK_INPUT_OK, 11000u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_GAME, desk_app_view(&app).screen);
    desk_app_on_input(&app, DESK_INPUT_OK, 11100u, nullptr);   // empezar
    const game_view_t v = desk_app_view(&app).game;
    TEST_ASSERT_EQUAL_INT(GAME_STATE_QUESTION, v.state);
    TEST_ASSERT_EQUAL_STRING("Lunes", v.question->options[v.question->correct]);
}

void test_wake_without_clock_still_offers_the_bank_questions(void)
{
    go_idle();
    TEST_ASSERT_TRUE(desk_app_on_wake(&app, 10000u));
    desk_app_on_input(&app, DESK_INPUT_OK, 11000u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, 11100u, nullptr);
    const game_view_t v = desk_app_view(&app).game;
    TEST_ASSERT_EQUAL_PTR(game_content_get(0u), v.question);
}

void test_wake_left_button_postpones_to_idle(void)
{
    go_idle();
    TEST_ASSERT_TRUE(desk_app_on_wake(&app, 10000u));
    desk_app_on_input(&app, DESK_INPUT_LEFT, 11000u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_IDLE, desk_app_view(&app).screen);
}

void test_wake_works_from_the_menu_and_other_calm_screens(void)
{
    go_idle();
    desk_app_on_input(&app, DESK_INPUT_OK, 6000u, nullptr);   // MENU
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_MENU, desk_app_view(&app).screen);
    TEST_ASSERT_TRUE(desk_app_on_wake(&app, 7000u));
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_WAKE, desk_app_view(&app).screen);
}

void test_repeated_wake_is_ignored_for_thirty_minutes(void)
{
    go_idle();
    TEST_ASSERT_TRUE(desk_app_on_wake(&app, 10000u));
    desk_app_on_input(&app, DESK_INPUT_LEFT, 11000u, nullptr);
    TEST_ASSERT_FALSE(desk_app_on_wake(&app, 12000u));
    TEST_ASSERT_FALSE(desk_app_on_wake(&app, 10000u + DESK_WAKE_COOLDOWN_MS - 1u));
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_IDLE, desk_app_view(&app).screen);
    TEST_ASSERT_TRUE(desk_app_on_wake(&app, 10000u + DESK_WAKE_COOLDOWN_MS));
}

void test_wake_does_not_interrupt_a_fall_or_medication_or_the_game(void)
{
    go_idle();
    alba_event_t event;
    alba_pack_event(&event, 12u, 1000u, ALBA_ALERTA_CAIDA_DETECTADA, 3900u, ALBA_CONF_PENDIENTE);
    desk_app_on_fall_event(&app, &event, 1000u, nullptr);
    TEST_ASSERT_FALSE(desk_app_on_wake(&app, 2000u));
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_FALL_PROMPT, desk_app_view(&app).screen);

    desk_app_init(&app, 0u);
    go_idle();
    TEST_ASSERT_TRUE(desk_app_show_medication_alarm(&app, 0u));
    TEST_ASSERT_FALSE(desk_app_on_wake(&app, 2000u));
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_MEDICATION_ALERT, desk_app_view(&app).screen);

    desk_app_init(&app, 0u);
    go_idle();
    desk_app_on_input(&app, DESK_INPUT_OK, 6000u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_RIGHT, 6100u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_RIGHT, 6200u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, 6300u, nullptr);   // juego
    TEST_ASSERT_FALSE(desk_app_on_wake(&app, 7000u));
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_GAME, desk_app_view(&app).screen);
}

void test_fall_interrupts_the_wake_screen(void)
{
    go_idle();
    TEST_ASSERT_TRUE(desk_app_on_wake(&app, 10000u));
    alba_event_t event;
    alba_pack_event(&event, 12u, 11000u, ALBA_ALERTA_CAIDA_DETECTADA, 3900u, ALBA_CONF_PENDIENTE);
    desk_app_on_fall_event(&app, &event, 11000u, nullptr);
    TEST_ASSERT_EQUAL_INT(DESK_SCREEN_FALL_PROMPT, desk_app_view(&app).screen);
}

void test_invalid_date_is_rejected(void)
{
    go_idle();
    desk_app_set_clock(&app, true, 7u, 30u);
    desk_app_set_date(&app, true, 9u, 9u, 11u);    // dia de semana inexistente
    TEST_ASSERT_TRUE(desk_app_on_wake(&app, 10000u));
    desk_app_on_input(&app, DESK_INPUT_OK, 11000u, nullptr);
    desk_app_on_input(&app, DESK_INPUT_OK, 11100u, nullptr);
    TEST_ASSERT_EQUAL_PTR(game_content_get(0u), desk_app_view(&app).game.question);   // sin orientacion
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_splash_transitions_to_idle_without_blocking);
    RUN_TEST(test_idle_eyes_blink_without_blocking);
    RUN_TEST(test_menu_navigation_opens_environment_and_feature_placeholder);
    RUN_TEST(test_cogni_game_round_runs_inside_the_desk_app);
    RUN_TEST(test_fall_prompt_interrupts_the_game);
    RUN_TEST(test_clock_menu_edits_and_saves_three_alarm_fields);
    RUN_TEST(test_alarm_settings_wrap_and_invalid_values_are_ignored);
    RUN_TEST(test_fall_prompt_uses_ok_to_request_cancel);
    RUN_TEST(test_confirmed_fall_is_visible_until_ok);
    RUN_TEST(test_medication_alarm_has_dedicated_screen_and_ok_acknowledges);
    RUN_TEST(test_medication_alarm_does_not_interrupt_fall_prompt);
    RUN_TEST(test_wake_shows_good_morning_and_ok_starts_the_game_with_orientation);
    RUN_TEST(test_wake_without_clock_still_offers_the_bank_questions);
    RUN_TEST(test_wake_left_button_postpones_to_idle);
    RUN_TEST(test_wake_works_from_the_menu_and_other_calm_screens);
    RUN_TEST(test_repeated_wake_is_ignored_for_thirty_minutes);
    RUN_TEST(test_wake_does_not_interrupt_a_fall_or_medication_or_the_game);
    RUN_TEST(test_fall_interrupts_the_wake_screen);
    RUN_TEST(test_invalid_date_is_rejected);
    return UNITY_END();
}
