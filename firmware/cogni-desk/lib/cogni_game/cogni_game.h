#ifndef COGNI_GAME_H
#define COGNI_GAME_H

/* CogniGame: preguntas de opcion multiple para el paciente (C puro, sin Arduino).
 * Tres botones = tres opciones: izquierdo (A), centro/OK (B), derecho (C).
 * No penaliza ni limita el tiempo: mide cuanto tarda en responder para el seguimiento. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_QUESTIONS_PER_ROUND 5u
#define GAME_OPTIONS             3u
#define GAME_QUESTION_LINES      4u
#define GAME_LINE_MAX            21u   /* caracteres por linea en la OLED (tamano 1)      */
#define GAME_OPTION_MAX          17u   /* 21 menos el prefijo "IZQ ", "OK  " o "DER "      */
#define GAME_FEEDBACK_MS         2500u /* cuanto se muestra "Correcto"/"Casi" si no pulsa  */
#define GAME_FEEDBACK_MIN_MS     600u  /* ignora pulsaciones accidentales al responder     */

typedef struct {
    const char *lines[GAME_QUESTION_LINES];   /* texto ya partido en lineas; NULL = sin usar */
    const char *options[GAME_OPTIONS];
    uint8_t correct;                          /* 0 = A (izq), 1 = B (OK), 2 = C (der)        */
} game_question_t;

/* Fecha y hora local para las preguntas de orientacion (dia, fecha y momento del dia). */
typedef struct {
    bool valid;
    uint8_t wday;    /* 0 = domingo ... 6 = sabado */
    uint8_t mday;    /* dia del mes, 1..31 */
    uint8_t month;   /* 1..12 */
    uint8_t hour;    /* 0..23 */
} game_clock_t;

#define GAME_ORIENTATION_COUNT 3u   /* dia de la semana, fecha y momento del dia */

/* Texto de una pregunta de orientacion armada al vuelo (las lineas de game_question_t apuntan aqui). */
typedef struct {
    char lines[GAME_QUESTION_LINES][GAME_LINE_MAX + 1u];
    char options[GAME_OPTIONS][GAME_OPTION_MAX + 1u];
} game_dyn_text_t;

bool game_clock_is_valid(const game_clock_t *clock);
/* Arma la pregunta de orientacion `kind` (0 dia de la semana, 1 fecha, 2 momento del dia).
 * `variant` mueve la posicion de la respuesta correcta entre rondas. false si el reloj no es valido. */
bool game_orientation_build(const game_clock_t *clock, uint8_t kind, uint8_t variant,
                            game_dyn_text_t *text, game_question_t *question);

/* Banco de preguntas (game_content.c). */
uint8_t game_content_count(void);
const game_question_t *game_content_get(uint8_t index);

typedef enum {
    GAME_STATE_IDLE = 0,
    GAME_STATE_INTRO,
    GAME_STATE_QUESTION,
    GAME_STATE_FEEDBACK,
    GAME_STATE_RESULT
} game_state_t;

typedef enum {
    GAME_BTN_A = 0,   /* izquierdo */
    GAME_BTN_B,       /* centro (OK) */
    GAME_BTN_C        /* derecho */
} game_button_t;

typedef struct {
    game_state_t state;
    uint8_t rounds_played;
    uint8_t pool_start;
    uint8_t q_number;          /* 0..GAME_QUESTIONS_PER_ROUND-1 */
    uint8_t score;
    uint8_t answered;
    uint8_t last_choice;
    bool last_correct;
    uint32_t q_shown_ms;
    uint32_t feedback_started_ms;
    uint32_t feedback_until_ms;
    uint32_t total_resp_ms;
    uint32_t revision;         /* sube con cada cambio visible: sirve para saber cuando redibujar */
    uint32_t results_revision; /* sube cada vez que termina una ronda */
    bool orientation_on;       /* esta ronda empieza con preguntas de orientacion */
    game_clock_t clock;
    game_question_t dyn_question;
    game_dyn_text_t dyn_text;
} cogni_game_t;

/* Lo que necesita la pantalla. */
typedef struct {
    game_state_t state;
    uint8_t number;            /* pregunta actual, 1..total */
    uint8_t total;
    uint8_t score;
    bool last_correct;
    uint8_t last_choice;
    const game_question_t *question;   /* NULL fuera de PREGUNTA/RETROALIMENTACION */
    uint32_t avg_ms;
    uint32_t revision;
} game_view_t;

/* Resultado de la ultima ronda terminada (para enviarlo a la nube). */
typedef struct {
    uint8_t score;
    uint8_t total;
    uint32_t avg_ms;
    uint32_t revision;         /* 0 = todavia no se termino ninguna ronda */
} game_result_t;

void cogni_game_init(cogni_game_t *g);
void cogni_game_start(cogni_game_t *g, uint32_t now_ms);                 /* abre la pantalla de inicio */
/* Igual, pero si `clock` es valido la ronda arranca con GAME_ORIENTATION_COUNT preguntas de
 * orientacion (dia, fecha, momento del dia) y el resto del banco de preguntas personales. */
void cogni_game_start_with_clock(cogni_game_t *g, uint32_t now_ms, const game_clock_t *clock);
/* Devuelve true si el jugador pidio salir del juego (volver al menu). */
bool cogni_game_on_button(cogni_game_t *g, game_button_t button, uint32_t now_ms);
void cogni_game_tick(cogni_game_t *g, uint32_t now_ms);
game_view_t cogni_game_view(const cogni_game_t *g);
game_result_t cogni_game_result(const cogni_game_t *g);

#ifdef __cplusplus
}
#endif

#endif
