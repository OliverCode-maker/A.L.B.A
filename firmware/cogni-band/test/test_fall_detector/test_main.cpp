// Pruebas del detector de caidas con senales sinteticas (100 Hz).
// Correr con:  pio test -e native -f test_fall_detector
//
// IMPORTANTE: estas pruebas verifican que la LOGICA hace lo disenado. No
// demuestran que los umbrales sirvan en una muneca real: eso requiere
// caidas simuladas con la pulsera puesta (ver el protocolo de captura).
#include <unity.h>
#include <math.h>
#include <string.h>

#include "fall_detector.h"

static fall_detector_t fd;
static uint32_t T;          // reloj de la simulacion (ms)
static uint32_t rng;
static int n_cand, n_conf, n_canc, n_rej;

// Ruido pseudoaleatorio determinista en [-amp, +amp]
static float noise(float amp)
{
    rng = rng * 1664525u + 1013904223u;
    return ((float)((rng >> 8) & 0xFFFFu) / 65535.0f * 2.0f - 1.0f) * amp;
}

static void count_event(fall_event_t e)
{
    switch (e) {
    case FALL_EVT_CANDIDATE: n_cand++; break;
    case FALL_EVT_CONFIRMED: n_conf++; break;
    case FALL_EVT_CANCELLED: n_canc++; break;
    case FALL_EVT_REJECTED:  n_rej++;  break;
    default: break;
    }
}

// Alimenta UNA muestra y avanza el reloj 10 ms.
static fall_event_t feed(float ax, float ay, float az, float gmag = 0.0f)
{
    fall_sample_t s;
    memset(&s, 0, sizeof s);
    s.t_ms = T;  s.ax = ax;  s.ay = ay;  s.az = az;  s.gx = gmag;
    T += 10;
    const fall_event_t e = fall_detector_process(&fd, &s);
    count_event(e);
    return e;
}

// Reposo en una orientacion dada, `ms` milisegundos, con ruido `amp` por eje.
static void rest(uint32_t ms, float ax, float ay, float az, float amp)
{
    for (uint32_t t = 0; t < ms; t += 10)
        feed(ax + noise(amp), ay + noise(amp), az + noise(amp));
}

// Caida realista: brazo colgando (gravedad en X) -> 250 ms de caida libre ->
// impacto -> el cuerpo queda tendido (gravedad en Z) y quieto.
static void do_fall(float gyro_dps)
{
    rest(2000, 1.0f, 0.0f, 0.0f, 0.02f);
    for (int i = 0; i < 25; i++)
        feed(0.3f + noise(0.02f), noise(0.02f), noise(0.02f), gyro_dps);
    feed(0.0f, 0.0f, 4.0f);
    feed(0.0f, 0.0f, 2.8f);
    feed(0.0f, 0.5f, 1.8f);
    rest(4000, 0.0f, 0.0f, 1.0f, 0.02f);
}

// Golpe/sentada brusca: impacto de `peak` g sin caida libre, y el usuario
// sigue moviendose normalmente (ruido grande, sin cambio de postura).
static void do_bump(float peak)
{
    rest(2000, 0.0f, 0.0f, 1.0f, 0.02f);
    feed(0.0f, 0.0f, 0.8f);                 // pequeno hundimiento, no llega a caida libre
    feed(0.0f, 0.0f, peak);
    rest(4000, 0.0f, 0.0f, 1.0f, 0.40f);
}

static float always_one(const fall_features_t *) { return 1.0f; }

void setUp(void)
{
    fall_detector_init(&fd, NULL);
    T = 1000;  rng = 12345;
    n_cand = n_conf = n_canc = n_rej = 0;
}
void tearDown(void) {}

// ----------------------------------------------------------------------------

void test_quiet_signal_no_events(void)
{
    rest(10000, 0.0f, 0.0f, 1.0f, 0.02f);
    TEST_ASSERT_EQUAL_INT(0, n_cand + n_conf + n_canc + n_rej);
    TEST_ASSERT_EQUAL_INT(FALL_ST_IDLE, fall_detector_state(&fd));
    TEST_ASSERT_EQUAL_UINT8(0, fall_detector_take_activity_pct(&fd));
}

