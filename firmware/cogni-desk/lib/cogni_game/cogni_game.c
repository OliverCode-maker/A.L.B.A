#include "cogni_game.h"

static uint8_t current_pool_index(const cogni_game_t *g)
{
    const uint8_t count = game_content_count();
    if (count == 0u) return 0u;
    return (uint8_t)((g->pool_start + g->q_number) % count);
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
    }
    ++g->revision;
}

void cogni_game_init(cogni_game_t *g)
{
    g->state = GAME_STATE_IDLE;
    g->rounds_played = 0u;
    g->pool_start = 0u;
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
}

void cogni_game_start(cogni_game_t *g, uint32_t now_ms)
{
    (void)now_ms;
    const uint8_t count = game_content_count();
    /* Cada ronda empieza en una pregunta distinta del banco. */
    g->pool_start = count == 0u
        ? 0u
        : (uint8_t)(((uint32_t)g->rounds_played * GAME_QUESTIONS_PER_ROUND) % count);
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
            ++g->revision;
        }
        return false;

    case GAME_STATE_QUESTION: {
        const game_question_t *q = game_content_get(current_pool_index(g));
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
        ? game_content_get(current_pool_index(g))
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
