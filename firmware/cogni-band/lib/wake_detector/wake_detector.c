/**
 * wake_detector.c
 * Ver wake_detector.h para el diagrama de estados y el modo de uso.
 */
#include "wake_detector.h"

#include <string.h>

/* ======================================================================== */
/*  Utilidades                                                              */
/* ======================================================================== */

/* i = 0 es la lectura aceptada mas reciente. */
static const wake_hr_item_t *hist_at(const wake_detector_t *w, uint8_t i)
{
    return &w->hist[(uint8_t)((w->hist_head + WAKE_HIST_LEN - 1u - i) % WAKE_HIST_LEN)];
}

static void hist_push(wake_detector_t *w, uint32_t t_s, uint8_t bpm)
{
    w->hist[w->hist_head].t_s = t_s;
    w->hist[w->hist_head].bpm = bpm;
    w->hist_head = (uint8_t)((w->hist_head + 1u) % WAKE_HIST_LEN);
    if (w->hist_n < WAKE_HIST_LEN) w->hist_n++;
}

/* Mediana de las ultimas `n` lecturas aceptadas. Rechaza un valor atipico
 * aislado mucho mejor que una media movil. */
static uint8_t median_last(const wake_detector_t *w, uint8_t n)
{
    uint8_t tmp[8];
    uint8_t k = 0;
    if (n > 8u) n = 8u;
    for (uint8_t i = 0; i < n && i < w->hist_n; i++) tmp[k++] = hist_at(w, i)->bpm;
    for (uint8_t i = 1; i < k; i++) {                    /* insercion: k <= 8 */
        const uint8_t v = tmp[i];
        uint8_t j = i;
        while (j > 0u && tmp[j - 1u] > v) { tmp[j] = tmp[j - 1u]; j--; }
        tmp[j] = v;
    }
    return k ? tmp[k / 2u] : 0u;
}

/* Pendiente de la FC en bpm/min por regresion lineal sobre las lecturas de los
 * ultimos `slope_window_s` segundos, usando sus marcas de tiempo REALES (el
 * periodo de medicion cambia entre 15 s y 300 s segun el estado, asi que no se
 * puede suponer un espaciado fijo). Requiere al menos 3 puntos. */
static float slope_bpm_per_min(const wake_detector_t *w, uint32_t now_s)
{
    const uint32_t win = w->cfg.slope_window_s;
    float sx = 0.0f, sy = 0.0f;
    uint8_t n = 0;

    for (uint8_t i = 0; i < w->hist_n; i++) {
        const wake_hr_item_t *it = hist_at(w, i);
        const uint32_t age = now_s - it->t_s;
        if (age > win) break;                  /* mas viejas: fuera de la ventana */
        sx += -(float)age;
        sy += (float)it->bpm;
        n++;
    }
    if (n < 3u) return 0.0f;

    const float mx = sx / (float)n, my = sy / (float)n;
    float sxx = 0.0f, sxy = 0.0f;
    for (uint8_t i = 0; i < n; i++) {
        const wake_hr_item_t *it = hist_at(w, i);
        const float dx = -(float)(now_s - it->t_s) - mx;
        const float dy = (float)it->bpm - my;
        sxx += dx * dx;
        sxy += dx * dy;
    }
    if (sxx <= 0.0f) return 0.0f;
    return (sxy / sxx) * 60.0f;                /* bpm/s -> bpm/min */
}

static float fmax2(float a, float b) { return a > b ? a : b; }

/* ======================================================================== */
/*  API                                                                     */
/* ======================================================================== */

void wake_config_default(wake_config_t *c)
{
    memset(c, 0, sizeof *c);
    c->sqi_min                = 60;
    c->bpm_min                = 35;
    c->bpm_max                = 180;
    c->baseline_alpha         = 0.02f;
    c->act_quiet_pct          = 5;
    c->act_awake_pct          = 15;
    c->rise_enter_delta_bpm   = 15.0f;
    c->rise_slope_bpm_min     = 3.0f;
    c->rise_abort_delta_bpm   = 8.0f;
    c->awake_abs_bpm          = 80.0f;
    c->awake_rel_delta_bpm    = 20.0f;
    c->sleep_return_delta_bpm = 10.0f;
    c->slope_window_s         = 180;
    c->rise_confirm_s         = 180;
    c->rising_timeout_s       = 900;
    c->hour_min               = 4;
    c->hour_max               = 11;
    c->refractory_s           = 6u * 3600u;
    c->period_sleep_s         = 60;
    c->period_rising_s        = 15;
    c->period_awake_s         = 300;
}

