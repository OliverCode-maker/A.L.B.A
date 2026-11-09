#include <Arduino.h>

#include "DeskApp.h"
#include "DisplaysManager.h"
#include "ButtonsManager.h"
#include "CommManager.h"
#include "SensorManager.h"
#include "WifiManager.h"
#include "ClockManager.h"

static desk_app_t app;
static desk_view_t last_view;
static bool has_rendered_view = false;
static environment_reading_t ambiente;

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
        (view.screen == DESK_SCREEN_ENVIRONMENT && new_environment_sample)) {
        ambiente = leerAmbiente();
        mostrarVista(&view, &ambiente);
        last_view = view;
        has_rendered_view = true;
    }
}