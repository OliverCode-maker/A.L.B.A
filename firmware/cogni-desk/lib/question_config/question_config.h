#ifndef QUESTION_CONFIG_H
#define QUESTION_CONFIG_H

/* Preguntas editables por el cuidador (C puro, sin Arduino).
 *
 * El cuidador escribe el atributo compartido `questions` del dispositivo en ThingsBoard. Formato:
 *
 *     pregunta|respuesta correcta|opcion falsa 1|opcion falsa 2#otra pregunta|...
 *
 * - Las preguntas se separan con `#` o con salto de linea; los 4 campos con `|`.
 * - La respuesta correcta se escribe SIEMPRE primero: el robot baraja las 3 opciones.
 * - Tildes y enie se convierten solas (la OLED no las tiene); `¿` y `¡` se quitan.
 * - La pregunta se parte sola en lineas de 21 caracteres (hasta 4 lineas, o sea ~80 letras).
 * - Cada opcion: maximo 17 caracteres y las 3 distintas entre si.
 * - Entre QC_MIN y QC_MAX preguntas validas. Las invalidas se descartan y se cuentan.
 * - Texto vacio = volver al banco de preguntas incluido en el robot. */

#include <stddef.h>
#include <stdint.h>

#include "cogni_game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QC_MAX       20u     /* preguntas personalizadas que caben */
#define QC_MIN       5u      /* minimo para una ronda completa sin repetir */
#define QC_TEXT_MAX  2048u   /* largo maximo del texto del atributo (sin el NUL) */

typedef struct {
    char lines[GAME_QUESTION_LINES][GAME_LINE_MAX + 1u];
    char options[GAME_OPTIONS][GAME_OPTION_MAX + 1u];
} qc_text_t;

/* Las preguntas de `q` apuntan al texto de este mismo struct: NO copiarlo despues de qc_parse(). */
typedef struct {
    qc_text_t text[QC_MAX];
    game_question_t q[QC_MAX];
    uint8_t count;       /* preguntas validas */
    uint8_t rejected;    /* entradas descartadas por formato, largo o repetidas */
} qc_set_t;

/* Saca el texto del atributo `key` de un JSON de ThingsBoard, quitando los escapes (\" \\ \n \uXXXX).
 * Sirve para {"questions":"..."} y para {"shared":{"questions":"..."}}.
 * Devuelve el largo escrito en `out`, -1 si la clave no esta o no es texto, -2 si no cabe. */
int qc_extract_string(const char *json, const char *key, char *out, size_t cap);

/* Convierte el texto del cuidador en preguntas. Devuelve las validas (igual a set->count). */
uint8_t qc_parse(const char *text, qc_set_t *set);

#ifdef __cplusplus
}
#endif

#endif
