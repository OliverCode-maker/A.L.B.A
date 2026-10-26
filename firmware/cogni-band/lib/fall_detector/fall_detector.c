/**
 * fall_detector.c
 * Ver fall_detector.h para el diagrama de estados y el modo de uso.
 */
#include "fall_detector.h"

#include <math.h>
#include <string.h>

#define RAD2DEG 57.29578f

/* ======================================================================== */
/*  Utilidades                                                              */
/* ======================================================================== */

static int16_t to_mg(float g)
{
    float mg = g * 1000.0f;
    if (mg >  32767.0f) mg =  32767.0f;
    if (mg < -32768.0f) mg = -32768.0f;
    return (int16_t)mg;
}

/* Milisegundos -> numero de muestras, acotado al tamano del anillo. */
static uint16_t ms_to_n(const fall_config_t *c, uint32_t ms)
{
    uint32_t n = (ms * (uint32_t)c->sample_hz + 500u) / 1000u;
    if (n > FALL_RING_LEN) n = FALL_RING_LEN;
    return (uint16_t)n;
}

static void ring_push(fall_detector_t *fd, const fall_sample_t *s, float svm)
{
    fall_ring_item_t *it = &fd->ring[fd->head];
    it->ax  = to_mg(s->ax);
    it->ay  = to_mg(s->ay);
    it->az  = to_mg(s->az);
    it->svm = to_mg(svm);
    fd->head = (uint16_t)((fd->head + 1u) % FALL_RING_LEN);
    if (fd->count < FALL_RING_LEN) fd->count++;
}

/* age = 0 es la muestra mas reciente, age = 1 la anterior, etc. */
static const fall_ring_item_t *ring_at(const fall_detector_t *fd, uint16_t age)
{
    uint16_t idx = (uint16_t)((fd->head + FALL_RING_LEN - 1u - age) % FALL_RING_LEN);
    return &fd->ring[idx];
}

/* Estadisticos sobre `n` muestras que empiezan `age_start` muestras atras.
 * Si el anillo tiene menos historia, usa la que haya y devuelve cuantas uso
 * (0 si no hay ninguna). Cualquier puntero de salida puede ser NULL.
 *   mean_vec : media del vector de aceleracion (aproxima la gravedad en reposo)
 *   std_svm  : desviacion estandar de SVM
 *   mean_dev1: media de |SVM - 1| (actividad)                                  */
static uint16_t ring_window(const fall_detector_t *fd, uint16_t age_start, uint16_t n,
                            float mean_vec[3], float *std_svm, float *mean_dev1)
{
    if (mean_vec) { mean_vec[0] = mean_vec[1] = mean_vec[2] = 0.0f; }
    if (std_svm)   *std_svm   = 0.0f;
    if (mean_dev1) *mean_dev1 = 0.0f;

    if (n == 0u || age_start >= fd->count) return 0u;
    const uint16_t avail = (uint16_t)(fd->count - age_start);
    if (n > avail) n = avail;

    int32_t sx = 0, sy = 0, sz = 0, ss = 0;
    for (uint16_t i = 0; i < n; i++) {
        const fall_ring_item_t *it = ring_at(fd, (uint16_t)(age_start + i));
        sx += it->ax;  sy += it->ay;  sz += it->az;  ss += it->svm;
    }
    const float inv = 1.0f / (1000.0f * (float)n);
    if (mean_vec) {
        mean_vec[0] = (float)sx * inv;
        mean_vec[1] = (float)sy * inv;
        mean_vec[2] = (float)sz * inv;
    }
    if (std_svm || mean_dev1) {
        const float mean_svm = (float)ss * inv;
        float acc = 0.0f, dev = 0.0f;
        for (uint16_t i = 0; i < n; i++) {
            const float v = (float)ring_at(fd, (uint16_t)(age_start + i))->svm / 1000.0f;
            const float d = v - mean_svm;
            acc += d * d;
            dev += fabsf(v - 1.0f);
        }
        if (std_svm)   *std_svm   = sqrtf(acc / (float)n);
        if (mean_dev1) *mean_dev1 = dev / (float)n;
    }
    return n;
}

