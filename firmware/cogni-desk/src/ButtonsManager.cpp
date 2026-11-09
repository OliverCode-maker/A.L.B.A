#include "ButtonsManager.h"

#include <Arduino.h>
#include <stddef.h>

#include "DeskConfig.h"

namespace {
struct ButtonState {
  uint8_t pin;
  int raw;
  int stable;
  uint32_t changed_ms;
};

ButtonState buttons[] = {
  { DESK_BUTTON_LEFT_GPIO, HIGH, HIGH, 0 },
  { DESK_BUTTON_RIGHT_GPIO, HIGH, HIGH, 0 },
  { DESK_BUTTON_OK_GPIO, HIGH, HIGH, 0 }
};

const button_event_t events[] = {
  BUTTON_EVENT_LEFT,
  BUTTON_EVENT_RIGHT,
  BUTTON_EVENT_OK
};
}

void initButtons(void) {
  for (ButtonState &button : buttons) {
    pinMode(button.pin, INPUT_PULLUP);
    button.raw = digitalRead(button.pin);
    button.stable = button.raw;
    button.changed_ms = millis();
  }
}

button_event_t actualizarBotones(uint32_t now_ms) {
  button_event_t event = BUTTON_EVENT_NONE;

  for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
    ButtonState &button = buttons[i];
    const int sample = digitalRead(button.pin);

    if (sample != button.raw) {
      button.raw = sample;
      button.changed_ms = now_ms;
    }

    if (button.stable != button.raw &&
        static_cast<uint32_t>(now_ms - button.changed_ms) >= DESK_BUTTON_DEBOUNCE_MS) {
      button.stable = button.raw;
      if (button.stable == LOW && event == BUTTON_EVENT_NONE) {
        event = events[i];
      }
    }
  }

  return event;
}