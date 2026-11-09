#include "DeskApp.h"

static void start_game(desk_app_t *app, uint32_t now_ms)
{
    game_clock_t clock;
    clock.valid = app->clock_valid && app->date_valid;
    clock.wday = app->date_wday;
    clock.mday = app->date_mday;
    clock.month = app->date_month;
    clock.hour = app->clock_hour;
    cogni_game_start_with_clock(&app->game, now_ms, &clock);
    app->screen = DESK_SCREEN_GAME;
}

static void apply_fall_action(desk_app_t *app, robot_action_t action)
{
    switch (action) {
    case ROBOT_ACT_SHOW_PROMPT:
        app->screen = DESK_SCREEN_FALL_PROMPT;
        break;
    case ROBOT_ACT_SHOW_HELP_CALLED:
        app->screen = DESK_SCREEN_FALL_CONFIRMED;
        break;
    case ROBOT_ACT_HIDE_PROMPT:
        app->screen = DESK_SCREEN_IDLE;
        break;
    default:
        break;
    }
}

void desk_app_init(desk_app_t *app, uint32_t now_ms)
{
    app->screen = DESK_SCREEN_SPLASH;
    app->menu_selection = 0u;
    app->started_ms = now_ms;
    app->eyes_closed = false;
    app->next_blink_ms = 0u;
    app->blink_ends_ms = 0u;
    app->clock_valid = false;
    app->clock_hour = 0u;
    app->clock_minute = 0u;
    app->date_valid = false;
    app->date_wday = 0u;
    app->date_mday = 1u;
    app->date_month = 1u;
    app->wake_seen = false;
    app->wake_last_ms = 0u;
    app->selected_alarm = 0u;
    app->alarm_edit_field = 0u;
    app->medication_alarm_index = 0u;
    app->alarm_revision = 0u;
    for (uint8_t i = 0u; i < DESK_ALARM_COUNT; ++i) {
        app->alarms[i].hour = 8u;
        app->alarms[i].minute = 0u;
        app->alarms[i].enabled = false;
    }
    robot_fall_init(&app->fall);
    cogni_game_init(&app->game);
}

robot_action_t desk_app_tick(desk_app_t *app, uint32_t now_ms, uint16_t *ref_seq)
{
    if (app->screen == DESK_SCREEN_SPLASH &&
        (uint32_t)(now_ms - app->started_ms) >= DESK_SPLASH_DURATION_MS) {
        app->screen = DESK_SCREEN_IDLE;
        app->next_blink_ms = now_ms + DESK_BLINK_INTERVAL_MS;
        app->eyes_closed = false;
    }

    if (app->screen == DESK_SCREEN_IDLE) {
        if (app->eyes_closed && (int32_t)(now_ms - app->blink_ends_ms) >= 0) {
            app->eyes_closed = false;
            app->next_blink_ms = now_ms + DESK_BLINK_INTERVAL_MS;
        } else if (!app->eyes_closed && (int32_t)(now_ms - app->next_blink_ms) >= 0) {
            app->eyes_closed = true;
            app->blink_ends_ms = now_ms + DESK_BLINK_DURATION_MS;
        }
    }

    if (app->screen == DESK_SCREEN_GAME) {
        cogni_game_tick(&app->game, now_ms);
    }

    if (robot_fall_state(&app->fall) == ROBOT_FALL_IDLE) {
        return ROBOT_ACT_NONE;
    }

    const robot_action_t action = robot_fall_tick(&app->fall, now_ms, ref_seq);
    apply_fall_action(app, action);
    return action;
}

