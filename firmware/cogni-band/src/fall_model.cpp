#include "fall_model.h"

#include <cmath>
#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "../models/fall_detector/fall_model_preprocess.h"

extern "C" const unsigned char fall_detector_model_data[];
extern "C" const unsigned int fall_detector_model_data_len;

namespace {

constexpr char TAG[] = "fall_model";
constexpr size_t kWindowValues =
    FALL_MODEL_WINDOW_SAMPLES * FALL_MODEL_CHANNELS;
constexpr size_t kTensorArenaSize = 64u * 1024u;

alignas(16) uint8_t tensor_arena[kTensorArenaSize];
float sample_ring[FALL_MODEL_WINDOW_SAMPLES][FALL_MODEL_CHANNELS];
uint16_t ring_head;
uint16_t ring_count;
uint16_t samples_after_first_window;
uint32_t slow_inferences;

const tflite::Model *model;
tflite::MicroInterpreter *interpreter;
TfLiteTensor *input_tensor;
TfLiteTensor *output_tensor;

float clamp(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

}  // namespace

extern "C" bool fall_model_init(void)
{
    memset(sample_ring, 0, sizeof sample_ring);
    ring_head = 0;
    ring_count = 0;
    samples_after_first_window = 0;
    slow_inferences = 0;

    const size_t model_size = fall_detector_model_data_len;
    model = tflite::GetModel(fall_detector_model_data);
    if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Modelo TFLite ausente o incompatible (schema %d, esperado %d)",
                 model != nullptr ? model->version() : -1,
                 TFLITE_SCHEMA_VERSION);
        return false;
    }

    static tflite::MicroMutableOpResolver<12> resolver;
    if (resolver.AddConv2D() != kTfLiteOk ||
        resolver.AddMaxPool2D() != kTfLiteOk ||
        resolver.AddMul() != kTfLiteOk ||
        resolver.AddAdd() != kTfLiteOk ||
        resolver.AddFullyConnected() != kTfLiteOk ||
        resolver.AddLogistic() != kTfLiteOk ||
        resolver.AddReshape() != kTfLiteOk ||
        resolver.AddExpandDims() != kTfLiteOk ||
        resolver.AddShape() != kTfLiteOk ||
        resolver.AddStridedSlice() != kTfLiteOk ||
        resolver.AddPack() != kTfLiteOk) {
        ESP_LOGE(TAG, "No se pudieron registrar los operadores TFLite");
        return false;
    }

    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, sizeof tensor_arena);
    interpreter = &static_interpreter;
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors fallo; arena disponible: %u bytes",
                 (unsigned)sizeof tensor_arena);
        return false;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);
    if (input_tensor == nullptr || output_tensor == nullptr ||
        input_tensor->type != kTfLiteInt8 ||
        output_tensor->type != kTfLiteInt8 ||
        input_tensor->dims == nullptr || input_tensor->dims->size != 3 ||
        input_tensor->dims->data[0] != 1 ||
        input_tensor->dims->data[1] != (int)FALL_MODEL_WINDOW_SAMPLES ||
        input_tensor->dims->data[2] != (int)FALL_MODEL_CHANNELS ||
        input_tensor->bytes != kWindowValues ||
        output_tensor->bytes != sizeof(int8_t) ||
        fabsf(input_tensor->params.scale - FALL_MODEL_INPUT_SCALE) > 1e-6f ||
        input_tensor->params.zero_point != FALL_MODEL_INPUT_ZERO_POINT ||
        fabsf(output_tensor->params.scale - FALL_MODEL_OUTPUT_SCALE) > 1e-6f ||
        output_tensor->params.zero_point != FALL_MODEL_OUTPUT_ZERO_POINT) {
        ESP_LOGE(TAG, "Forma/tipo/cuantizacion del modelo no coincide con los metadatos");
        return false;
    }

    ESP_LOGI(TAG, "Modelo listo: %u bytes, ventana %u x %u, arena %u bytes",
             (unsigned)model_size, (unsigned)FALL_MODEL_WINDOW_SAMPLES,
             (unsigned)FALL_MODEL_CHANNELS, (unsigned)sizeof tensor_arena);
    return true;
}

extern "C" bool fall_model_append_sample(const fall_sample_t *sample,
                                          fall_model_window_t *window)
{
    if (sample == nullptr || window == nullptr) return false;

    const bool window_was_full = ring_count == FALL_MODEL_WINDOW_SAMPLES;
    float *row = sample_ring[ring_head];
    row[0] = sample->ax;
    row[1] = -sample->az;
    row[2] = sample->ay;
    row[3] = sample->gx;
    row[4] = -sample->gz;
    row[5] = sample->gy;

    ring_head = (uint16_t)((ring_head + 1u) % FALL_MODEL_WINDOW_SAMPLES);
    if (ring_count < FALL_MODEL_WINDOW_SAMPLES) ring_count++;
    if (ring_count < FALL_MODEL_WINDOW_SAMPLES) return false;

    if (window_was_full) {
        if (++samples_after_first_window < FALL_MODEL_INFERENCE_STRIDE) return false;
        samples_after_first_window = 0;
    }

    window->peak_g = 0.0f;
    window->t_ms = sample->t_ms;
    for (uint16_t i = 0; i < FALL_MODEL_WINDOW_SAMPLES; i++) {
        const uint16_t source =
            (uint16_t)((ring_head + i) % FALL_MODEL_WINDOW_SAMPLES);
        const size_t destination = (size_t)i * FALL_MODEL_CHANNELS;
        memcpy(&window->values[destination], sample_ring[source],
               sizeof sample_ring[source]);
        const float *values = sample_ring[source];
        const float magnitude =
            sqrtf(values[0] * values[0] + values[1] * values[1] +
                  values[2] * values[2]);
        if (magnitude > window->peak_g) window->peak_g = magnitude;
    }
    return true;
}

extern "C" bool fall_model_infer(const fall_model_window_t *window,
                                  float *probability)
{
    if (window == nullptr || probability == nullptr || interpreter == nullptr ||
        input_tensor == nullptr || output_tensor == nullptr) {
        ESP_LOGE(TAG, "Inferencia solicitada antes de inicializar el modelo");
        return false;
    }

    const int64_t start_us = esp_timer_get_time();
    for (size_t i = 0; i < kWindowValues; i++) {
        const size_t channel = i % FALL_MODEL_CHANNELS;
        const float value =
            (window->values[i] - fall_model_mean[channel]) /
            fall_model_scale[channel];
        if (!std::isfinite(value)) {
            ESP_LOGE(TAG, "Muestra no finita en indice %u", (unsigned)i);
            return false;
        }

        const float quantized =
            value / input_tensor->params.scale +
            (float)input_tensor->params.zero_point;
        const long rounded = lroundf(quantized);
        input_tensor->data.int8[i] =
            (int8_t)(rounded < -128 ? -128 : (rounded > 127 ? 127 : rounded));
    }

    if (interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke fallo");
        return false;
    }

    const int32_t quantized_output = output_tensor->data.int8[0];
    *probability = clamp(
        (float)(quantized_output - output_tensor->params.zero_point) *
            output_tensor->params.scale,
        0.0f, 1.0f);
    const int64_t elapsed_us = esp_timer_get_time() - start_us;
    if (elapsed_us > 10000 && ++slow_inferences % 100u == 1u) {
        ESP_LOGW(TAG, "Inferencia excedio el periodo IMU: %lld us",
                 (long long)elapsed_us);
    }
    return true;
}
