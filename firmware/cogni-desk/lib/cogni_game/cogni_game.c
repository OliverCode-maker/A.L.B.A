#include "cogni_game.h"

static uint8_t orientation_count(const cogni_game_t *g)
{
    return g->orientation_on ? (uint8_t)GAME_ORIENTATION_COUNT : 0u;
}

static uint32_t next_random(cogni_game_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    g->rng = x != 0u ? x : 0x9E3779B9u;
    return g->rng;
}

static void mix_seed(cogni_game_t *g, uint32_t seed)
{
    g->rng ^= seed * 2654435761u + 0x7F4A7C15u;
    if (g->rng == 0u) g->rng = 0x9E3779B9u;
    (void)next_random(g);
}

/* Elige al azar `m` preguntas distintas del banco, sin las de la ronda anterior si alcanzan. */
static void pick_bank_questions(cogni_game_t *g, uint8_t m)
{
    uint8_t count = game_content_count();
    if (count > GAME_BANK_MAX) count = (uint8_t)GAME_BANK_MAX;
    uint8_t cand[GAME_BANK_MAX];
    uint8_t n = 0u;
    for (uint8_t i = 0u; i < count; ++i) {
        bool used = false;
        for (uint8_t k = 0u; k < g->prev_count; ++k) {
            if (g->prev_picks[k] == i) used = true;
        }
        if (!used) cand[n++] = i;
    }
    if (n < m) {                       /* banco chico: se permite repetir las de la ronda anterior */
        n = 0u;
        for (uint8_t i = 0u; i < count; ++i) cand[n++] = i;
    }
    for (uint8_t i = 0u; i < m && i < n; ++i) {     /* Fisher-Yates parcial */
        const uint8_t j = (uint8_t)(i + next_random(g) % (uint32_t)(n - i));
        const uint8_t tmp = cand[i];
        cand[i] = cand[j];
        cand[j] = tmp;
        g->picks[i] = cand[i];
    }
    for (uint8_t i = 0u; i < GAME_QUESTIONS_PER_ROUND; ++i) {
        g->prev_picks[i] = i < m ? g->picks[i] : 0u;
    }
    g->prev_count = m < n ? m : n;
}

/* Pregunta actual: orientacion o banco, ambas armadas en el juego (el banco con opciones barajadas). */
static const game_question_t *current_question(const cogni_game_t *g)
{
    return g->q_number < orientation_count(g) ? &g->dyn_question : &g->bank_question;
}

static void prepare_question(cogni_game_t *g)
{
    if (g->q_number < orientation_count(g)) {
        if (!game_orientation_build(&g->clock, g->q_number, g->rounds_played, &g->dyn_text,
                                    &g->dyn_question)) {
            g->orientation_on = false;   /* el reloj dejo de servir: se sigue solo con el banco */
        }
        if (g->orientation_on) return;
    }

    const uint8_t slot = (uint8_t)(g->q_number - orientation_count(g));
    const game_question_t *src = game_content_get(g->picks[slot < GAME_QUESTIONS_PER_ROUND ? slot : 0u]);
    if (src == 0) src = game_content_get(0u);
    if (src == 0) return;

    /* Baraja las 3 opciones: la correcta cae en una posicion al azar. */
    uint8_t perm[GAME_OPTIONS] = {0u, 1u, 2u};
    for (uint8_t i = (uint8_t)(GAME_OPTIONS - 1u); i > 0u; --i) {
        const uint8_t j = (uint8_t)(next_random(g) % (uint32_t)(i + 1u));
        const uint8_t tmp = perm[i];
        perm[i] = perm[j];
        perm[j] = tmp;
    }
    for (uint8_t l = 0u; l < GAME_QUESTION_LINES; ++l) g->bank_question.lines[l] = src->lines[l];
    for (uint8_t o = 0u; o < GAME_OPTIONS; ++o) {
        g->bank_question.options[o] = src->options[perm[o]];
        if (perm[o] == src->correct) g->bank_question.correct = o;
    }
}

static uint32_t average_ms(const cogni_game_t *g)
{
    return g->answered == 0u ? 0u : g->total_resp_ms / g->answered;
}

static void advance(cogni_game_t *g, uint32_t now_ms)
{
    ++g->q_number;
    if (g->q_number >= GAME_QUESTIONS_PER_ROUND) {
        g->state = GAME_STATE_RESULT;
        ++g->rounds_played;
        ++g->results_revision;
    } else {
        g->state = GAME_STATE_QUESTION;
        g->q_shown_ms = now_ms;
        prepare_question(g);
    }
    ++g->revision;
}

