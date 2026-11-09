#include <unity.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "cogni_game.h"
}

static cogni_game_t g;

void setUp(void) { cogni_game_init(&g); }
void tearDown(void) {}

/* Responde la pregunta actual con la opcion correcta (o una incorrecta) y avanza. */
static uint32_t answer(uint32_t t, bool correct)
{
    const game_view_t v = cogni_game_view(&g);
    const uint8_t right = v.question->correct;
    const uint8_t pick = correct ? right : (uint8_t)((right + 1u) % GAME_OPTIONS);
    cogni_game_on_button(&g, (game_button_t)pick, t);
    t += GAME_FEEDBACK_MS;
    cogni_game_tick(&g, t);
    return t;
}

void test_content_respects_oled_limits(void)
{
    TEST_ASSERT_TRUE(game_content_count() >= GAME_QUESTIONS_PER_ROUND);
    for (uint8_t i = 0u; i < game_content_count(); ++i) {
        const game_question_t *q = game_content_get(i);
        TEST_ASSERT_NOT_NULL(q);
        TEST_ASSERT_NOT_NULL(q->lines[0]);
        for (uint8_t l = 0u; l < GAME_QUESTION_LINES; ++l) {
            if (q->lines[l] != 0) TEST_ASSERT_TRUE(strlen(q->lines[l]) <= GAME_LINE_MAX);
        }
        for (uint8_t o = 0u; o < GAME_OPTIONS; ++o) {
            TEST_ASSERT_NOT_NULL(q->options[o]);
            TEST_ASSERT_TRUE(strlen(q->options[o]) <= GAME_OPTION_MAX);
            TEST_ASSERT_TRUE(strlen(q->options[o]) > 0u);
        }
        TEST_ASSERT_TRUE(q->correct < GAME_OPTIONS);
    }
    TEST_ASSERT_NULL(game_content_get(game_content_count()));
}

void test_content_has_no_accents_the_oled_font_cannot_draw(void)
{
    for (uint8_t i = 0u; i < game_content_count(); ++i) {
        const game_question_t *q = game_content_get(i);
        for (uint8_t l = 0u; l < GAME_QUESTION_LINES; ++l) {
            if (q->lines[l] == 0) continue;
            for (const char *c = q->lines[l]; *c; ++c) TEST_ASSERT_TRUE((unsigned char)*c < 128u);
        }
        for (uint8_t o = 0u; o < GAME_OPTIONS; ++o)
            for (const char *c = q->options[o]; *c; ++c) TEST_ASSERT_TRUE((unsigned char)*c < 128u);
    }
}

void test_intro_then_question_then_feedback(void)
{
    TEST_ASSERT_EQUAL_INT(GAME_STATE_IDLE, cogni_game_view(&g).state);
    cogni_game_start(&g, 0u);
    TEST_ASSERT_EQUAL_INT(GAME_STATE_INTRO, cogni_game_view(&g).state);
    TEST_ASSERT_NULL(cogni_game_view(&g).question);

    TEST_ASSERT_FALSE(cogni_game_on_button(&g, GAME_BTN_B, 100u));
    game_view_t v = cogni_game_view(&g);
    TEST_ASSERT_EQUAL_INT(GAME_STATE_QUESTION, v.state);
    TEST_ASSERT_EQUAL_UINT8(1u, v.number);
    TEST_ASSERT_NOT_NULL(v.question);

    cogni_game_on_button(&g, (game_button_t)v.question->correct, 3100u);
    v = cogni_game_view(&g);
    TEST_ASSERT_EQUAL_INT(GAME_STATE_FEEDBACK, v.state);
    TEST_ASSERT_TRUE(v.last_correct);
    TEST_ASSERT_EQUAL_UINT8(1u, v.score);
    TEST_ASSERT_EQUAL_UINT32(3000u, v.avg_ms);       /* tardo 3 s en responder */
}

void test_intro_left_button_exits(void)
{
    cogni_game_start(&g, 0u);
    TEST_ASSERT_TRUE(cogni_game_on_button(&g, GAME_BTN_A, 10u));
    TEST_ASSERT_EQUAL_INT(GAME_STATE_IDLE, cogni_game_view(&g).state);
}