robot_action_t desk_app_on_input(desk_app_t *app, desk_input_t input, uint32_t now_ms,
                                 uint16_t *ref_seq)
{
    if (input == DESK_INPUT_NONE) return ROBOT_ACT_NONE;

    const robot_fall_state_t fall_state = robot_fall_state(&app->fall);
    if (fall_state != ROBOT_FALL_IDLE) {
        if (input != DESK_INPUT_OK) return ROBOT_ACT_NONE;
        return robot_fall_on_button_ok(&app->fall, now_ms, ref_seq);
    }

    if (app->screen == DESK_SCREEN_FALL_CONFIRMED) {
        if (input == DESK_INPUT_OK) app->screen = DESK_SCREEN_IDLE;
        return ROBOT_ACT_NONE;
    }

    switch (app->screen) {
    case DESK_SCREEN_MEDICATION_ALERT:
        if (input == DESK_INPUT_OK) app->screen = DESK_SCREEN_IDLE;
        break;
    case DESK_SCREEN_WAKE:
        if (input == DESK_INPUT_OK) {
            start_game(app, now_ms);                 /* OK: empezar la estimulacion */
        } else if (input == DESK_INPUT_LEFT) {
            app->screen = DESK_SCREEN_IDLE;          /* IZQ: mas tarde */
        }
        break;
    case DESK_SCREEN_IDLE:
        if (input == DESK_INPUT_OK) app->screen = DESK_SCREEN_MENU;
        break;
    case DESK_SCREEN_MENU:
        if (input == DESK_INPUT_LEFT) {
            app->menu_selection = (uint8_t)((app->menu_selection + DESK_MENU_ITEM_COUNT - 1u) %
                                            DESK_MENU_ITEM_COUNT);
        } else if (input == DESK_INPUT_RIGHT) {
            app->menu_selection = (uint8_t)((app->menu_selection + 1u) % DESK_MENU_ITEM_COUNT);
        } else if (input == DESK_INPUT_OK) {
            if (app->menu_selection == 0u) {
                app->screen = DESK_SCREEN_ENVIRONMENT;
            } else if (app->menu_selection == 1u) {
                app->screen = DESK_SCREEN_HEALTH;
            } else if (app->menu_selection == 2u) {
                start_game(app, now_ms);
            } else if (app->menu_selection == 3u) {
                app->screen = DESK_SCREEN_CLOCK;
            } else {
                app->screen = DESK_SCREEN_FEATURE;
            }
        }
        break;
    case DESK_SCREEN_ENVIRONMENT:
    case DESK_SCREEN_HEALTH:
    case DESK_SCREEN_FEATURE:
        if (input == DESK_INPUT_LEFT || input == DESK_INPUT_OK) {
            app->screen = DESK_SCREEN_MENU;
        }
        break;
    case DESK_SCREEN_GAME: {
        const game_button_t button = input == DESK_INPUT_LEFT  ? GAME_BTN_A
                                   : input == DESK_INPUT_RIGHT ? GAME_BTN_C
                                                               : GAME_BTN_B;
        if (cogni_game_on_button(&app->game, button, now_ms)) {
            app->screen = DESK_SCREEN_MENU;
        }
        break;
    }
    case DESK_SCREEN_CLOCK:
        if (input == DESK_INPUT_LEFT) {
            app->screen = DESK_SCREEN_MENU;
        } else if (input == DESK_INPUT_RIGHT) {
            app->selected_alarm =
                (uint8_t)((app->selected_alarm + 1u) % DESK_ALARM_COUNT);
        } else if (input == DESK_INPUT_OK) {
            app->alarm_edit_field = 0u;
            app->screen = DESK_SCREEN_ALARM_EDIT;
        }
        break;
    case DESK_SCREEN_ALARM_EDIT:
        if (app->alarm_edit_field == 0u) {
            if (input == DESK_INPUT_LEFT) {
                app->alarms[app->selected_alarm].hour =
                    (uint8_t)((app->alarms[app->selected_alarm].hour + 23u) % 24u);
            } else if (input == DESK_INPUT_RIGHT) {
                app->alarms[app->selected_alarm].hour =
                    (uint8_t)((app->alarms[app->selected_alarm].hour + 1u) % 24u);
            } else if (input == DESK_INPUT_OK) {
                app->alarm_edit_field = 1u;
            }
        } else if (app->alarm_edit_field == 1u) {
            if (input == DESK_INPUT_LEFT) {
                app->alarms[app->selected_alarm].minute =
                    (uint8_t)((app->alarms[app->selected_alarm].minute + 59u) % 60u);
            } else if (input == DESK_INPUT_RIGHT) {
                app->alarms[app->selected_alarm].minute =
                    (uint8_t)((app->alarms[app->selected_alarm].minute + 1u) % 60u);
            } else if (input == DESK_INPUT_OK) {
                app->alarm_edit_field = 2u;
            }
        } else if (input == DESK_INPUT_LEFT || input == DESK_INPUT_RIGHT) {
            app->alarms[app->selected_alarm].enabled =
                !app->alarms[app->selected_alarm].enabled;
        } else if (input == DESK_INPUT_OK) {
            ++app->alarm_revision;
            app->screen = DESK_SCREEN_CLOCK;
        }
        break;
    default:
        break;
    }

    return ROBOT_ACT_NONE;
}

