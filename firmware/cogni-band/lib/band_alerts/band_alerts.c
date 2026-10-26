#include "band_alerts.h"

/* g -> mili-g, con tope en lo que cabe en 16 bits (65.5 g). */
static uint16_t g_to_mg(float g)
{
    if (g <= 0.0f) return 0u;
    const float mg = g * 1000.0f + 0.5f;
    if (mg >= 65535.0f) return 65535u;
    return (uint16_t)mg;
}

bool band_alert_from_fall(fall_event_t event, const fall_features_t *f, band_alert_t *out)
{
    if (out == NULL) return false;

    switch (event) {
    case FALL_EVT_CANDIDATE: out->confirmacion = ALBA_CONF_PENDIENTE;  break;
    case FALL_EVT_CONFIRMED: out->confirmacion = ALBA_CONF_CONFIRMADA; break;
    case FALL_EVT_CANCELLED: out->confirmacion = ALBA_CONF_CANCELADA;  break;
    default:                 return false;     /* NONE, REJECTED: nada que avisar */
    }
    out->alerta_tipo = ALBA_ALERTA_CAIDA_DETECTADA;
    out->valor_pico  = (f != NULL) ? g_to_mg(f->svm_max) : 0u;
    return true;
}

bool band_alert_from_wake(wake_event_t event, uint8_t bpm, band_alert_t *out)
{
    if (out == NULL || event != WAKE_EVT_TRIGGER) return false;
    out->alerta_tipo  = ALBA_ALERTA_VIGILIA_TRIGGER;
    out->valor_pico   = bpm;
    out->confirmacion = ALBA_CONF_CONFIRMADA;
    return true;
}

void band_cancel_gate_init(band_cancel_gate_t *g)
{
    g->armed = false;
    g->seq = 0;
}

void band_cancel_note_sent(band_cancel_gate_t *g, const band_alert_t *alert, uint16_t seq)
{
    if (g == NULL || alert == NULL || alert->alerta_tipo != ALBA_ALERTA_CAIDA_DETECTADA) return;
    if (alert->confirmacion == ALBA_CONF_PENDIENTE) {
        g->armed = true;
        g->seq = seq;
    } else {
        g->armed = false;
    }
}

bool band_cancel_accept(band_cancel_gate_t *g, uint16_t ref_seq)
{
    if (g == NULL || !g->armed || g->seq != ref_seq) return false;
    g->armed = false;
    return true;
}