void test_wrong_answer_does_not_score_and_shows_the_correct_one(void)
{
    cogni_game_start(&g, 0u);
    cogni_game_on_button(&g, GAME_BTN_B, 0u);
    const uint8_t right = cogni_game_view(&g).question->correct;
    cogni_game_on_button(&g, (game_button_t)((right + 1u) % GAME_OPTIONS), 1000u);
    const game_view_t v = cogni_game_view(&g);
    TEST_ASSERT_EQUAL_INT(GAME_STATE_FEEDBACK, v.state);
    TEST_ASSERT_FALSE(v.last_correct);
    TEST_ASSERT_EQUAL_UINT8(0u, v.score);
    TEST_ASSERT_NOT_NULL(v.question);      /* la pantalla necesita la pregunta para mostrar la respuesta */
}

void test_feedback_closes_by_itself_or_by_button_but_not_by_accident(void)
{
    cogni_game_start(&g, 0u);
    cogni_game_on_button(&g, GAME_BTN_B, 0u);
    cogni_game_on_button(&g, GAME_BTN_A, 1000u);                       /* respondio */
    cogni_game_on_button(&g, GAME_BTN_A, 1000u + GAME_FEEDBACK_MIN_MS - 1u);   /* doble pulsacion: se ignora */
    TEST_ASSERT_EQUAL_INT(GAME_STATE_FEEDBACK, cogni_game_view(&g).state);
    cogni_game_on_button(&g, GAME_BTN_A, 1000u + GAME_FEEDBACK_MIN_MS);        /* pulsacion intencional */
    TEST_ASSERT_EQUAL_INT(GAME_STATE_QUESTION, cogni_game_view(&g).state);
    TEST_ASSERT_EQUAL_UINT8(2u, cogni_game_view(&g).number);

    cogni_game_on_button(&g, GAME_BTN_A, 5000u);                       /* segunda respuesta */
    cogni_game_tick(&g, 5000u + GAME_FEEDBACK_MS - 1u);
    TEST_ASSERT_EQUAL_INT(GAME_STATE_FEEDBACK, cogni_game_view(&g).state);
    cogni_game_tick(&g, 5000u + GAME_FEEDBACK_MS);                     /* se cierra sola */
    TEST_ASSERT_EQUAL_UINT8(3u, cogni_game_view(&g).number);
}

void test_full_round_scores_and_reports_result(void)
{
    cogni_game_start(&g, 0u);
    cogni_game_on_button(&g, GAME_BTN_B, 0u);
    uint32_t t = 0u;
    for (uint8_t i = 0u; i < GAME_QUESTIONS_PER_ROUND; ++i) {
        TEST_ASSERT_EQUAL_INT(GAME_STATE_QUESTION, cogni_game_view(&g).state);
        t = answer(t + 2000u, i < 4u);          /* 4 aciertos y 1 error */
    }
    const game_view_t v = cogni_game_view(&g);
    TEST_ASSERT_EQUAL_INT(GAME_STATE_RESULT, v.state);
    TEST_ASSERT_EQUAL_UINT8(4u, v.score);

    const game_result_t r = cogni_game_result(&g);
    TEST_ASSERT_EQUAL_UINT8(4u, r.score);
    TEST_ASSERT_EQUAL_UINT8(GAME_QUESTIONS_PER_ROUND, r.total);
    TEST_ASSERT_EQUAL_UINT32(1u, r.revision);

    TEST_ASSERT_TRUE(cogni_game_on_button(&g, GAME_BTN_B, t + 10u));   /* OK en resultados: salir */
    TEST_ASSERT_EQUAL_INT(GAME_STATE_IDLE, cogni_game_view(&g).state);
    TEST_ASSERT_EQUAL_UINT32(1u, cogni_game_result(&g).revision);       /* el resultado se conserva */
}

/* Indice de la pregunta del banco que se esta mostrando (-1 si no es del banco). */
static bool same_text(const char *a, const char *b)
{
    if (a == 0 || b == 0) return a == b;
    return strcmp(a, b) == 0;
}

/* Las lineas se comparan por texto: el compilador puede unir literales iguales ("Como se llama"). */
static int bank_index_of(const game_question_t *q)
{
    for (uint8_t i = 0u; i < game_content_count(); ++i) {
        const game_question_t *b = game_content_get(i);
        bool same = true;
        for (uint8_t l = 0u; l < GAME_QUESTION_LINES; ++l) same = same && same_text(b->lines[l], q->lines[l]);
        if (same) return (int)i;
    }
    return -1;
}