robot_action_t desk_app_on_fall_event(desk_app_t *app, const alba_event_t *event,
                                      uint32_t now_ms, uint16_t *ref_seq)
{
    const robot_action_t action = robot_fall_on_event(&app->fall, event, now_ms, ref_seq);
    apply_fall_action(app, action);
    return action;
}

bool desk_app_show_medication_alarm(desk_app_t *app, uint8_t alarm_index)
{
    if (robot_fall_state(&app->fall) != ROBOT_FALL_IDLE ||
        app->screen == DESK_SCREEN_FALL_CONFIRMED ||
        app->screen == DESK_SCREEN_MEDICATION_ALERT ||
        alarm_index >= DESK_ALARM_COUNT) {
        return false;
    }

    app->medication_alarm_index = alarm_index;
    app->screen = DESK_SCREEN_MEDICATION_ALERT;
    return true;
}

void desk_app_set_clock(desk_app_t *app, bool valid, uint8_t hour, uint8_t minute)
{
    app->clock_valid = valid;
    app->clock_hour = hour < 24u ? hour : 0u;
    app->clock_minute = minute < 60u ? minute : 0u;
}

void desk_app_set_date(desk_app_t *app, bool valid, uint8_t wday, uint8_t mday, uint8_t month)
{
    app->date_valid = valid && wday < 7u && mday >= 1u && mday <= 31u && month >= 1u && month <= 12u;
    if (app->date_valid) {
        app->date_wday = wday;
        app->date_mday = mday;
        app->date_month = month;
    }
}

bool desk_app_on_wake(desk_app_t *app, uint32_t now_ms)
{
    if (app->wake_seen && (uint32_t)(now_ms - app->wake_last_ms) < DESK_WAKE_COOLDOWN_MS) {
        return false;
    }
    if (robot_fall_state(&app->fall) != ROBOT_FALL_IDLE) return false;

    switch (app->screen) {
    case DESK_SCREEN_IDLE:
    case DESK_SCREEN_MENU:
    case DESK_SCREEN_FEATURE:
    case DESK_SCREEN_ENVIRONMENT:
    case DESK_SCREEN_CLOCK:
    case DESK_SCREEN_HEALTH:
        break;
    default:
        return false;   /* caida, medicamento, edicion de alarma, juego o aviso ya en pantalla */
    }

    app->wake_seen = true;
    app->wake_last_ms = now_ms;
    app->screen = DESK_SCREEN_WAKE;
    return true;
}

void desk_app_set_alarm(desk_app_t *app, uint8_t index, const desk_alarm_t *alarm)
{
    if (index >= DESK_ALARM_COUNT || alarm == 0 ||
        alarm->hour >= 24u || alarm->minute >= 60u) {
        return;
    }
    app->alarms[index] = *alarm;
}

uint32_t desk_app_alarm_revision(const desk_app_t *app)
{
    return app->alarm_revision;
}

game_result_t desk_app_game_result(const desk_app_t *app)
{
    return cogni_game_result(&app->game);
}

desk_view_t desk_app_view(const desk_app_t *app)
{
    desk_view_t view = {0};
    view.screen = app->screen;
    view.menu_selection = app->menu_selection;
    view.eyes_closed = app->eyes_closed;
    view.clock_valid = app->clock_valid;
    view.clock_hour = app->clock_hour;
    view.clock_minute = app->clock_minute;
    view.selected_alarm = app->selected_alarm;
    view.alarm_edit_field = app->alarm_edit_field;
    view.medication_alarm_index = app->medication_alarm_index;
    view.game = cogni_game_view(&app->game);
    for (uint8_t i = 0u; i < DESK_ALARM_COUNT; ++i) {
        view.alarms[i] = app->alarms[i];
    }
    return view;
}
