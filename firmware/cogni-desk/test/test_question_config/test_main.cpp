#include <unity.h>
#include <string.h>

extern "C" {
#include "question_config.h"
}

static qc_set_t set;
static char out[QC_TEXT_MAX + 1];

void setUp(void) { game_content_set_custom(nullptr, 0u); }
void tearDown(void) {}

/* ---- extraer el texto del JSON ---- */

void test_extract_from_attribute_update(void)
{
    TEST_ASSERT_EQUAL_INT(7, qc_extract_string("{\"questions\":\"a|b|c|d\"}", "questions", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("a|b|c|d", out);
}

void test_extract_from_attribute_response_and_spaces(void)
{
    const char *json = "{ \"shared\" : { \"other\": \"x\", \"questions\" :  \"hola\" } }";
    TEST_ASSERT_EQUAL_INT(4, qc_extract_string(json, "questions", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("hola", out);
}

void test_extract_unescapes_quotes_newlines_and_unicode(void)
{
    const char *json = "{\"questions\":\"di \\\"hola\\\"\\nfin \\u00f1 \\u00e1 \\u20ac\"}";
    TEST_ASSERT_GREATER_THAN_INT(0, qc_extract_string(json, "questions", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("di \"hola\"\nfin \xC3\xB1 \xC3\xA1 ", out);
}

void test_extract_ignores_a_value_that_looks_like_the_key(void)
{
    const char *json = "{\"note\":\"questions\",\"questions\":\"real\"}";
    TEST_ASSERT_EQUAL_INT(4, qc_extract_string(json, "questions", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("real", out);
}

void test_extract_empty_missing_and_not_text(void)
{
    TEST_ASSERT_EQUAL_INT(0, qc_extract_string("{\"questions\":\"\"}", "questions", out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, qc_extract_string("{\"other\":\"x\"}", "questions", out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, qc_extract_string("{\"questions\":null}", "questions", out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, qc_extract_string("{\"questions\":\"sin cerrar", "questions", out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, qc_extract_string("", "questions", out, sizeof out));
    TEST_ASSERT_EQUAL_INT(-1, qc_extract_string(nullptr, "questions", out, sizeof out));
}

void test_extract_reports_when_it_does_not_fit(void)
{
    char small[5];
    TEST_ASSERT_EQUAL_INT(-2, qc_extract_string("{\"questions\":\"123456\"}", "questions", small, sizeof small));
    TEST_ASSERT_EQUAL_INT(4, qc_extract_string("{\"questions\":\"1234\"}", "questions", small, sizeof small));
}

/* ---- armar preguntas ---- */

void test_parse_one_question(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, qc_parse("Cual fue mi regalo?|Una bicicleta|Un reloj|Un libro", &set));
    TEST_ASSERT_EQUAL_UINT8(0u, set.rejected);
    TEST_ASSERT_EQUAL_STRING("Cual fue mi regalo?", set.q[0].lines[0]);
    TEST_ASSERT_NULL(set.q[0].lines[1]);
    TEST_ASSERT_EQUAL_STRING("Una bicicleta", set.q[0].options[0]);
    TEST_ASSERT_EQUAL_STRING("Un reloj", set.q[0].options[1]);
    TEST_ASSERT_EQUAL_STRING("Un libro", set.q[0].options[2]);
    TEST_ASSERT_EQUAL_UINT8(0u, set.q[0].correct);
}

void test_parse_wraps_long_questions_at_21_characters(void)
{
    qc_parse("Donde vivia cuando tenia diez anos de edad?|Casa|Campo|Edificio", &set);
    TEST_ASSERT_EQUAL_UINT8(1u, set.count);
    TEST_ASSERT_EQUAL_STRING("Donde vivia cuando", set.q[0].lines[0]);
    TEST_ASSERT_EQUAL_STRING("tenia diez anos de", set.q[0].lines[1]);
    TEST_ASSERT_EQUAL_STRING("edad?", set.q[0].lines[2]);
    for (int l = 0; l < 4; ++l) {
        if (set.q[0].lines[l] != nullptr) TEST_ASSERT_LESS_OR_EQUAL(21, (int)strlen(set.q[0].lines[l]));
    }
}

void test_parse_converts_accents_and_drops_inverted_marks(void)
{
    qc_parse("\xC2\xBF" "D\xC3\xB3" "nde na\xC3\xA7\xC3\xAD" "?|Gu\xC3\xA1" "yaquil|Ni\xC3\xB1" "o|\xC3\x91" "ata", &set);
    TEST_ASSERT_EQUAL_UINT8(1u, set.count);
    TEST_ASSERT_EQUAL_STRING("Donde nai?", set.q[0].lines[0]);
    TEST_ASSERT_EQUAL_STRING("Guayaquil", set.q[0].options[0]);
    TEST_ASSERT_EQUAL_STRING("Nino", set.q[0].options[1]);
    TEST_ASSERT_EQUAL_STRING("Nata", set.q[0].options[2]);
}

void test_parse_accepts_hash_and_newline_separators_and_trims(void)
{
    const char *t = " P1 | a | b | c #P2|d|e|f\nP3|g|h|i\n\n#  # P4|j|k|l ";
    TEST_ASSERT_EQUAL_UINT8(4u, qc_parse(t, &set));
    TEST_ASSERT_EQUAL_UINT8(0u, set.rejected);
    TEST_ASSERT_EQUAL_STRING("P1", set.q[0].lines[0]);
    TEST_ASSERT_EQUAL_STRING("a", set.q[0].options[0]);
    TEST_ASSERT_EQUAL_STRING("P4", set.q[3].lines[0]);
}

void test_parse_rejects_bad_entries_and_keeps_the_good_ones(void)
{
    const char *t =
        "Buena|a|b|c#"
        "Faltan campos|a|b#"
        "Sobran campos|a|b|c|d#"
        "Opcion larga|123456789012345678|b|c#"          /* 18 > 17 */
        "Repetidas|Si|si|no#"
        "Palabra gigante abcdefghijklmnopqrstuvwxyz|a|b|c#"
        "Campo vacio||b|c#"
        "Muy larga una dos tres cuatro cinco seis siete ocho nueve diez once doce trece catorce quince|a|b|c#"
        "Otra buena|x|y|z";
    TEST_ASSERT_EQUAL_UINT8(2u, qc_parse(t, &set));
    TEST_ASSERT_EQUAL_UINT8(7u, set.rejected);
    TEST_ASSERT_EQUAL_STRING("Otra buena", set.q[1].lines[0]);
}

void test_parse_option_of_exactly_17_characters_is_valid(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, qc_parse("P|12345678901234567|b|c", &set));
}

void test_parse_caps_at_qc_max(void)
{
    char text[1024] = "";
    for (unsigned i = 0; i < QC_MAX + 3u; ++i) strcat(text, "P|a|b|c#");
    TEST_ASSERT_EQUAL_UINT8(QC_MAX, qc_parse(text, &set));
    TEST_ASSERT_EQUAL_UINT8(3u, set.rejected);
}

void test_parse_empty_and_null_give_nothing(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, qc_parse("", &set));
    TEST_ASSERT_EQUAL_UINT8(0u, qc_parse("  # \n ", &set));
    TEST_ASSERT_EQUAL_UINT8(0u, set.rejected);
    TEST_ASSERT_EQUAL_UINT8(0u, qc_parse(nullptr, &set));
}

void test_parsed_questions_are_valid_for_the_game(void)
{
    const char *t = "P1|a|b|c#P2|d|e|f#P3|g|h|i#P4|j|k|l#P5|m|n|o#P6|p|q|r";
    TEST_ASSERT_EQUAL_UINT8(6u, qc_parse(t, &set));
    game_content_set_custom(set.q, set.count);
    TEST_ASSERT_EQUAL_UINT8(6u, game_content_count());
    TEST_ASSERT_EQUAL_STRING("P3", game_content_get(2u)->lines[0]);
    TEST_ASSERT_NULL(game_content_get(6u));
    game_content_set_custom(nullptr, 0u);                     /* vuelve al banco incluido */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(20u, game_content_count());
}

void test_custom_bank_drives_a_whole_round(void)
{
    const char *t = "P1|a|b|c#P2|d|e|f#P3|g|h|i#P4|j|k|l#P5|m|n|o#P6|p|q|r";
    qc_parse(t, &set);
    game_content_set_custom(set.q, set.count);
    cogni_game_t g;
    cogni_game_init(&g);
    cogni_game_seed(&g, 123u);
    cogni_game_start(&g, 0u);
    uint32_t now = 100u;
    cogni_game_on_button(&g, GAME_BTN_B, now);                  /* pasa la intro */
    for (unsigned i = 0; i < GAME_QUESTIONS_PER_ROUND; ++i) {
        game_view_t v = cogni_game_view(&g);
        TEST_ASSERT_EQUAL_INT(GAME_STATE_QUESTION, v.state);
        TEST_ASSERT_NOT_NULL(v.question);
        const char *first = v.question->lines[0];
        TEST_ASSERT_TRUE(first[0] == 'P');                       /* viene del banco del cuidador */
        now += 1000u;
        cogni_game_on_button(&g, (game_button_t)v.question->correct, now);
        now += GAME_FEEDBACK_MS + 10u;
        cogni_game_tick(&g, now);
    }
    TEST_ASSERT_EQUAL_UINT8(GAME_QUESTIONS_PER_ROUND, cogni_game_result(&g).score);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_extract_from_attribute_update);
    RUN_TEST(test_extract_from_attribute_response_and_spaces);
    RUN_TEST(test_extract_unescapes_quotes_newlines_and_unicode);
    RUN_TEST(test_extract_ignores_a_value_that_looks_like_the_key);
    RUN_TEST(test_extract_empty_missing_and_not_text);
    RUN_TEST(test_extract_reports_when_it_does_not_fit);
    RUN_TEST(test_parse_one_question);
    RUN_TEST(test_parse_wraps_long_questions_at_21_characters);
    RUN_TEST(test_parse_converts_accents_and_drops_inverted_marks);
    RUN_TEST(test_parse_accepts_hash_and_newline_separators_and_trims);
    RUN_TEST(test_parse_rejects_bad_entries_and_keeps_the_good_ones);
    RUN_TEST(test_parse_option_of_exactly_17_characters_is_valid);
    RUN_TEST(test_parse_caps_at_qc_max);
    RUN_TEST(test_parse_empty_and_null_give_nothing);
    RUN_TEST(test_parsed_questions_are_valid_for_the_game);
    RUN_TEST(test_custom_bank_drives_a_whole_round);
    return UNITY_END();
}
