/**
 * robot_fall.h
 * Lo que hace el ROBOT cuando la pulsera detecta una posible caida.
 *
 * La pulsera no tiene boton: avisa "PENDIENTE" y espera 20 s. En ese tiempo el
 * robot le pregunta al paciente "¿Estas bien?"; si responde, el robot manda
 * CANCELAR a la pulsera (con el seq de la caida) y no se molesta al cuidador.
 * Si no responde, la pulsera manda CONFIRMADA y el robot avisa que ya se aviso.
 *
 *            PENDIENTE                    boton "estoy bien"
 *   IDLE ------------------> ASKING ---------------------------> CANCELING
 *    ^   mostrar pregunta      |  | CONFIRMADA / tiempo            |  repite CANCELAR 3 veces
 *    |                         |  v                                |  (ESP-NOW no confirma)
 *    +-------------------------+--+--- CANCELADA / tiempo ---------+
 *
 * C puro: el robot real traduce las acciones (ROBOT_ACT_*) a pantalla, voz y
 * radio. No toca hardware, asi que se prueba en el PC.
 *
 *     robot_action_t a = robot_fall_on_event(&fall, &msg.evt, millis(), &ref_seq);
 *     // ... y en la tarea periodica y al pulsar el boton:
 *     a = robot_fall_tick(&fall, millis(), &ref_seq);
 *     a = robot_fall_on_button_ok(&fall, millis(), &ref_seq);
 *     // si a == ROBOT_ACT_SEND_CANCEL:  alba_pack_cancel(&c, seq++, ref_seq); esp_now_send(...)
 *
 * No es thread-safe: usarlo desde una sola tarea.
 */
#ifndef ROBOT_FALL_H
#define ROBOT_FALL_H

#include <stdint.h>

#include "alba_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_FALL_PROMPT_TIMEOUT_MS  25000u   /* la pulsera confirma a los 20 s; esto es por si se pierde ese paquete */
#define ROBOT_CANCEL_REPEATS          3u       /* envios EXTRA de CANCELAR tras el primero                              */
#define ROBOT_CANCEL_REPEAT_GAP_MS    400u

typedef enum {
    ROBOT_ACT_NONE = 0,
    ROBOT_ACT_SHOW_PROMPT,        /* mostrar/decir "¿Estas bien?" y habilitar el boton            */
    ROBOT_ACT_HIDE_PROMPT,        /* quitar la pregunta (se cancelo, o se acabo el tiempo)        */
    ROBOT_ACT_SEND_CANCEL,        /* mandar CANCELAR a la pulsera con *ref_seq                    */
    ROBOT_ACT_SHOW_HELP_CALLED    /* la caida se confirmo: "ya avise a tu cuidador"              */
} robot_action_t;

typedef enum { ROBOT_FALL_IDLE = 0, ROBOT_FALL_ASKING, ROBOT_FALL_CANCELING } robot_fall_state_t;

typedef struct {
    robot_fall_state_t state;
    uint16_t seq;              /* seq del aviso PENDIENTE que se esta atendiendo */
    uint32_t t_start_ms;
    uint32_t t_last_cancel_ms;
    uint8_t  cancels_left;     /* reenvios de CANCELAR que faltan                */
} robot_fall_t;

void robot_fall_init(robot_fall_t *f);

/** Un evento recibido de la pulsera (los que no son de caida se ignoran). */
robot_action_t robot_fall_on_event(robot_fall_t *f, const alba_event_t *e, uint32_t now_ms, uint16_t *ref_seq);

/** El paciente pulso "estoy bien". Devuelve SEND_CANCEL si habia una caida por atender. */
robot_action_t robot_fall_on_button_ok(robot_fall_t *f, uint32_t now_ms, uint16_t *ref_seq);

/** Llamar cada ~100 ms: reenvia el CANCELAR y vence la pregunta si nadie responde. */
robot_action_t robot_fall_tick(robot_fall_t *f, uint32_t now_ms, uint16_t *ref_seq);

robot_fall_state_t robot_fall_state(const robot_fall_t *f);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_FALL_H */
