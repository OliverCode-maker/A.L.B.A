#include "cogni_game.h"

/* ============================================================================
 *  BANCO DE PREGUNTAS  -  ESTE ES EL ARCHIVO QUE SE EDITA PARA CAMBIAR EL JUEGO
 *
 *  Reglas (el test las verifica):
 *   - Sin tildes ni enie (la fuente de la OLED no las tiene): "Pais", "Montana"...
 *   - Cada linea de la pregunta: maximo 21 caracteres. Hasta 4 lineas; el resto NULL.
 *   - Cada opcion: maximo 17 caracteres.
 *   - correct: 0 = boton izquierdo, 1 = boton centro (OK), 2 = boton derecho.
 *   - Cada ronda usa 5 preguntas; empieza en una distinta cada vez.
 *
 *  Las 3 primeras son PERSONALES: son de ejemplo, cambiarlas por datos reales
 *  del paciente. Las otras 3 son de cultura general.
 * ========================================================================== */
static const game_question_t pool[] = {
    /* --- PERSONALES (EDITAR) --- */
    { { "En que ciudad vive",  "su familia?", 0, 0 },
      { "Guayaquil", "Quito", "Cuenca" }, 0 },
    { { "Como se llama su",    "nieta?", 0, 0 },
      { "Maria", "Lucia", "Ana" }, 1 },
    { { "Que comida le gusta", "mas?", 0, 0 },
      { "Sopa", "Arroz", "Encebollado" }, 2 },
    /* --- CULTURA GENERAL --- */
    { { "Cual es la capital",  "del Ecuador?", 0, 0 },
      { "Quito", "Guayaquil", "Cuenca" }, 0 },
    { { "En que mes se celebra", "la Independencia de", "Guayaquil?", 0 },
      { "Agosto", "Octubre", "Mayo" }, 1 },
    { { "Que oceano toca la",  "costa del Ecuador?", 0, 0 },
      { "Indico", "Atlantico", "Pacifico" }, 2 },
};

uint8_t game_content_count(void)
{
    return (uint8_t)(sizeof(pool) / sizeof(pool[0]));
}

const game_question_t *game_content_get(uint8_t index)
{
    return index < game_content_count() ? &pool[index] : 0;
}