void cogni_game_init(cogni_game_t *g)
{
    g->state = GAME_STATE_IDLE;
    g->rounds_played = 0u;
    g->rng = 0x2545F491u;
    g->prev_count = 0u;
    for (uint8_t i = 0u; i < GAME_QUESTIONS_PER_ROUND; ++i) {
        g->picks[i] = 0u;
        g->prev_picks[i] = 0u;
    }
    g->q_number = 0u;
    g->score = 0u;
    g->answered = 0u;
    g->last_choice = 0u;
    g->last_correct = false;
    g->q_shown_ms = 0u;
    g->feedback_started_ms = 0u;
    g->feedback_until_ms = 0u;
    g->total_resp_ms = 0u;
    g->revision = 0u;
    g->results_revision = 0u;
    g->orientation_on = false;
    g->clock.valid = false;
}

void cogni_game_seed(cogni_game_t *g, uint32_t seed)
{
    mix_seed(g, seed);
}

void cogni_game_start(cogni_game_t *g, uint32_t now_ms)
{
    cogni_game_start_with_clock(g, now_ms, 0);
}

void cogni_game_start_with_clock(cogni_game_t *g, uint32_t now_ms, const game_clock_t *clock)
{
    mix_seed(g, now_ms);   /* el momento en que se abre el juego tambien cambia el sorteo */
    g->orientation_on = game_clock_is_valid(clock);
    if (g->orientation_on) g->clock = *clock;
    /* Preguntas del banco al azar, distintas a las de la ronda anterior. */
    pick_bank_questions(g, (uint8_t)(GAME_QUESTIONS_PER_ROUND - orientation_count(g)));
    g->q_number = 0u;
    g->score = 0u;
    g->answered = 0u;
    g->total_resp_ms = 0u;
    g->state = GAME_STATE_INTRO;
    ++g->revision;
}

bool cogni_game_on_button(cogni_game_t *g, game_button_t button, uint32_t now_ms)
{
    switch (g->state) {
    case GAME_STATE_INTRO:
        if (button == GAME_BTN_A) {
            g->state = GAME_STATE_IDLE;
            return true;
        }
        if (button == GAME_BTN_B && game_content_count() > 0u) {
            g->state = GAME_STATE_QUESTION;
            g->q_number = 0u;
            g->q_shown_ms = now_ms;
            prepare_question(g);
            ++g->revision;
        }
        return false;

    case GAME_STATE_QUESTION: {
        const game_question_t *q = current_question(g);
        g->last_choice = (uint8_t)button;
        g->last_correct = (q != 0) && ((uint8_t)button == q->correct);
        if (g->last_correct) ++g->score;
        g->total_resp_ms += (uint32_t)(now_ms - g->q_shown_ms);
        ++g->answered;
        g->state = GAME_STATE_FEEDBACK;
        g->feedback_started_ms = now_ms;
        g->feedback_until_ms = now_ms + GAME_FEEDBACK_MS;
        ++g->revision;
        return false;
    }

    case GAME_STATE_FEEDBACK:
        if ((uint32_t)(now_ms - g->feedback_started_ms) >= GAME_FEEDBACK_MIN_MS) {
            advance(g, now_ms);
        }
        return false;

    case GAME_STATE_RESULT:
        g->state = GAME_STATE_IDLE;
        return true;

    default:
        return false;
    }
}

void cogni_game_tick(cogni_game_t *g, uint32_t now_ms)
{
    if (g->state == GAME_STATE_FEEDBACK && (int32_t)(now_ms - g->feedback_until_ms) >= 0) {
        advance(g, now_ms);
    }
}

game_view_t cogni_game_view(const cogni_game_t *g)
{
    game_view_t v;
    v.state = g->state;
    v.number = (uint8_t)(g->q_number + 1u);
    v.total = (uint8_t)GAME_QUESTIONS_PER_ROUND;
    v.score = g->score;
    v.last_correct = g->last_correct;
    v.last_choice = g->last_choice;
    v.question = (g->state == GAME_STATE_QUESTION || g->state == GAME_STATE_FEEDBACK)
        ? current_question(g)
        : 0;
    v.avg_ms = average_ms(g);
    v.revision = g->revision;
    return v;
}

game_result_t cogni_game_result(const cogni_game_t *g)
{
    game_result_t r;
    r.score = g->score;
    r.total = (uint8_t)GAME_QUESTIONS_PER_ROUND;
    r.avg_ms = average_ms(g);
    r.revision = g->results_revision;
    return r;
}
