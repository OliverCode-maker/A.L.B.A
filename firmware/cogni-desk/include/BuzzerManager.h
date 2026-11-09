#ifndef BUZZER_MANAGER_H
#define BUZZER_MANAGER_H

#include <stdint.h>

typedef enum {
    BUZZER_PATTERN_OFF = 0,
    BUZZER_PATTERN_FALL,            /* pitidos rapidos: posible caida, esperando "estoy bien" */
    BUZZER_PATTERN_FALL_CONFIRMED,  /* pitidos largos: caida confirmada, hay que acudir        */
    BUZZER_PATTERN_MEDICATION,      /* pitidos suaves: hora del medicamento                    */
    BUZZER_PATTERN_COMFORT          /* unos pocos pitidos: ambiente fuera de rango (8 s)        */
} buzzer_pattern_t;

void initBuzzer(void);
/* Llamar en cada vuelta del loop; no bloquea. */
void actualizarBuzzer(buzzer_pattern_t pattern, uint32_t now_ms);

#endif