void test_real_fall_is_candidate_then_confirmed(void)
{
    do_fall(250.0f);

    TEST_ASSERT_EQUAL_INT(1, n_cand);
    TEST_ASSERT_EQUAL_INT(FALL_ST_CONFIRM, fall_detector_state(&fd));

    const fall_features_t *f = fall_detector_features(&fd);
    TEST_ASSERT_TRUE(f->svm_max > 2.6f);
    TEST_ASSERT_TRUE(f->svm_min < 0.55f);
    TEST_ASSERT_FLOAT_WITHIN(60.0f, 250.0f, f->ff_duration_ms);   // ~250 ms +- 60
    TEST_ASSERT_FLOAT_WITHIN(8.0f, 90.0f, f->delta_angle);        // X -> Z = 90 grados
    TEST_ASSERT_TRUE(f->std_post < 0.12f);
    TEST_ASSERT_TRUE(f->gyro_peak >= 250.0f);

    // Nadie cancela: a los 20 s debe confirmarse.
    rest(21000, 0.0f, 0.0f, 1.0f, 0.02f);
    TEST_ASSERT_EQUAL_INT(1, n_conf);
    TEST_ASSERT_EQUAL_INT(0, n_canc);
    TEST_ASSERT_EQUAL_INT(FALL_ST_IDLE, fall_detector_state(&fd));
}

void test_real_fall_without_gyro_still_detected(void)
{
    do_fall(0.0f);   // giroscopio apagado para ahorrar bateria
    TEST_ASSERT_EQUAL_INT(1, n_cand);
}

void test_cancel_prevents_confirmation(void)
{
    do_fall(0.0f);
    TEST_ASSERT_EQUAL_INT(FALL_ST_CONFIRM, fall_detector_state(&fd));

    fall_detector_cancel(&fd);
    feed(0.0f, 0.0f, 1.0f);                       // el evento sale en la siguiente muestra
    TEST_ASSERT_EQUAL_INT(1, n_canc);

    rest(25000, 0.0f, 0.0f, 1.0f, 0.02f);
    TEST_ASSERT_EQUAL_INT(0, n_conf);
}

void test_cancel_outside_confirm_window_is_ignored(void)
{
    rest(3000, 0.0f, 0.0f, 1.0f, 0.02f);
    fall_detector_cancel(&fd);
    rest(1000, 0.0f, 0.0f, 1.0f, 0.02f);
    TEST_ASSERT_EQUAL_INT(0, n_canc);
}

void test_hard_bump_without_posture_change_is_rejected(void)
{
    do_bump(3.5f);                                // sentarse de golpe / golpear la mesa
    TEST_ASSERT_EQUAL_INT(0, n_cand);
    TEST_ASSERT_EQUAL_INT(1, n_rej);
    TEST_ASSERT_TRUE(fall_detector_features(&fd)->delta_angle < 40.0f);
}

void test_moderate_bump_does_not_even_trigger(void)
{
    do_bump(2.8f);                                // bajo el umbral de impacto directo
    TEST_ASSERT_EQUAL_INT(0, n_cand + n_rej);
    TEST_ASSERT_EQUAL_INT(FALL_ST_IDLE, fall_detector_state(&fd));
}

void test_freefall_too_short_is_ignored(void)
{
    rest(2000, 0.0f, 0.0f, 1.0f, 0.02f);
    for (int i = 0; i < 3; i++) feed(0.3f, 0.0f, 0.0f);   // 30 ms
    feed(0.0f, 0.0f, 4.0f);
    rest(4000, 0.0f, 0.0f, 1.0f, 0.02f);
    TEST_ASSERT_EQUAL_INT(0, n_cand + n_rej);
}