void test_bank_questions_are_all_different(void)
{
    for (uint8_t i = 0u; i < game_content_count(); ++i) {
        TEST_ASSERT_EQUAL_INT((int)i, bank_index_of(game_content_get(i)));
    }
}

/* Juega una ronda completa respondiendo bien; devuelve los indices del banco que salieron. */
static uint32_t play_round(uint32_t t, int *seen, unsigned *n_seen)
{
    cogni_game_start(&g, t);
    cogni_game_on_button(&g, GAME_BTN_B, t);
    *n_seen = 0u;
    for (uint8_t i = 0u; i < GAME_QUESTIONS_PER_ROUND; ++i) {
        const int idx = bank_index_of(cogni_game_view(&g).question);
        if (idx >= 0) seen[(*n_seen)++] = idx;
        t = answer(t + 1000u, true);
    }
    cogni_game_on_button(&g, GAME_BTN_B, t + 1u);   /* salir */
    return t + 2u;
}

void test_bank_has_at_least_twenty_autobiographical_questions(void)
{
    TEST_ASSERT_TRUE(game_content_count() >= 20u);
    TEST_ASSERT_TRUE(game_content_count() <= GAME_BANK_MAX);
    for (uint8_t i = 0u; i < game_content_count(); ++i) {
        const game_question_t *q = game_content_get(i);
        TEST_ASSERT_NOT_EQUAL(0, strcmp(q->options[0], q->options[1]));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(q->options[0], q->options[2]));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(q->options[1], q->options[2]));
    }
}

void test_a_round_never_repeats_a_question(void)
{
    for (uint32_t seed = 1u; seed <= 40u; ++seed) {
        cogni_game_init(&g);
        cogni_game_seed(&g, seed);
        int seen[GAME_QUESTIONS_PER_ROUND];
        unsigned n = 0u;
        play_round(1000u, seen, &n);
        TEST_ASSERT_EQUAL_UINT(GAME_QUESTIONS_PER_ROUND, n);
        for (unsigned a = 0u; a < n; ++a)
            for (unsigned b = a + 1u; b < n; ++b) TEST_ASSERT_NOT_EQUAL(seen[a], seen[b]);
    }
}

void test_consecutive_rounds_do_not_repeat_questions(void)
{
    for (uint32_t seed = 1u; seed <= 40u; ++seed) {
        cogni_game_init(&g);
        cogni_game_seed(&g, seed);
        int first[GAME_QUESTIONS_PER_ROUND], second[GAME_QUESTIONS_PER_ROUND];
        unsigned n1 = 0u, n2 = 0u;
        uint32_t t = play_round(1000u, first, &n1);
        play_round(t, second, &n2);
        for (unsigned a = 0u; a < n1; ++a)
            for (unsigned b = 0u; b < n2; ++b) TEST_ASSERT_NOT_EQUAL(first[a], second[b]);
    }
}

void test_different_seeds_give_different_questions(void)
{
    bool used[GAME_BANK_MAX] = {false};
    unsigned distinct = 0u;
    for (uint32_t seed = 1u; seed <= 60u; ++seed) {
        cogni_game_init(&g);
        cogni_game_seed(&g, seed);
        cogni_game_start(&g, 1000u);
        cogni_game_on_button(&g, GAME_BTN_B, 1000u);
        const int idx = bank_index_of(cogni_game_view(&g).question);
        TEST_ASSERT_TRUE(idx >= 0);
        if (!used[idx]) { used[idx] = true; distinct++; }
    }
    TEST_ASSERT_TRUE(distinct >= 10u);   /* la primera pregunta no es siempre la misma */
}

void test_every_question_gets_asked_over_many_rounds(void)
{
    bool asked[GAME_BANK_MAX] = {false};
    cogni_game_init(&g);
    cogni_game_seed(&g, 12345u);
    uint32_t t = 1000u;
    for (unsigned r = 0u; r < 40u; ++r) {
        int seen[GAME_QUESTIONS_PER_ROUND];
        unsigned n = 0u;
        t = play_round(t, seen, &n);
        for (unsigned i = 0u; i < n; ++i) asked[seen[i]] = true;
    }
    for (uint8_t i = 0u; i < game_content_count(); ++i) TEST_ASSERT_TRUE(asked[i]);
}

