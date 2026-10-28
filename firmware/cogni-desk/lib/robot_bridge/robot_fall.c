#include "robot_fall.h"

void robot_fall_init(robot_fall_t *f)
{
    f->state = ROBOT_FALL_IDLE;
    f->seq = 0;
    f->t_start_ms = 0;
    f->t_last_cancel_ms = 0;
    f->cancels_left = 0;
}

robot_fall_state_t robot_fall_state(const robot_fall_t *f) { return f->state; }

robot_action_t robot_fall_on_event(robot_fall_t *f, const alba_event_t *e, uint32_t now_ms, uint16_t *ref_seq)
{
    if (e == NULL || e->alerta_tipo != ALBA_ALERTA_CAIDA_DETECTADA) return ROBOT_ACT_NONE;

    switch (e->confirmacion) {
    case ALBA_CONF_PENDIENTE:
        /* Una caida nueva (aunque ya hubiera otra en curso) reinicia la pregunta. */
        f->state = ROBOT_FALL_ASKING;
        f->seq = e->hdr.seq;
        f->t_start_ms = now_ms;
        f->cancels_left = 0;
        if (ref_seq) *ref_seq = f->seq;
        return ROBOT_ACT_SHOW_PROMPT;

    case ALBA_CONF_CONFIRMADA:
        /* Se reacciona aunque el PENDIENTE se haya perdido por el aire. */
        f->state = ROBOT_FALL_IDLE;
        return ROBOT_ACT_SHOW_HELP_CALLED;

    case ALBA_CONF_CANCELADA:
        if (f->state == ROBOT_FALL_IDLE) return ROBOT_ACT_NONE;
        f->state = ROBOT_FALL_IDLE;
        f->cancels_left = 0;
        return ROBOT_ACT_HIDE_PROMPT;

    default:
        return ROBOT_ACT_NONE;
    }
}

robot_action_t robot_fall_on_button_ok(robot_fall_t *f, uint32_t now_ms, uint16_t *ref_seq)
{
    if (f->state != ROBOT_FALL_ASKING) return ROBOT_ACT_NONE;    /* nada que cancelar (o ya se esta cancelando) */
    f->state = ROBOT_FALL_CANCELING;
    f->t_last_cancel_ms = now_ms;
    f->cancels_left = ROBOT_CANCEL_REPEATS;
    if (ref_seq) *ref_seq = f->seq;
    return ROBOT_ACT_SEND_CANCEL;
}

robot_action_t robot_fall_tick(robot_fall_t *f, uint32_t now_ms, uint16_t *ref_seq)
{
    if (f->state == ROBOT_FALL_IDLE) return ROBOT_ACT_NONE;

    /* Si nadie responde, o la pulsera nunca contesto, no dejar la pregunta en pantalla para siempre. */
    if ((uint32_t)(now_ms - f->t_start_ms) >= ROBOT_FALL_PROMPT_TIMEOUT_MS) {
        f->state = ROBOT_FALL_IDLE;
        f->cancels_left = 0;
        return ROBOT_ACT_HIDE_PROMPT;
    }

    if (f->state == ROBOT_FALL_CANCELING && f->cancels_left > 0u &&
        (uint32_t)(now_ms - f->t_last_cancel_ms) >= ROBOT_CANCEL_REPEAT_GAP_MS)
    {
        f->cancels_left--;
        f->t_last_cancel_ms = now_ms;
        if (ref_seq) *ref_seq = f->seq;
        return ROBOT_ACT_SEND_CANCEL;
    }
    return ROBOT_ACT_NONE;
}
