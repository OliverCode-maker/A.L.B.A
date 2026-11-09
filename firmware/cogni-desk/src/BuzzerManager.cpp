#include "BuzzerManager.h"

#include <Arduino.h>

#include "DeskConfig.h"

// Funciona con buzzer activo o pasivo: se le manda una onda cuadrada con tone().
namespace {
struct Pattern {
  uint16_t frequency_hz;
  uint16_t on_ms;
  uint16_t off_ms;
};

const Pattern kFall = {2700u, 120u, 120u};
const Pattern kFallConfirmed = {2000u, 700u, 300u};
const Pattern kMedication = {2400u, 250u, 1250u};

buzzer_pattern_t current = BUZZER_PATTERN_OFF;
uint32_t pattern_started_ms;
uint32_t phase_started_ms;
bool tone_on;

const Pattern *patternData(buzzer_pattern_t pattern) {
  switch (pattern) {
  case BUZZER_PATTERN_FALL: return &kFall;
  case BUZZER_PATTERN_FALL_CONFIRMED: return &kFallConfirmed;
  case BUZZER_PATTERN_MEDICATION: return &kMedication;
  default: return nullptr;
  }
}

void silence() {
  if (tone_on) noTone(DESK_BUZZER_GPIO);
  digitalWrite(DESK_BUZZER_GPIO, LOW);
  tone_on = false;
}
}

void initBuzzer(void) {
  pinMode(DESK_BUZZER_GPIO, OUTPUT);
  digitalWrite(DESK_BUZZER_GPIO, LOW);
  current = BUZZER_PATTERN_OFF;
  tone_on = false;
}

void actualizarBuzzer(buzzer_pattern_t pattern, uint32_t now_ms) {
  if (pattern != current) {
    silence();
    current = pattern;
    pattern_started_ms = now_ms;
    phase_started_ms = now_ms;
  }

  const Pattern *data = patternData(current);
  if (data == nullptr) return;

  if (current == BUZZER_PATTERN_MEDICATION &&
      static_cast<uint32_t>(now_ms - pattern_started_ms) >= DESK_MEDICATION_BUZZ_MAX_MS) {
    silence();
    return;
  }

  const uint32_t elapsed = static_cast<uint32_t>(now_ms - phase_started_ms);
  if (!tone_on) {
    // Arranca sonando al cambiar de patron (phase_started_ms == pattern_started_ms) o tras la pausa.
    if (current != BUZZER_PATTERN_OFF && (elapsed >= data->off_ms || now_ms == pattern_started_ms)) {
      tone(DESK_BUZZER_GPIO, data->frequency_hz);
      tone_on = true;
      phase_started_ms = now_ms;
    }
  } else if (elapsed >= data->on_ms) {
    noTone(DESK_BUZZER_GPIO);
    digitalWrite(DESK_BUZZER_GPIO, LOW);
    tone_on = false;
    phase_started_ms = now_ms;
  }
}