void test_options_are_shuffled_but_the_right_answer_stays_right(void)
{
    bool pos_seen[GAME_OPTIONS] = {false, false, false};
    for (uint32_t seed = 1u; seed <= 80u; ++seed) {
        cogni_game_init(&g);
        cogni_game_seed(&g, seed);
        cogni_game_start(&g, 1000u + seed);
        cogni_game_on_button(&g, GAME_BTN_B, 1000u);
        const game_question_t *shown = cogni_game_view(&g).question;
        const int idx = bank_index_of(shown);
        TEST_ASSERT_TRUE(idx >= 0);
        const game_question_t *orig = game_content_get((uint8_t)idx);
        TEST_ASSERT_EQUAL_STRING(orig->options[orig->correct], shown->options[shown->correct]);
        for (uint8_t o = 0u; o < GAME_OPTIONS; ++o) {   /* mismas 3 opciones, otro orden */
            bool found = false;
            for (uint8_t k = 0u; k < GAME_OPTIONS; ++k) {
                if (strcmp(shown->options[o], orig->options[k]) == 0) found = true;
            }
            TEST_ASSERT_TRUE(found);
        }
        pos_seen[shown->correct] = true;
    }
    TEST_ASSERT_TRUE(pos_seen[0] && pos_seen[1] && pos_seen[2]);
}

void test_same_seed_and_time_repeat_the_same_round(void)
{
    int a[GAME_QUESTIONS_PER_ROUND], b[GAME_QUESTIONS_PER_ROUND];
    unsigned na = 0u, nb = 0u;
    cogni_game_init(&g); cogni_game_seed(&g, 7u);
    play_round(5000u, a, &na);
    cogni_game_init(&g); cogni_game_seed(&g, 7u);
    play_round(5000u, b, &nb);
    TEST_ASSERT_EQUAL_UINT(na, nb);
    for (unsigned i = 0u; i < na; ++i) TEST_ASSERT_EQUAL_INT(a[i], b[i]);
}

void test_revision_changes_whenever_the_screen_must_be_redrawn(void)
{
    const uint32_t r0 = cogni_game_view(&g).revision;
    cogni_game_start(&g, 0u);
    const uint32_t r1 = cogni_game_view(&g).revision;
    cogni_game_on_button(&g, GAME_BTN_B, 0u);
    const uint32_t r2 = cogni_game_view(&g).revision;
    cogni_game_on_button(&g, GAME_BTN_A, 1000u);
    const uint32_t r3 = cogni_game_view(&g).revision;
    TEST_ASSERT_TRUE(r0 != r1 && r1 != r2 && r2 != r3);
    cogni_game_tick(&g, 1100u);                            /* nada cambio: no hay que redibujar */
    TEST_ASSERT_EQUAL_UINT32(r3, cogni_game_view(&g).revision);
}

/* Lunes 9 de noviembre de 2026, 10:00. */
static game_clock_t monday_morning(void)
{
    game_clock_t c;
    c.valid = true; c.wday = 1u; c.mday = 9u; c.month = 11u; c.hour = 10u;
    return c;
}

static void check_dyn_question_shape(const game_question_t *q)
{
    TEST_ASSERT_NOT_NULL(q->lines[0]);
    for (uint8_t l = 0u; l < GAME_QUESTION_LINES; ++l) {
        if (q->lines[l] != 0) TEST_ASSERT_TRUE(strlen(q->lines[l]) <= GAME_LINE_MAX);
    }
    for (uint8_t o = 0u; o < GAME_OPTIONS; ++o) {
        TEST_ASSERT_TRUE(strlen(q->options[o]) > 0u);
        TEST_ASSERT_TRUE(strlen(q->options[o]) <= GAME_OPTION_MAX);
        for (uint8_t p = (uint8_t)(o + 1u); p < GAME_OPTIONS; ++p) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(q->options[o], q->options[p]));   /* sin repetidas */
        }
    }
    TEST_ASSERT_TRUE(q->correct < GAME_OPTIONS);
}

void test_orientation_day_date_and_part_of_day_are_right(void)
{
    const game_clock_t c = monday_morning();
    game_dyn_text_t t; game_question_t q;
    TEST_ASSERT_TRUE(game_orientation_build(&c, 0u, 0u, &t, &q));
    TEST_ASSERT_EQUAL_STRING("Lunes", q.options[q.correct]);
    TEST_ASSERT_TRUE(game_orientation_build(&c, 1u, 0u, &t, &q));
    TEST_ASSERT_EQUAL_STRING("9", q.options[q.correct]);
    TEST_ASSERT_TRUE(game_orientation_build(&c, 2u, 0u, &t, &q));
    TEST_ASSERT_EQUAL_STRING("Manana", q.options[q.correct]);
}

