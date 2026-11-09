#include "cogni_game.h"

/* ============================================================================
 *  BANCO DE PREGUNTAS AUTOBIOGRAFICAS  -  ESTE ES EL ARCHIVO QUE SE EDITA
 *
 *  >>> TODAS LAS RESPUESTAS SON EJEMPLOS INVENTADOS: reemplazarlas por la vida real
 *  >>> del paciente (con ayuda de la familia o del cuidador) antes de usarlo con el.
 *
 *  Las preguntas estan en primera persona ("mi regalo", "mi pareja") para que el
 *  paciente las lea como recuerdos propios. Cada vez que se abre CogniGame el robot
 *  elige al azar de este banco (sin repetir las de la ronda anterior) y tambien
 *  baraja el orden de las 3 opciones.
 *
 *  Reglas (el test las verifica):
 *   - Al menos 20 preguntas; maximo GAME_BANK_MAX (64).
 *   - Sin tildes ni enie (la fuente de la OLED no las tiene): "Pais", "Montana"...
 *   - Cada linea de la pregunta: maximo 21 caracteres. Hasta 4 lineas; el resto 0.
 *   - Cada opcion: maximo 17 caracteres, las 3 distintas entre si.
 *   - correct: 0, 1 o 2 = posicion de la respuesta correcta en la lista de opciones.
 * ========================================================================== */
static const game_question_t pool[] = {
    { { "Cual fue mi regalo",    "mas apreciado?", 0, 0 },
      { "Una bicicleta", "Un reloj", "Un libro" }, 0 },
    { { "Donde vivia cuando",    "tenia 10 anos?", 0, 0 },
      { "En una casa", "En el campo", "En un edificio" }, 1 },
    { { "Que hacia cuando",      "tenia 20 anos?", 0, 0 },
      { "Estudiaba", "Trabajaba", "Viajaba" }, 1 },
    { { "Quien fue mi mejor",    "amigo de la infancia?", 0, 0 },
      { "Carlos", "Luis", "Pedro" }, 2 },
    { { "En que trabaje la",     "mayor parte de mi", "vida?", 0 },
      { "Docente", "Comerciante", "Enfermero" }, 0 },
    { { "Donde conoci a",        "mi pareja?", 0, 0 },
      { "En la escuela", "En una fiesta", "En el trabajo" }, 1 },
    { { "Como se llama",         "mi pareja?", 0, 0 },
      { "Rosa", "Marta", "Elena" }, 2 },
    { { "Cuantos hijos",         "tengo?", 0, 0 },
      { "Dos", "Tres", "Cuatro" }, 1 },
    { { "Como se llama",         "mi primer hijo?", 0, 0 },
      { "Juan", "Pablo", "Andres" }, 0 },
    { { "Cual era mi comida",    "favorita de la", "infancia?", 0 },
      { "Sopa de pollo", "Encebollado", "Arroz con huevo" }, 0 },
    { { "Que musica me",         "gusta mas?", 0, 0 },
      { "Pasillo", "Bolero", "Vallenato" }, 1 },
    { { "Cual fue mi lugar",     "favorito de", "vacaciones?", 0 },
      { "La playa", "La montana", "El campo" }, 0 },
    { { "Que mascota tuve",      "de pequeno?", 0, 0 },
      { "Un perro", "Un gato", "Un loro" }, 0 },
    { { "Como se llamaba",       "mi mascota?", 0, 0 },
      { "Firulais", "Michi", "Rocky" }, 2 },
    { { "Que fiesta disfrutaba", "mas?", 0, 0 },
      { "Navidad", "Carnaval", "Mi cumpleanos" }, 0 },
    { { "En que ciudad",         "naci?", 0, 0 },
      { "Guayaquil", "Quito", "Cuenca" }, 0 },
    { { "Como se llamaba",       "mi mama?", 0, 0 },
      { "Maria", "Carmen", "Rosa" }, 1 },
    { { "Como se llamaba",       "mi papa?", 0, 0 },
      { "Jose", "Manuel", "Luis" }, 0 },
    { { "Que pasatiempo",        "me gusta mas?", 0, 0 },
      { "Cocinar", "Tejer", "La jardineria" }, 2 },
    { { "Cual fue uno de los",   "mejores dias de", "mi vida?", 0 },
      { "Mi boda", "Nacio mi hijo", "Mi graduacion" }, 1 },
};

uint8_t game_content_count(void)
{
    return (uint8_t)(sizeof(pool) / sizeof(pool[0]));
}

const game_question_t *game_content_get(uint8_t index)
{
    return index < game_content_count() ? &pool[index] : 0;
}
