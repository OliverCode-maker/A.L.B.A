#include "ClockManager.h"

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

#include "WifiManager.h"

namespace {
constexpr char TIME_ZONE[] = "UTC5";
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.nist.gov";
constexpr time_t MIN_VALID_EPOCH = 1735689600;
constexpr uint8_t DEFAULT_ALARM_HOUR = 8u;
constexpr uint16_t INVALID_DAY = 0xFFFFu;
constexpr uint32_t SAVE_RETRY_INTERVAL_MS = 5000u;

Preferences preferences;
bool preferences_ready = false;
bool ntp_configured = false;
bool wifi_was_connected = false;
uint32_t saved_alarm_revision = 0u;
uint32_t last_save_attempt_ms = 0u;
uint32_t last_attempted_revision = 0u;
bool has_save_attempt = false;
uint16_t last_fired_day[DESK_ALARM_COUNT] = {
    INVALID_DAY, INVALID_DAY, INVALID_DAY
};
uint8_t pending_alarm_mask = 0u;

uint16_t packAlarm(const desk_alarm_t &alarm) {
  return static_cast<uint16_t>((alarm.hour << 7u) |
                               (alarm.minute << 1u) |
                               (alarm.enabled ? 1u : 0u));
}

desk_alarm_t unpackAlarm(uint16_t packed) {
  desk_alarm_t alarm = {};
  alarm.hour = static_cast<uint8_t>((packed >> 7u) & 0x1Fu);
  alarm.minute = static_cast<uint8_t>((packed >> 1u) & 0x3Fu);
  alarm.enabled = (packed & 1u) != 0u;
  if (alarm.hour >= 24u || alarm.minute >= 60u) {
    alarm.hour = DEFAULT_ALARM_HOUR;
    alarm.minute = 0u;
    alarm.enabled = false;
  }
  return alarm;
}

void startNtpIfReady() {
  const bool connected = wifiManagerConnected();
  if (connected && (!wifi_was_connected || !ntp_configured)) {
    configTzTime(TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2);
    ntp_configured = true;
    Serial.println(F("[clock] sincronizacion NTP solicitada (UTC-5)"));
  }
  wifi_was_connected = connected;
}

bool readValidLocalTime(struct tm *local_time) {
  const time_t epoch = time(nullptr);
  if (epoch < MIN_VALID_EPOCH || local_time == nullptr) return false;
  return localtime_r(&epoch, local_time) != nullptr;
}

void persistAlarmsIfChanged(const desk_app_t *app, uint32_t now_ms) {
  if (!preferences_ready) return;
  const uint32_t revision = desk_app_alarm_revision(app);
  if (revision == saved_alarm_revision) return;
  if (has_save_attempt && last_attempted_revision == revision &&
      now_ms - last_save_attempt_ms < SAVE_RETRY_INTERVAL_MS) {
    return;
  }
  has_save_attempt = true;
  last_attempted_revision = revision;
  last_save_attempt_ms = now_ms;

  bool all_saved = true;
  const desk_view_t view = desk_app_view(app);
  for (uint8_t i = 0u; i < DESK_ALARM_COUNT; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "a%u", static_cast<unsigned>(i));
    if (preferences.putUShort(key, packAlarm(view.alarms[i])) != sizeof(uint16_t)) {
      all_saved = false;
      Serial.printf("[clock] no se pudo guardar la alarma %u\n",
                    static_cast<unsigned>(i + 1u));
    }
  }
  if (all_saved) saved_alarm_revision = revision;
}

void updateAlarms(desk_app_t *app, const struct tm &local_time) {
  const uint16_t day_key =
      static_cast<uint16_t>((local_time.tm_year % 100) * 366 + local_time.tm_yday);
  const desk_view_t view = desk_app_view(app);
  for (uint8_t i = 0u; i < DESK_ALARM_COUNT; ++i) {
    const desk_alarm_t &alarm = view.alarms[i];
    if (alarm.enabled && alarm.hour == local_time.tm_hour &&
        alarm.minute == local_time.tm_min && last_fired_day[i] != day_key) {
      pending_alarm_mask |= static_cast<uint8_t>(1u << i);
      last_fired_day[i] = day_key;
    }
  }

  for (uint8_t i = 0u; i < DESK_ALARM_COUNT; ++i) {
    const uint8_t bit = static_cast<uint8_t>(1u << i);
    if ((pending_alarm_mask & bit) != 0u &&
        desk_app_show_medication_alarm(app, i)) {
      pending_alarm_mask &= static_cast<uint8_t>(~bit);
      break;
    }
  }
}
}

bool initClockManager(desk_app_t *app) {
  if (app == nullptr) return false;

  preferences_ready = preferences.begin("desk-clock", false);
  if (!preferences_ready) {
    Serial.println(F("[clock] no se pudo abrir almacenamiento NVS de alarmas"));
    return false;
  }

  for (uint8_t i = 0u; i < DESK_ALARM_COUNT; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "a%u", static_cast<unsigned>(i));
    const uint16_t default_alarm =
        static_cast<uint16_t>(DEFAULT_ALARM_HOUR << 7u);
    const desk_alarm_t alarm = unpackAlarm(preferences.getUShort(key, default_alarm));
    desk_app_set_alarm(app, i, &alarm);
  }
  return true;
}

void actualizarClockManager(desk_app_t *app, uint32_t now_ms) {
  if (app == nullptr) return;

  startNtpIfReady();
  persistAlarmsIfChanged(app, now_ms);

  struct tm local_time;
  if (!readValidLocalTime(&local_time)) {
    desk_app_set_clock(app, false, 0u, 0u);
    return;
  }

  desk_app_set_clock(app, true, static_cast<uint8_t>(local_time.tm_hour),
                     static_cast<uint8_t>(local_time.tm_min));
  updateAlarms(app, local_time);
}