void test_part_of_day_boundaries(void)
{
    static const struct { uint8_t hour; const char *want; } cases[] = {
        {0u, "Noche"}, {4u, "Noche"}, {5u, "Manana"}, {11u, "Manana"},
        {12u, "Tarde"}, {18u, "Tarde"}, {19u, "Noche"}, {23u, "Noche"}
    };
    for (unsigned i = 0u; i < sizeof cases / sizeof cases[0]; ++i) {
        game_clock_t c = monday_morning();
        c.hour = cases[i].hour;
        game_dyn_text_t t; game_question_t q;
        TEST_ASSERT_TRUE(game_orientation_build(&c, 2u, 0u, &t, &q));
        TEST_ASSERT_EQUAL_STRING(cases[i].want, q.options[q.correct]);
    }
}

void test_orientation_questions_are_valid_for_every_date_and_hour(void)
{
    for (uint8_t wday = 0u; wday < 7u; ++wday) {
        for (uint8_t mday = 1u; mday <= 31u; ++mday) {
            for (uint8_t hour = 0u; hour < 24u; hour += 5u) {
                game_clock_t c;
                c.valid = true; c.wday = wday; c.mday = mday; c.month = 1u; c.hour = hour;
                for (uint8_t kind = 0u; kind < GAME_ORIENTATION_COUNT; ++kind) {
                    for (uint8_t variant = 0u; variant < 3u; ++variant) {
                        game_dyn_text_t t; game_question_t q;
                        TEST_ASSERT_TRUE(game_orientation_build(&c, kind, variant, &t, &q));
                        check_dyn_question_shape(&q);
                    }
                }
            }
        }
    }
}

void test_date_distractors_stay_inside_the_calendar(void)
{
    for (uint8_t mday = 1u; mday <= 31u; ++mday) {
        game_clock_t c = monday_morning();
        c.mday = mday;
        game_dyn_text_t t; game_question_t q;
        TEST_ASSERT_TRUE(game_orientation_build(&c, 1u, 0u, &t, &q));
        for (uint8_t o = 0u; o < GAME_OPTIONS; ++o) {
            const int n = atoi(q.options[o]);
            TEST_ASSERT_TRUE(n >= 1 && n <= 31);
        }
    }
}

void test_correct_position_moves_between_rounds(void)
{
    const game_clock_t c = monday_morning();
    bool seen[GAME_OPTIONS] = {false, false, false};
    for (uint8_t variant = 0u; variant < 3u; ++variant) {
        game_dyn_text_t t; game_question_t q;
        TEST_ASSERT_TRUE(game_orientation_build(&c, 0u, variant, &t, &q));
        seen[q.correct] = true;
    }
    TEST_ASSERT_TRUE(seen[0] && seen[1] && seen[2]);
}

void test_invalid_clock_builds_nothing(void)
{
    game_clock_t c = monday_morning();
    game_dyn_text_t t; game_question_t q;
    c.valid = false;
    TEST_ASSERT_FALSE(game_orientation_build(&c, 0u, 0u, &t, &q));
    c = monday_morning(); c.wday = 7u;
    TEST_ASSERT_FALSE(game_orientation_build(&c, 0u, 0u, &t, &q));
    c = monday_morning(); c.mday = 0u;
    TEST_ASSERT_FALSE(game_orientation_build(&c, 0u, 0u, &t, &q));
    c = monday_morning(); c.hour = 24u;
    TEST_ASSERT_FALSE(game_orientation_build(&c, 0u, 0u, &t, &q));
    TEST_ASSERT_FALSE(game_orientation_build(0, 0u, 0u, &t, &q));
    c = monday_morning();
    TEST_ASSERT_FALSE(game_orientation_build(&c, GAME_ORIENTATION_COUNT, 0u, &t, &q));
}

