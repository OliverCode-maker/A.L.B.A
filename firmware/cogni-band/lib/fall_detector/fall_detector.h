/**
 * fall_detector.h
 * Detector de caidas para la pulsera. C puro: no depende de Arduino ni de
 * FreeRTOS, por eso se puede probar en el PC (pio test -e native).
 *
 * Pipeline (una muestra IMU a la vez, ~100 Hz):
 *
 *   IDLE --SVM<0.55g--> FREEFALL --fin caida libre--> IMPACT_WAIT --SVM>2.6g--> POST
 *     |                                                                          |
 *     +-------------------- SVM>3.2g (impacto directo) ------------------------>+
 *                                                                                |
 *                                    3 s despues: extraer caracteristicas + clasificar
 *                                                  |                  |
 *                                              score<umbral       score>=umbral
 *                                                  v                  v
 *                                           IDLE (REJECTED)   CONFIRM (CANDIDATE)
 *                                                               |            |
 *                                                       cancel()|            | 20 s sin cancelar
 *                                                               v            v
 *                                                  IDLE (CANCELLED)   IDLE (CONFIRMED)
 *
 * Uso tipico (en la tarea tFallDetect, con el detector declarado `static`
 * porque ocupa ~2.7 kB y no debe ir en el stack):
 *
 *     static fall_detector_t fd;
 *     fall_detector_init(&fd, NULL);                  // configuracion por defecto
 *     ...
 *     switch (fall_detector_process(&fd, &muestra)) {
 *         case FALL_EVT_CANDIDATE: haptic_alert(); enviar(PENDIENTE); break;
 *         case FALL_EVT_CONFIRMED: enviar(CONFIRMADA);                 break;
 *         case FALL_EVT_CANCELLED: enviar(CANCELADA);                  break;
 *         default: break;
 *     }
 *
 * NO es thread-safe: llama a todas las funciones desde la misma tarea.
 * Los umbrales por defecto son un PUNTO DE PARTIDA y deben calibrarse con
 * datos reales de la pulsera puesta en la muneca.
 */
#ifndef ALBA_FALL_DETECTOR_H
#define ALBA_FALL_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Muestras de historial. A 100 Hz son 3.2 s (8 bytes por muestra = 2.5 kB). */
#define FALL_RING_LEN 320

/** Una muestra del IMU ya convertida a unidades fisicas. */
typedef struct {
    uint32_t t_ms;          /* marca de tiempo en ms (monotonica)           */
    float ax, ay, az;       /* aceleracion en g  (configura el MPU a +-16 g) */
    float gx, gy, gz;       /* velocidad angular en grados/s (0 si no se usa) */
} fall_sample_t;

/** Caracteristicas del evento; es tambien el vector de entrada del clasificador. */
typedef struct {
    float svm_max;          /* pico de |a| tras el impacto (g)                    */
    float svm_min;          /* minimo de |a| en la caida libre (g)                */
    float jerk_max;         /* maximo |d(SVM)/dt| alrededor del impacto (g/s)     */
    float std_pre;          /* desv. estandar de SVM ~1 s antes del evento (g)    */
    float std_post;         /* desv. estandar de SVM en el reposo posterior (g)   */
    float act_post;         /* media de |SVM-1| en el reposo posterior (g)        */
    float delta_angle;      /* cambio de orientacion del vector gravedad (grados) */
    float gyro_peak;        /* pico de velocidad angular (grados/s)               */
    float ff_duration_ms;   /* duracion de la caida libre (ms)                    */
    float impact_delay_ms;  /* ms entre el fin de la caida libre y el impacto     */
} fall_features_t;

/** Clasificador: recibe las caracteristicas y devuelve una puntuacion en [0,1].
 *  Aqui se enchufa el modelo entrenado (por ejemplo un arbol exportado con
 *  emlearn) sin tocar la maquina de estados. */
typedef float (*fall_classifier_fn)(const fall_features_t *features);

/** Resultado de procesar una muestra. */
typedef enum {
    FALL_EVT_NONE = 0,
    FALL_EVT_CANDIDATE,     /* posible caida: activar vibracion y abrir ventana de cancelacion */
    FALL_EVT_CONFIRMED,     /* paso la ventana sin cancelar: avisar al cuidador                */
    FALL_EVT_CANCELLED,     /* el usuario cancelo: era falsa alarma                            */
    FALL_EVT_REJECTED       /* el clasificador descarto el evento (movimiento cotidiano)       */
} fall_event_t;

