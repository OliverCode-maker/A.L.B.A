#include <unity.h>
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

void test_each_round_starts_on_a_different_question(void)
{
    cogni_game_start(&g, 0u);
    cogni_game_on_button(&g, GAME_BTN_B, 0u);
    const game_question_t *first_round = cogni_game_view(&g).question;
    uint32_t t = 0u;
    for (uint8_t i = 0u; i < GAME_QUESTIONS_PER_ROUND; ++i) t = answer(t + 1000u, true);
    cogni_game_on_button(&g, GAME_BTN_B, t + 1u);   /* salir */

    cogni_game_start(&g, t + 2u);
    cogni_game_on_button(&g, GAME_BTN_B, t + 3u);
    TEST_ASSERT_TRUE(cogni_game_view(&g).question != first_round);
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
    RUN_TEST(test_each_round_starts_on_a_different_question);
    RUN_TEST(test_revision_changes_whenever_the_screen_must_be_redrawn);
    return UNITY_END();
}