/* Angulo entre dos vectores de gravedad (grados). 0 si alguno no es fiable. */
static float vec_angle_deg(const float a[3], const float b[3])
{
    const float na = sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
    const float nb = sqrtf(b[0]*b[0] + b[1]*b[1] + b[2]*b[2]);
    if (na < 0.5f || nb < 0.5f) return 0.0f;   /* en reposo |a| ~ 1 g; menos = dato dudoso */
    float c = (a[0]*b[0] + a[1]*b[1] + a[2]*b[2]) / (na * nb);
    if (c >  1.0f) c =  1.0f;
    if (c < -1.0f) c = -1.0f;
    return acosf(c) * RAD2DEG;
}

/* Guarda lo que paso ANTES del evento (la gravedad previa y el ruido previo),
 * porque cuando llegue el momento de clasificar ya habra salido del anillo.
 * `gap_ms` deja un margen para no contaminar la estimacion con el inicio
 * del propio movimiento brusco. */
static void capture_pre(fall_detector_t *fd, uint16_t gap_ms)
{
    const fall_config_t *c = &fd->cfg;
    const uint16_t gap   = ms_to_n(c, gap_ms);
    const uint16_t n_g   = ms_to_n(c, 500u);
    const uint16_t n_std = ms_to_n(c, 1000u);

    float g[3];
    const uint16_t got = ring_window(fd, gap, n_g, g, NULL, NULL);
    fd->pre_valid = (got >= (uint16_t)(n_g / 2u)) && (got > 0u);
    for (int i = 0; i < 3; i++) fd->grav_pre[i] = fd->pre_valid ? g[i] : 0.0f;

    float sd = 0.0f;
    ring_window(fd, gap, n_std, NULL, &sd, NULL);
    fd->feat.std_pre = sd;
}

static void start_refractory(fall_detector_t *fd, uint32_t now_ms)
{
    fd->refractory_until  = now_ms + fd->cfg.refractory_ms;
    fd->refractory_active = true;
}

/* ======================================================================== */
/*  Clasificador por defecto                                                */
/* ======================================================================== */

/* Puntos enteros (no flotantes) para que una puntuacion justo en el umbral
 * no dependa del redondeo. Los pesos son un punto de partida a calibrar. */
float fall_classify_heuristic(const fall_features_t *f)
{
    int pts = 0;
    if (f->svm_max     > 2.6f)   pts += 25;  /* hubo impacto fuerte                       */
    if (f->svm_min     < 0.55f)  pts += 20;  /* hubo caida libre previa                   */
    if (f->delta_angle > 40.0f)  pts += 30;  /* la postura cambio: discriminante principal */
    if (f->std_post    < 0.12f)  pts += 20;  /* quedo quieto tras el impacto              */
    if (f->gyro_peak   > 200.0f) pts += 5;   /* rotacion brusca (si el giroscopio esta activo) */
    return (float)pts / 100.0f;
}

/* ======================================================================== */
/*  API                                                                     */
/* ======================================================================== */

void fall_config_default(fall_config_t *c)
{
    memset(c, 0, sizeof *c);
    c->sample_hz          = 100;
    c->freefall_th_g      = 0.55f;
    c->freefall_min_ms    = 60;
    c->freefall_max_ms    = 400;
    c->impact_th_g        = 2.6f;
    c->impact_direct_th_g = 3.2f;
    c->impact_window_ms   = 600;
    c->peak_window_ms     = 300;
    c->post_window_ms     = 3000;
    c->confirm_wait_ms    = 20000;
    c->refractory_ms      = 10000;
    c->score_th           = 0.70f;
    c->activity_th_g      = 0.05f;
    c->classifier         = NULL;
}