typedef enum {
    FALL_ST_IDLE = 0,
    FALL_ST_FREEFALL,
    FALL_ST_IMPACT_WAIT,
    FALL_ST_POST,
    FALL_ST_CONFIRM
} fall_state_t;

/** Parametros ajustables. fall_config_default() da los valores iniciales. */
typedef struct {
    uint16_t sample_hz;             /* frecuencia nominal del IMU (100)                       */
    float    freefall_th_g;         /* SVM por debajo de esto = caida libre (0.55)            */
    uint16_t freefall_min_ms;       /* caida libre minima valida (60)                         */
    uint16_t freefall_max_ms;       /* caida libre maxima valida (400)                        */
    float    impact_th_g;           /* pico minimo de impacto tras caida libre (2.6)          */
    float    impact_direct_th_g;    /* impacto sin caida libre, p. ej. desde sentada (3.2)    */
    uint16_t impact_window_ms;      /* tiempo maximo entre caida libre e impacto (600)        */
    uint16_t peak_window_ms;        /* ventana tras el impacto donde se busca el pico (300)   */
    uint16_t post_window_ms;        /* ventana de analisis de postura/inactividad (3000)      */
    uint32_t confirm_wait_ms;       /* ventana para cancelar antes de avisar (20000)          */
    uint32_t refractory_ms;         /* silencio tras cada evento resuelto (10000)             */
    float    score_th;              /* puntuacion minima para considerarlo caida (0.70)       */
    float    activity_th_g;         /* |SVM-1| sobre esto cuenta como actividad (0.05)        */
    fall_classifier_fn classifier;  /* NULL = heuristica por defecto                          */
} fall_config_t;

/* --- Estado interno (se declara aqui solo para poder asignarlo estaticamente) --- */
typedef struct { int16_t ax, ay, az, svm; } fall_ring_item_t;   /* en mili-g */

typedef struct {
    fall_config_t    cfg;
    fall_state_t     state;

    fall_ring_item_t ring[FALL_RING_LEN];
    uint16_t         head;          /* proxima posicion de escritura */
    uint16_t         count;         /* muestras validas en el anillo  */

    uint32_t t_ff_start, t_ff_end, t_impact, t_confirm, refractory_until;
    bool     refractory_active;
    bool     cancel_req;

    float    grav_pre[3];           /* gravedad estimada antes del evento */
    bool     pre_valid;
    fall_features_t feat;

    float    last_svm;
    uint32_t last_t_ms;
    bool     have_last;

    uint32_t act_samples, act_total; /* para la actigrafia (porcentaje) */

    /* contadores utiles para depurar y para el informe */
    uint32_t n_candidates, n_confirmed, n_cancelled, n_rejected;
} fall_detector_t;

/** Rellena `cfg` con los valores por defecto. */
void fall_config_default(fall_config_t *cfg);

/** Inicializa el detector. `cfg` puede ser NULL para usar los valores por defecto. */
void fall_detector_init(fall_detector_t *fd, const fall_config_t *cfg);

/** Procesa UNA muestra del IMU. Llamar a frecuencia constante (~100 Hz). */
fall_event_t fall_detector_process(fall_detector_t *fd, const fall_sample_t *sample);

/** El usuario (o el cuidador) pulso "cancelar". Solo tiene efecto en estado CONFIRM;
 *  el evento FALL_EVT_CANCELLED se devuelve en la siguiente llamada a process(). */
void fall_detector_cancel(fall_detector_t *fd);

/** Porcentaje (0-100) de muestras "activas" desde la ultima llamada; luego reinicia.
 *  Es la actigrafia que consume el detector de vigilia. */
uint8_t fall_detector_take_activity_pct(fall_detector_t *fd);

/** Ultimas caracteristicas evaluadas (validas tras CANDIDATE o REJECTED). */
const fall_features_t *fall_detector_features(const fall_detector_t *fd);

fall_state_t fall_detector_state(const fall_detector_t *fd);

/** Heuristica ponderada por defecto (puntos sobre 100). Es publica para poder
 *  compararla contra el modelo entrenado. */
float fall_classify_heuristic(const fall_features_t *features);

#ifdef __cplusplus
}
#endif

#endif /* ALBA_FALL_DETECTOR_H */
