#include "ButtonsManager.h"

void initButtons() {
  pinMode(BTN_IZQ_PIN, INPUT_PULLUP);
  pinMode(BTN_DER_PIN, INPUT_PULLUP);
  pinMode(BTN_OK_PIN, INPUT_PULLUP);
}