void fall_detector_init(fall_detector_t *fd, const fall_config_t *cfg)
{
    memset(fd, 0, sizeof *fd);
    if (cfg) fd->cfg = *cfg; else fall_config_default(&fd->cfg);
    if (fd->cfg.sample_hz == 0u)  fd->cfg.sample_hz = 100;
    if (!fd->cfg.classifier)      fd->cfg.classifier = fall_classify_heuristic;
    fd->state = FALL_ST_IDLE;
}

void fall_detector_cancel(fall_detector_t *fd)
{
    if (fd->state == FALL_ST_CONFIRM) fd->cancel_req = true;
}

uint8_t fall_detector_take_activity_pct(fall_detector_t *fd)
{
    uint8_t pct = 0;
    if (fd->act_total > 0u) {
        pct = (uint8_t)((fd->act_samples * 100u + fd->act_total / 2u) / fd->act_total);
    }
    fd->act_samples = 0;
    fd->act_total   = 0;
    return pct;
}

const fall_features_t *fall_detector_features(const fall_detector_t *fd) { return &fd->feat; }
fall_state_t fall_detector_state(const fall_detector_t *fd)             { return fd->state; }

/* Pasaron post_window_ms desde el impacto: medir la postura y la inactividad
 * y decidir si es una caida. */
static fall_event_t evaluate_post(fall_detector_t *fd, uint32_t now_ms)
{
    const fall_config_t *c = &fd->cfg;
    fall_features_t *f = &fd->feat;

    /* Gravedad "despues": ultimo segundo, ya en reposo. */
    float grav_post[3];
    ring_window(fd, 0u, ms_to_n(c, 1000u), grav_post, NULL, NULL);

    /* Inactividad: se descarta el primer segundo (el cuerpo aun se asienta). */
    const uint32_t span = (c->post_window_ms > 1000u) ? (c->post_window_ms - 1000u)
                                                      : c->post_window_ms;
    float sd = 0.0f, dev = 0.0f;
    ring_window(fd, 0u, ms_to_n(c, span), NULL, &sd, &dev);
    f->std_post = sd;
    f->act_post = dev;

    f->delta_angle = fd->pre_valid ? vec_angle_deg(fd->grav_pre, grav_post) : 0.0f;

    const float score = c->classifier(f);
    if (score >= c->score_th) {
        fd->state     = FALL_ST_CONFIRM;
        fd->t_confirm = now_ms;
        fd->cancel_req = false;
        fd->n_candidates++;
        return FALL_EVT_CANDIDATE;
    }
    fd->state = FALL_ST_IDLE;
    start_refractory(fd, now_ms);
    fd->n_rejected++;
    return FALL_EVT_REJECTED;
}