void wake_detector_init(wake_detector_t *w, const wake_config_t *cfg)
{
    memset(w, 0, sizeof *w);
    if (cfg) w->cfg = *cfg; else wake_config_default(&w->cfg);
    w->state    = WAKE_SLEEP;
    w->baseline = 55.0f;      /* solo hasta que baseline_ready sea true */
}

uint16_t wake_detector_sampling_period_s(const wake_detector_t *w)
{
    switch (w->state) {
    case WAKE_RISING: return w->cfg.period_rising_s;
    case WAKE_AWAKE:  return w->cfg.period_awake_s;
    default:          return w->cfg.period_sleep_s;
    }
}

wake_state_t wake_detector_state(const wake_detector_t *w)    { return w->state; }
float        wake_detector_baseline(const wake_detector_t *w) { return w->baseline; }

wake_event_t wake_detector_update(wake_detector_t *w, const wake_vital_t *v, uint8_t hour_local)
{
    const wake_config_t *c = &w->cfg;

    /* 1. Control de calidad: una lectura mala es peor que ninguna. */
    if (v->sqi < c->sqi_min || v->bpm < c->bpm_min || v->bpm > c->bpm_max) {
        w->n_rejected++;
        return WAKE_EVT_NONE;
    }

    hist_push(w, v->t_s, v->bpm);

    const float med   = (float)median_last(w, 3);
    const float slope = slope_bpm_per_min(w, v->t_s);
    const bool  quiet = v->act_pct < c->act_quiet_pct;
    const bool  in_window = (hour_local >= c->hour_min) && (hour_local <= c->hour_max);
    const bool  refractory_ok = !w->has_triggered ||
                                ((v->t_s - w->t_last_trigger) > c->refractory_s);

    switch (w->state)
    {
    case WAKE_SLEEP:
        if (!w->baseline_ready) {
            /* Primera linea base: mediana de las primeras lecturas con el paciente quieto. */
            if (quiet && w->hist_n >= 3u) {
                w->baseline = med;
                w->baseline_ready = true;
            }
            break;
        }
        /* La basal solo se actualiza con el paciente quieto: si no, la propia
         * subida contaminaria la referencia que debe detectarla. */
        if (quiet) w->baseline += c->baseline_alpha * (med - w->baseline);

        if (in_window && refractory_ok &&
            med > w->baseline + c->rise_enter_delta_bpm &&
            slope >= c->rise_slope_bpm_min)
        {
            w->state = WAKE_RISING;
            w->t_rise_start = v->t_s;
        }
        break;

    case WAKE_RISING: {
        /* La subida se revirtio: fue un despertar transitorio (arousal). */
        if (med < w->baseline + c->rise_abort_delta_bpm) { w->state = WAKE_SLEEP; break; }
        /* Sospecha que no se consolida: no gastar bateria midiendo cada 15 s para siempre. */
        const uint32_t elapsed = v->t_s - w->t_rise_start;
        if (elapsed > c->rising_timeout_s) { w->state = WAKE_SLEEP; break; }

        const float awake_th = fmax2(c->awake_abs_bpm, w->baseline + c->awake_rel_delta_bpm);
        if (elapsed >= c->rise_confirm_s &&
            med > awake_th &&
            v->act_pct > c->act_awake_pct &&      /* movimiento que corrobora el despertar */
            in_window && refractory_ok)
        {
            w->state = WAKE_AWAKE;
            w->t_last_trigger = v->t_s;
            w->has_triggered = true;
            return WAKE_EVT_TRIGGER;
        }
        break;
    }

    case WAKE_AWAKE:
        if (med < w->baseline + c->sleep_return_delta_bpm && quiet) w->state = WAKE_SLEEP;
        break;
    }
    return WAKE_EVT_NONE;
}
