#ifndef DESK_APP_H
#define DESK_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "robot_fall.h"
#include "cogni_game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DESK_SPLASH_DURATION_MS 3000u
#define DESK_MENU_ITEM_COUNT    4u
#define DESK_ALARM_COUNT        3u
#define DESK_BLINK_INTERVAL_MS  3200u
#define DESK_BLINK_DURATION_MS  180u

typedef enum {
    DESK_INPUT_NONE = 0,
    DESK_INPUT_LEFT,
    DESK_INPUT_RIGHT,
    DESK_INPUT_OK
} desk_input_t;

typedef enum {
    DESK_SCREEN_SPLASH = 0,
    DESK_SCREEN_IDLE,
    DESK_SCREEN_MENU,
    DESK_SCREEN_FEATURE,
    DESK_SCREEN_ENVIRONMENT,
    DESK_SCREEN_CLOCK,
    DESK_SCREEN_ALARM_EDIT,
    DESK_SCREEN_MEDICATION_ALERT,
    DESK_SCREEN_FALL_PROMPT,
    DESK_SCREEN_FALL_CONFIRMED,
    DESK_SCREEN_HEALTH,
    DESK_SCREEN_GAME,
    DESK_SCREEN_WAKE,
    DESK_SCREEN_COMFORT
} desk_screen_t;

/* Despues de un aviso de despertar se ignoran los siguientes (el robot ya esta atendiendo al paciente). */
#define DESK_WAKE_COOLDOWN_MS (30ul * 60ul * 1000ul)

typedef struct {
    uint8_t hour;
    uint8_t minute;
    bool enabled;
} desk_alarm_t;

typedef struct {
    desk_screen_t screen;
    uint8_t menu_selection;
    bool eyes_closed;
    bool clock_valid;
    uint8_t clock_hour;
    uint8_t clock_minute;
    uint8_t selected_alarm;
    uint8_t alarm_edit_field;
    uint8_t medication_alarm_index;
    desk_alarm_t alarms[DESK_ALARM_COUNT];
    game_view_t game;
    uint8_t comfort_flags;     /* banderas COMFORT_* mostradas en DESK_SCREEN_COMFORT */
    int16_t comfort_temp_x10;
    uint8_t comfort_hum;
} desk_view_t;

typedef struct {
    desk_screen_t screen;
    uint8_t menu_selection;
    uint32_t started_ms;
    uint32_t next_blink_ms;
    uint32_t blink_ends_ms;
    bool eyes_closed;
    bool clock_valid;
    uint8_t clock_hour;
    uint8_t clock_minute;
    uint8_t selected_alarm;
    uint8_t alarm_edit_field;
    uint8_t medication_alarm_index;
    bool date_valid;
    uint8_t date_wday;     /* 0 = domingo */
    uint8_t date_mday;
    uint8_t date_month;    /* 1..12 */
    bool wake_seen;
    uint32_t wake_last_ms;
    uint8_t comfort_flags;
    int16_t comfort_temp_x10;
    uint8_t comfort_hum;
    uint32_t alarm_revision;
    desk_alarm_t alarms[DESK_ALARM_COUNT];
    robot_fall_t fall;
    cogni_game_t game;
} desk_app_t;

void desk_app_init(desk_app_t *app, uint32_t now_ms);
robot_action_t desk_app_tick(desk_app_t *app, uint32_t now_ms, uint16_t *ref_seq);
robot_action_t desk_app_on_input(desk_app_t *app, desk_input_t input, uint32_t now_ms,
                                 uint16_t *ref_seq);
robot_action_t desk_app_on_fall_event(desk_app_t *app, const alba_event_t *event,
                                      uint32_t now_ms, uint16_t *ref_seq);
bool desk_app_show_medication_alarm(desk_app_t *app, uint8_t alarm_index);
void desk_app_set_clock(desk_app_t *app, bool valid, uint8_t hour, uint8_t minute);
/* Fecha local (para las preguntas de orientacion del juego); wday: 0 = domingo. */
void desk_app_set_date(desk_app_t *app, bool valid, uint8_t wday, uint8_t mday, uint8_t month);
/* La pulsera detecto el despertar: muestra "Buenos dias", la hora del medicamento y ofrece el juego.
 * true si se mostro; false si se ignoro (hay una caida o alarma en curso, el juego ya corre, o hay
 * un despertar reciente). */
bool desk_app_on_wake(desk_app_t *app, uint32_t now_ms);
/* Semilla del sorteo de preguntas (por ejemplo esp_random() al arrancar). */
void desk_app_seed_game(desk_app_t *app, uint32_t seed);
/* Ambiente fuera del rango de confort: muestra el aviso (OK lo cierra). true si se mostro; false si
 * hay algo mas importante en pantalla (caida, medicamento, despertar, juego, edicion de alarma):
 * el llamador reintenta mas tarde. */
bool desk_app_show_comfort_alert(desk_app_t *app, uint8_t flags, int16_t temp_x10, uint8_t hum_pct);
void desk_app_set_alarm(desk_app_t *app, uint8_t index, const desk_alarm_t *alarm);
uint32_t desk_app_alarm_revision(const desk_app_t *app);
/* Resultado de la ultima ronda de CogniGame; result.revision cambia cada vez que termina una. */
game_result_t desk_app_game_result(const desk_app_t *app);
desk_view_t desk_app_view(const desk_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
