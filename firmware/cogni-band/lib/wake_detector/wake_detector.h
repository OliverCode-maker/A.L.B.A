/**
 * wake_detector.h
 * Detector de la transicion sueno -> vigilia matutina a partir de la
 * frecuencia cardiaca y la actigrafia. C puro (se prueba en el PC).
 *
 * Idea: durante la noche se aprende la FC basal. Cuando la FC sube de forma
 * sostenida (umbral relativo Y absoluto), con movimiento que lo corrobora y
 * dentro de la ventana horaria de la manana, se dispara UN evento que activa
 * la estimulacion cognitiva en el robot.
 *
 *   SLEEP --FC sube y pendiente>=3 bpm/min--> RISING --sostenido 3 min--> AWAKE
 *     ^                                          |                          |
 *     +------- la subida se revierte / timeout --+                          |
 *     +------------------- FC baja y quieto ------------------------------- +
 *
 * El detector NO decide cuando medir: expone el periodo recomendado
 * (wake_detector_sampling_period_s) para que la tarea de signos vitales lo
 * use. Asi se mide poco de noche (ahorro de bateria) y mas seguido solo
 * cuando hay sospecha de despertar.
 *
 * Los umbrales por defecto son un punto de partida: calibra con una noche
 * real de registro y compara el BPM contra un pulsioximetro comercial.
 */
#ifndef ALBA_WAKE_DETECTOR_H
#define ALBA_WAKE_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAKE_HIST_LEN 24     /* historial de lecturas aceptadas */

/** Una lectura de signos vitales. */
typedef struct {
    uint32_t t_s;        /* segundos (monotonicos; vale desde el arranque o epoch) */
    uint8_t  bpm;        /* frecuencia cardiaca                                    */
    uint8_t  sqi;        /* calidad de la senal PPG, 0-100                         */
    uint8_t  act_pct;    /* % de actividad en el intervalo (fall_detector_take_activity_pct) */
} wake_vital_t;

typedef enum { WAKE_SLEEP = 0, WAKE_RISING, WAKE_AWAKE } wake_state_t;
typedef enum { WAKE_EVT_NONE = 0, WAKE_EVT_TRIGGER } wake_event_t;

typedef struct {
    /* control de calidad */
    uint8_t  sqi_min;               /* lecturas con menos calidad se descartan (60)   */
    uint8_t  bpm_min, bpm_max;      /* rango fisiologico aceptado (35 - 180)          */
    /* linea base */
    float    baseline_alpha;        /* suavizado EMA de la FC basal (0.02)            */
    uint8_t  act_quiet_pct;         /* por debajo = paciente quieto (5)               */
    uint8_t  act_awake_pct;         /* por encima = movimiento que corrobora (15)     */
    /* deteccion de la subida */
    float    rise_enter_delta_bpm;  /* FC sobre la basal para sospechar (15)          */
    float    rise_slope_bpm_min;    /* pendiente minima en bpm/min (3)                */
    float    rise_abort_delta_bpm;  /* si baja de basal+8 se aborta (8)               */
    float    awake_abs_bpm;         /* criterio absoluto de despertar (80)            */
    float    awake_rel_delta_bpm;   /* criterio relativo: basal + 20                  */
    float    sleep_return_delta_bpm;/* AWAKE -> SLEEP cuando FC < basal+10 y quieto   */
    uint16_t slope_window_s;        /* ventana de la regresion de pendiente (180)     */
    uint16_t rise_confirm_s;        /* la subida debe sostenerse tanto tiempo (180)   */
    uint16_t rising_timeout_s;      /* sospecha sin confirmar vuelve a SLEEP (900)    */
    /* contexto */
    uint8_t  hour_min, hour_max;    /* ventana horaria local de la manana (4 - 11)    */
    uint32_t refractory_s;          /* minimo entre dos disparos (21600 = 6 h)        */
    /* periodo de medicion recomendado por estado */
    uint16_t period_sleep_s;        /* 60  */
    uint16_t period_rising_s;       /* 15  */
    uint16_t period_awake_s;        /* 300 */
} wake_config_t;

typedef struct { uint32_t t_s; uint8_t bpm; } wake_hr_item_t;

typedef struct {
    wake_config_t  cfg;
    wake_state_t   state;
    wake_hr_item_t hist[WAKE_HIST_LEN];
    uint8_t        hist_head;
    uint8_t        hist_n;
    float          baseline;
    bool           baseline_ready;
    uint32_t       t_rise_start;
    uint32_t       t_last_trigger;
    bool           has_triggered;
    uint32_t       n_rejected;      /* lecturas descartadas por calidad/rango */
} wake_detector_t;

void wake_config_default(wake_config_t *cfg);

/** `cfg` puede ser NULL para usar los valores por defecto. */
void wake_detector_init(wake_detector_t *w, const wake_config_t *cfg);

/** Procesa una lectura. `hour_local` es la hora local (0-23); la pulsera la
 *  recibe del robot, que sincroniza la hora real. Devuelve TRIGGER una sola
 *  vez por despertar. */
wake_event_t wake_detector_update(wake_detector_t *w, const wake_vital_t *v, uint8_t hour_local);

/** Cada cuantos segundos conviene medir segun el estado actual. */
uint16_t wake_detector_sampling_period_s(const wake_detector_t *w);

wake_state_t wake_detector_state(const wake_detector_t *w);
float        wake_detector_baseline(const wake_detector_t *w);

#ifdef __cplusplus
}
#endif

#endif /* ALBA_WAKE_DETECTOR_H */