void test_freefall_without_impact_times_out(void)
{
    rest(2000, 0.0f, 0.0f, 1.0f, 0.02f);
    for (int i = 0; i < 25; i++) feed(0.3f, 0.0f, 0.0f);  // brazo en el aire...
    rest(3000, 0.0f, 0.0f, 1.0f, 0.02f);                  // ...y vuelve sin golpe
    TEST_ASSERT_EQUAL_INT(0, n_cand + n_rej);
    TEST_ASSERT_EQUAL_INT(FALL_ST_IDLE, fall_detector_state(&fd));
}

void test_refractory_period_suppresses_cascade(void)
{
    do_bump(3.5f);                                // evento 1 -> rechazado
    TEST_ASSERT_EQUAL_INT(1, n_rej);

    do_bump(3.5f);                                // ~3 s despues: dentro de los 10 s
    TEST_ASSERT_EQUAL_INT(1, n_rej);

    rest(10000, 0.0f, 0.0f, 1.0f, 0.02f);         // pasa el periodo refractario
    do_bump(3.5f);
    TEST_ASSERT_EQUAL_INT(2, n_rej);
}

void test_activity_percentage(void)
{
    for (int i = 0; i < 100; i++)                 // mitad de las muestras con |SVM-1| = 0.10 g
        feed(0.0f, 0.0f, (i % 2) ? 1.10f : 1.00f);
    TEST_ASSERT_EQUAL_UINT8(50, fall_detector_take_activity_pct(&fd));
    TEST_ASSERT_EQUAL_UINT8(0,  fall_detector_take_activity_pct(&fd));   // se reinicio
}

void test_custom_classifier_is_used(void)
{
    fall_config_t cfg;
    fall_config_default(&cfg);
    cfg.classifier = always_one;
    fall_detector_init(&fd, &cfg);

    do_bump(3.5f);                                // la heuristica lo rechazaria; el modelo no
    TEST_ASSERT_EQUAL_INT(1, n_cand);
}

void test_no_history_at_boot_does_not_crash(void)
{
    T = 0;                                        // la caida empieza con el anillo vacio
    for (int i = 0; i < 25; i++) feed(0.3f, 0.0f, 0.0f);
    feed(0.0f, 0.0f, 4.0f);
    feed(0.0f, 0.0f, 2.8f);
    rest(4000, 0.0f, 0.0f, 1.0f, 0.02f);
    // Sin postura previa no se puede medir el cambio de orientacion.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fall_detector_features(&fd)->delta_angle);
    TEST_ASSERT_EQUAL_INT(1, n_cand + n_rej);     // se resolvio, sin colgarse
}

void test_timestamp_wraparound(void)
{
    T = 0xFFFFFFFFu - 3000u;                      // el reloj de 32 bits se desborda a mitad de la caida
    do_fall(0.0f);
    TEST_ASSERT_EQUAL_INT(1, n_cand);
    rest(21000, 0.0f, 0.0f, 1.0f, 0.02f);
    TEST_ASSERT_EQUAL_INT(1, n_conf);
}

void test_ram_footprint(void)
{
    TEST_ASSERT_TRUE(sizeof(fall_detector_t) < 3000);   // cabe holgado en los 400 kB del C3
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_quiet_signal_no_events);
    RUN_TEST(test_real_fall_is_candidate_then_confirmed);
    RUN_TEST(test_real_fall_without_gyro_still_detected);
    RUN_TEST(test_cancel_prevents_confirmation);
    RUN_TEST(test_cancel_outside_confirm_window_is_ignored);
    RUN_TEST(test_hard_bump_without_posture_change_is_rejected);
    RUN_TEST(test_moderate_bump_does_not_even_trigger);
    RUN_TEST(test_freefall_too_short_is_ignored);
    RUN_TEST(test_freefall_without_impact_times_out);
    RUN_TEST(test_refractory_period_suppresses_cascade);
    RUN_TEST(test_activity_percentage);
    RUN_TEST(test_custom_classifier_is_used);
    RUN_TEST(test_no_history_at_boot_does_not_crash);
    RUN_TEST(test_timestamp_wraparound);
    RUN_TEST(test_ram_footprint);
    return UNITY_END();
}