void test_round_with_clock_asks_orientation_first_then_personal(void)
{
    const game_clock_t c = monday_morning();
    uint32_t t = 1000u;
    cogni_game_start_with_clock(&g, t, &c);
    cogni_game_on_button(&g, GAME_BTN_B, t);
    const char *expect[GAME_ORIENTATION_COUNT] = { "Lunes", "9", "Manana" };
    for (uint8_t i = 0u; i < GAME_ORIENTATION_COUNT; ++i) {
        const game_view_t v = cogni_game_view(&g);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(i + 1u), v.number);
        TEST_ASSERT_EQUAL_STRING(expect[i], v.question->options[v.question->correct]);
        t = answer(t, true);
    }
    const game_view_t pool_q = cogni_game_view(&g);
    TEST_ASSERT_TRUE(bank_index_of(pool_q.question) >= 0);   /* siguen preguntas del banco */
    t = answer(t, true);
    t = answer(t, true);
    TEST_ASSERT_EQUAL_INT(GAME_STATE_RESULT, cogni_game_view(&g).state);
    TEST_ASSERT_EQUAL_UINT8(5u, cogni_game_result(&g).score);
}

void test_wrong_orientation_answer_is_not_scored(void)
{
    const game_clock_t c = monday_morning();
    uint32_t t = 1000u;
    cogni_game_start_with_clock(&g, t, &c);
    cogni_game_on_button(&g, GAME_BTN_B, t);
    t = answer(t, false);
    TEST_ASSERT_EQUAL_UINT8(0u, g.score);
    TEST_ASSERT_FALSE(g.last_correct);
}

void test_round_without_clock_still_uses_only_the_bank(void)
{
    uint32_t t = 1000u;
    cogni_game_start_with_clock(&g, t, 0);
    cogni_game_on_button(&g, GAME_BTN_B, t);
    TEST_ASSERT_TRUE(bank_index_of(cogni_game_view(&g).question) >= 0);
    game_clock_t bad = monday_morning();
    bad.valid = false;
    cogni_game_start_with_clock(&g, t, &bad);
    cogni_game_on_button(&g, GAME_BTN_B, t);
    TEST_ASSERT_TRUE(bank_index_of(cogni_game_view(&g).question) >= 0);
}

void test_orientation_question_survives_until_answered(void)
{
    const game_clock_t c = monday_morning();
    cogni_game_start_with_clock(&g, 1000u, &c);
    cogni_game_on_button(&g, GAME_BTN_B, 1000u);
    const game_view_t a = cogni_game_view(&g);
    const game_view_t b = cogni_game_view(&g);
    TEST_ASSERT_EQUAL_PTR(a.question, b.question);
    TEST_ASSERT_EQUAL_STRING("Que dia de la semana", a.question->lines[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_content_respects_oled_limits);
    RUN_TEST(test_content_has_no_accents_the_oled_font_cannot_draw);
    RUN_TEST(test_intro_then_question_then_feedback);
    RUN_TEST(test_intro_left_button_exits);
    RUN_TEST(test_wrong_answer_does_not_score_and_shows_the_correct_one);
    RUN_TEST(test_feedback_closes_by_itself_or_by_button_but_not_by_accident);
    RUN_TEST(test_full_round_scores_and_reports_result);
    RUN_TEST(test_bank_questions_are_all_different);
    RUN_TEST(test_bank_has_at_least_twenty_autobiographical_questions);
    RUN_TEST(test_a_round_never_repeats_a_question);
    RUN_TEST(test_consecutive_rounds_do_not_repeat_questions);
    RUN_TEST(test_different_seeds_give_different_questions);
    RUN_TEST(test_every_question_gets_asked_over_many_rounds);
    RUN_TEST(test_options_are_shuffled_but_the_right_answer_stays_right);
    RUN_TEST(test_same_seed_and_time_repeat_the_same_round);
    RUN_TEST(test_revision_changes_whenever_the_screen_must_be_redrawn);
    RUN_TEST(test_orientation_day_date_and_part_of_day_are_right);
    RUN_TEST(test_part_of_day_boundaries);
    RUN_TEST(test_orientation_questions_are_valid_for_every_date_and_hour);
    RUN_TEST(test_date_distractors_stay_inside_the_calendar);
    RUN_TEST(test_correct_position_moves_between_rounds);
    RUN_TEST(test_invalid_clock_builds_nothing);
    RUN_TEST(test_round_with_clock_asks_orientation_first_then_personal);
    RUN_TEST(test_wrong_orientation_answer_is_not_scored);
    RUN_TEST(test_round_without_clock_still_uses_only_the_bank);
    RUN_TEST(test_orientation_question_survives_until_answered);
    return UNITY_END();
}
