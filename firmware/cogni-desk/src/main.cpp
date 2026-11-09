#include <Arduino.h>

#include "DeskApp.h"
#include "DisplaysManager.h"
#include "ButtonsManager.h"
#include "CommManager.h"
#include "SensorManager.h"
#include "WifiManager.h"
#include "ClockManager.h"
#include "BuzzerManager.h"

static desk_app_t app;
static desk_view_t last_view;
static bool has_rendered_view = false;
static environment_reading_t ambiente;
static health_reading_t last_salud;

static bool mismaSalud(const health_reading_t &a, const health_reading_t &b) {
    return a.online == b.online && a.bpm_valid == b.bpm_valid && a.bpm == b.bpm &&
           a.spo2_valid == b.spo2_valid && a.spo2 == b.spo2 &&
           a.bat_valid == b.bat_valid && a.bat_pct == b.bat_pct;
}

static buzzer_pattern_t patronBuzzer(desk_screen_t screen) {
    switch (screen) {
    case DESK_SCREEN_FALL_PROMPT:     return BUZZER_PATTERN_FALL;
    case DESK_SCREEN_FALL_CONFIRMED:  return BUZZER_PATTERN_FALL_CONFIRMED;
    case DESK_SCREEN_MEDICATION_ALERT: return BUZZER_PATTERN_MEDICATION;
    default:                          return BUZZER_PATTERN_OFF;
    }
}

static desk_input_t traducirEntrada(button_event_t event) {
    switch (event) {
    case BUTTON_EVENT_LEFT:  return DESK_INPUT_LEFT;
    case BUTTON_EVENT_RIGHT: return DESK_INPUT_RIGHT;
    case BUTTON_EVENT_OK:    return DESK_INPUT_OK;
    default:                 return DESK_INPUT_NONE;
    }
}

void setup() {
    Serial.begin(115200);

    if (!initDisplays()) {
        Serial.println(F("No se pudo iniciar el sistema de pantallas."));
        while (true) delay(1000);
    }

    initButtons();
    initBuzzer();
    initSensors();
    desk_app_init(&app, millis());
    if (!initCommunication()) {
        Serial.println(F("ESP-NOW no disponible; interfaz y DHT11 siguen activos."));
    }
    if (!initWifiManager()) {
        Serial.println(F("Wi-Fi no pudo iniciar completamente; interfaz local sigue activa."));
    }
    if (!initClockManager(&app)) {
        Serial.println(F("Alarmas persistentes no disponibles; reloj sigue activo."));
    }
}

void loop() {
    const uint32_t now_ms = millis();
    actualizarWifiManager(now_ms);
    const bool new_environment_sample = actualizarSensores(now_ms);
    procesarComunicacionesBackground(&app, now_ms);

    const button_event_t event = actualizarBotones(now_ms);
    uint16_t ref_seq = 0u;
    robot_action_t action = desk_app_on_input(&app, traducirEntrada(event), now_ms, &ref_seq);
    ejecutarAccionRobot(action, ref_seq);
    action = desk_app_tick(&app, now_ms, &ref_seq);
    ejecutarAccionRobot(action, ref_seq);
    actualizarClockManager(&app, now_ms);

    const desk_view_t view = desk_app_view(&app);
    actualizarBuzzer(patronBuzzer(view.screen), now_ms);
    const health_reading_t salud = leerSalud(now_ms);
    if (!has_rendered_view || view.screen != last_view.screen ||
        view.menu_selection != last_view.menu_selection ||
        view.eyes_closed != last_view.eyes_closed ||
        view.clock_valid != last_view.clock_valid ||
        view.clock_hour != last_view.clock_hour ||
        view.clock_minute != last_view.clock_minute ||
        view.selected_alarm != last_view.selected_alarm ||
        view.alarm_edit_field != last_view.alarm_edit_field ||
        view.medication_alarm_index != last_view.medication_alarm_index ||
        view.alarms[0].hour != last_view.alarms[0].hour ||
        view.alarms[0].minute != last_view.alarms[0].minute ||
        view.alarms[0].enabled != last_view.alarms[0].enabled ||
        view.alarms[1].hour != last_view.alarms[1].hour ||
        view.alarms[1].minute != last_view.alarms[1].minute ||
        view.alarms[1].enabled != last_view.alarms[1].enabled ||
        view.alarms[2].hour != last_view.alarms[2].hour ||
        view.alarms[2].minute != last_view.alarms[2].minute ||
        view.alarms[2].enabled != last_view.alarms[2].enabled ||
        (view.screen == DESK_SCREEN_ENVIRONMENT && new_environment_sample) ||
        (view.screen == DESK_SCREEN_HEALTH && !mismaSalud(salud, last_salud))) {
        ambiente = leerAmbiente();
        mostrarVista(&view, &ambiente, &salud);
        last_salud = salud;
        last_view = view;
        has_rendered_view = true;
    }
}