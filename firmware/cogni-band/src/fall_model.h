#ifndef ALBA_FALL_MODEL_H
#define ALBA_FALL_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "fall_detector.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FALL_MODEL_WINDOW_SAMPLES 200u
#define FALL_MODEL_CHANNELS 6u
#define FALL_MODEL_INFERENCE_STRIDE 25u

typedef enum {
    FALL_RULE_CLASS_NON_FALL = 0,
    FALL_RULE_CLASS_PRE_IMPACT,
    FALL_RULE_CLASS_FALL,
    FALL_RULE_CLASS_COUNT
} fall_rule_class_t;

typedef struct {
    float values[FALL_MODEL_WINDOW_SAMPLES * FALL_MODEL_CHANNELS];
    float peak_g;
    uint32_t t_ms;
    fall_rule_class_t rule_class;
} fall_model_window_t;

bool fall_model_init(void);

/* Returns true when a new full window should be queued for inference. */
bool fall_model_append_sample(const fall_sample_t *sample, fall_model_window_t *window);

/* Call only from the inference task after fall_model_init succeeds. */
bool fall_model_infer(const fall_model_window_t *window, float *probability);

#ifdef __cplusplus
}
#endif

#endif
