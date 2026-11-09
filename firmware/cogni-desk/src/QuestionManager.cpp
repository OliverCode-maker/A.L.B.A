#include "QuestionManager.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "CloudManager.h"
#include "question_config.h"

namespace {
constexpr char NAMESPACE[] = "alba-q";
constexpr char KEY[] = "qtext";

qc_set_t set;                      // las preguntas apuntan aqui: debe seguir vivo mientras se juega
char incoming[QC_TEXT_MAX + 1u];
char stored[QC_TEXT_MAX + 1u];
uint8_t active_count = 0u;         // 0 = banco incluido en el robot

void saveText(const char *text) {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) {
    Serial.println(F("[preguntas] no se pudo guardar en memoria (siguen activas hasta reiniciar)"));
    return;
  }
  if (text[0] == '\0') {
    prefs.remove(KEY);
  } else if (strcmp(stored, text) != 0) {
    prefs.putString(KEY, text);
  }
  prefs.end();
  strncpy(stored, text, sizeof(stored) - 1u);
  stored[sizeof(stored) - 1u] = '\0';
}

// Devuelve cuantas se descartaron. Con texto vacio vuelve al banco incluido.
uint8_t apply(const char *text, bool from_cloud) {
  if (text[0] == '\0') {
    game_content_set_custom(nullptr, 0u);
    active_count = 0u;
    if (from_cloud) saveText(text);
    Serial.println(F("[preguntas] banco incluido en el robot (sin preguntas del cuidador)"));
    return 0u;
  }

  const uint8_t valid = qc_parse(text, &set);
  if (valid < QC_MIN) {
    Serial.printf("[preguntas] solo %u validas (minimo %u): se mantiene el banco anterior\n",
                  static_cast<unsigned>(valid), static_cast<unsigned>(QC_MIN));
    const unsigned not_loaded = static_cast<unsigned>(valid) + set.rejected;
    // Se vuelve a armar el banco activo si habia uno guardado (qc_parse borro `set`).
    if (active_count > 0u) {
      qc_parse(stored, &set);
      game_content_set_custom(set.q, set.count);
    }
    return static_cast<uint8_t>(not_loaded > 255u ? 255u : not_loaded);
  }

  game_content_set_custom(set.q, set.count);
  active_count = set.count;
  if (from_cloud) saveText(text);
  Serial.printf("[preguntas] %u preguntas del cuidador activas (%u descartadas)\n",
                static_cast<unsigned>(set.count), static_cast<unsigned>(set.rejected));
  return set.rejected;
}
}  // namespace

void initQuestions(void) {
  stored[0] = '\0';
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, true)) return;
  prefs.getString(KEY, stored, sizeof(stored));
  prefs.end();
  if (stored[0] != '\0') apply(stored, false);
}

void actualizarQuestions(bool game_active) {
  if (game_active) return;
  if (!cloudTakeQuestions(incoming, sizeof(incoming))) return;
  const uint8_t rejected = apply(incoming, true);
  cloudOnQuestions(active_count, rejected);
}
