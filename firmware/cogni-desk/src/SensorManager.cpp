#include "SensorManager.h"

#include <Arduino.h>
#include <DHT.h>
#include <math.h>

#include "DeskConfig.h"

namespace {
DHT dht(DESK_DHT11_DATA_GPIO, DHT11);
environment_reading_t reading = {};
uint32_t last_sample_ms;
bool sampled_once;
bool failure_reported;
}

void initSensors(void) {
  dht.begin();
  reading = {};
  sampled_once = false;
  failure_reported = false;
}

bool actualizarSensores(uint32_t now_ms) {
  if (sampled_once &&
      static_cast<uint32_t>(now_ms - last_sample_ms) < DESK_DHT11_SAMPLE_INTERVAL_MS) {
    return false;
  }

  sampled_once = true;
  last_sample_ms = now_ms;

  const float temperature = dht.readTemperature();
  const float humidity = dht.readHumidity();
  reading.sampled_at_ms = now_ms;

  if (isnan(temperature) || isnan(humidity) || humidity < 0.0f || humidity > 100.0f) {
    reading.valid = false;
    if (!failure_reported) {
      Serial.println(F("[dht11] lectura no valida; revisar sensor y cableado"));
      failure_reported = true;
    }
    return true;
  }

  reading.valid = true;
  reading.temperature_c = temperature;
  reading.humidity_pct = humidity;
  failure_reported = false;
  return true;
}

environment_reading_t leerAmbiente(void) {
  return reading;
}
