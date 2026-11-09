#include <stdio.h>

#include "cogni_game.h"

static const char *const k_days[7] = {
    "Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"
};
static const char *const k_parts[3] = { "Manana", "Tarde", "Noche" };

bool game_clock_is_valid(const game_clock_t *c)
{
    return c != 0 && c->valid && c->wday < 7u && c->mday >= 1u && c->mday <= 31u &&
           c->month >= 1u && c->month <= 12u && c->hour < 24u;
}

static void set_line(game_dyn_text_t *t, game_question_t *q, uint8_t i, const char *s)
{
    (void)snprintf(t->lines[i], sizeof t->lines[i], "%s", s);
    q->lines[i] = t->lines[i];
}

/* Pone la respuesta correcta en la posicion `pos` y las dos falsas en las otras dos. */
static void place_options(game_dyn_text_t *t, game_question_t *q, uint8_t pos,
                          const char *right, const char *wrong1, const char *wrong2)
{
    const char *wrong[2] = { wrong1, wrong2 };
    uint8_t w = 0u;
    for (uint8_t i = 0u; i < GAME_OPTIONS; i++) {
        const char *s = (i == pos) ? right : wrong[w++];
        (void)snprintf(t->options[i], sizeof t->options[i], "%s", s);
        q->options[i] = t->options[i];
    }
    q->correct = pos;
}

bool game_orientation_build(const game_clock_t *c, uint8_t kind, uint8_t variant,
                            game_dyn_text_t *t, game_question_t *q)
{
    if (!game_clock_is_valid(c) || t == 0 || q == 0 || kind >= GAME_ORIENTATION_COUNT) return false;

    for (uint8_t i = 0u; i < GAME_QUESTION_LINES; i++) q->lines[i] = 0;
    const uint8_t pos = (uint8_t)((variant + kind) % GAME_OPTIONS);

    if (kind == 0u) {
        set_line(t, q, 0u, "Que dia de la semana");
        set_line(t, q, 1u, "es hoy?");
        place_options(t, q, pos, k_days[c->wday], k_days[(c->wday + 2u) % 7u],
                      k_days[(c->wday + 5u) % 7u]);
    } else if (kind == 1u) {
        char right[8], w1[8], w2[8];
        const uint8_t d1 = (c->mday > 2u) ? (uint8_t)(c->mday - 2u) : (uint8_t)(c->mday + 2u);
        const uint8_t d2 = (c->mday <= 27u) ? (uint8_t)(c->mday + 4u) : (uint8_t)(c->mday - 4u);
        (void)snprintf(right, sizeof right, "%u", (unsigned)c->mday);
        (void)snprintf(w1, sizeof w1, "%u", (unsigned)d1);
        (void)snprintf(w2, sizeof w2, "%u", (unsigned)d2);
        set_line(t, q, 0u, "Que fecha es hoy?");
        set_line(t, q, 1u, "(numero del dia)");
        place_options(t, q, pos, right, w1, w2);
    } else {
        const uint8_t part = (c->hour >= 5u && c->hour <= 11u) ? 0u
                           : (c->hour >= 12u && c->hour <= 18u) ? 1u : 2u;
        set_line(t, q, 0u, "Que momento del dia");
        set_line(t, q, 1u, "es ahora?");
        place_options(t, q, pos, k_parts[part], k_parts[(part + 1u) % 3u],
                      k_parts[(part + 2u) % 3u]);
    }
    return true;
}
