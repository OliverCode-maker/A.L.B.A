#ifndef QUESTION_MANAGER_H
#define QUESTION_MANAGER_H

#include <stdbool.h>

/* Preguntas de CogniGame configurables por el cuidador desde ThingsBoard (atributo compartido
 * `questions`, formato en lib/question_config/question_config.h). Se guardan en la memoria del
 * robot (NVS), asi sobreviven a un reinicio y funcionan sin internet. Sin preguntas del cuidador
 * se usa el banco de 20 incluido en game_content.c. */

/* Carga las preguntas guardadas, si hay. Llamar en setup(). */
void initQuestions(void);

/* Aplica las preguntas nuevas que llegaron de la nube. No toca el banco mientras el paciente
 * juega (`game_active`): espera a que cierre el juego. Llamar en cada vuelta del loop. */
void actualizarQuestions(bool game_active);

#endif