fall_event_t fall_detector_process(fall_detector_t *fd, const fall_sample_t *s)
{
    const fall_config_t *c = &fd->cfg;
    fall_features_t *f = &fd->feat;

    const float svm  = sqrtf(s->ax * s->ax + s->ay * s->ay + s->az * s->az);
    const float gyro = sqrtf(s->gx * s->gx + s->gy * s->gy + s->gz * s->gz);

    /* Derivada de SVM con el intervalo real entre muestras (g/s). */
    float jerk = 0.0f;
    if (fd->have_last) {
        const uint32_t dt = s->t_ms - fd->last_t_ms;
        if (dt > 0u && dt <= 200u) jerk = fabsf(svm - fd->last_svm) * 1000.0f / (float)dt;
    }
    fd->last_svm  = svm;
    fd->last_t_ms = s->t_ms;
    fd->have_last = true;

    /* Actigrafia: cuenta siempre, en cualquier estado. */
    fd->act_total++;
    if (fabsf(svm - 1.0f) > c->activity_th_g) fd->act_samples++;

    ring_push(fd, s, svm);

    switch (fd->state)
    {
    case FALL_ST_IDLE:
        /* Periodo refractario: ignora todo tras un evento resuelto.
         * La resta con signo hace que funcione aunque t_ms se desborde. */
        if (fd->refractory_active) {
            if ((int32_t)(s->t_ms - fd->refractory_until) < 0) return FALL_EVT_NONE;
            fd->refractory_active = false;
        }
        if (svm < c->freefall_th_g) {
            memset(f, 0, sizeof *f);
            f->svm_min    = svm;
            fd->t_ff_start = s->t_ms;
            capture_pre(fd, 100u);
            fd->state = FALL_ST_FREEFALL;
        } else if (svm > c->impact_direct_th_g) {
            /* Impacto fuerte sin caida libre (p. ej. caer desde sentada). */
            memset(f, 0, sizeof *f);
            f->svm_min   = 1.0f;
            f->svm_max   = svm;
            f->jerk_max  = jerk;
            f->gyro_peak = gyro;
            capture_pre(fd, 500u);
            fd->t_impact = s->t_ms;
            fd->state = FALL_ST_POST;
        }
        break;

    case FALL_ST_FREEFALL:
        if (svm  < f->svm_min)    f->svm_min   = svm;
        if (svm  > f->svm_max)    f->svm_max   = svm;
        if (gyro > f->gyro_peak)  f->gyro_peak = gyro;
        if (svm >= c->freefall_th_g) {
            const uint32_t dur = s->t_ms - fd->t_ff_start;
            if (dur >= c->freefall_min_ms && dur <= c->freefall_max_ms) {
                f->ff_duration_ms = (float)dur;
                fd->t_ff_end = s->t_ms;
                if (jerk > f->jerk_max) f->jerk_max = jerk;
                fd->state = FALL_ST_IMPACT_WAIT;
            } else {
                fd->state = FALL_ST_IDLE;          /* demasiado corta o demasiado larga */
            }
        } else if ((s->t_ms - fd->t_ff_start) > c->freefall_max_ms) {
            fd->state = FALL_ST_IDLE;              /* sigue "cayendo": no es una caida real */
        }
        break;

    case FALL_ST_IMPACT_WAIT:
        if (svm  > f->svm_max)    f->svm_max   = svm;
        if (jerk > f->jerk_max)   f->jerk_max  = jerk;
        if (gyro > f->gyro_peak)  f->gyro_peak = gyro;
        if (f->svm_max > c->impact_th_g) {
            fd->t_impact = s->t_ms;
            f->impact_delay_ms = (float)(s->t_ms - fd->t_ff_end);
            fd->state = FALL_ST_POST;
        } else if ((s->t_ms - fd->t_ff_end) > c->impact_window_ms) {
            fd->state = FALL_ST_IDLE;              /* caida libre sin impacto */
        }
        break;

    case FALL_ST_POST: {
        const uint32_t since = s->t_ms - fd->t_impact;
        if (since <= c->peak_window_ms) {          /* el pico del impacto, no el movimiento posterior */
            if (svm  > f->svm_max)   f->svm_max   = svm;
            if (jerk > f->jerk_max)  f->jerk_max  = jerk;
            if (gyro > f->gyro_peak) f->gyro_peak = gyro;
        }
        if (since >= c->post_window_ms) return evaluate_post(fd, s->t_ms);
        break;
    }

    case FALL_ST_CONFIRM:
        if (fd->cancel_req) {
            fd->cancel_req = false;
            fd->state = FALL_ST_IDLE;
            start_refractory(fd, s->t_ms);
            fd->n_cancelled++;
            return FALL_EVT_CANCELLED;
        }
        if ((s->t_ms - fd->t_confirm) >= c->confirm_wait_ms) {
            fd->state = FALL_ST_IDLE;
            start_refractory(fd, s->t_ms);
            fd->n_confirmed++;
            return FALL_EVT_CONFIRMED;
        }
        break;
    }
    return FALL_EVT_NONE;
}
