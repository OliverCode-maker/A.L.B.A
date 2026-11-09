#include "WifiManager.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include "DeskConfig.h"

namespace {
constexpr size_t EEPROM_SIZE = 512u;
constexpr size_t CREDENTIAL_FIELD_SIZE = 50u;
constexpr int SSID_SLOT_0 = 0;
constexpr int SSID_SLOT_1 = 50;
constexpr int PASSWORD_SLOT_0 = 100;
constexpr int PASSWORD_SLOT_1 = 150;
constexpr int CREDENTIAL_MARKER = 300;
constexpr uint32_t CONNECTION_TIMEOUT_MS = 15000u;
constexpr uint32_t AP_SUCCESS_GRACE_MS = 5000u;
constexpr char AP_SSID[] = "CogniDesk-Setup";

enum class WifiState : uint8_t {
  kProvisioning,
  kConnectingSaved,
  kConnectingNew,
  kConnected
};

struct Credentials {
  String ssid;
  String password;
};

WebServer server(80);
WifiState state = WifiState::kProvisioning;
Credentials saved_credentials[2];
Credentials pending_credentials;
bool saved_slot_valid[2] = {false, false};
bool eeprom_ready = false;
bool server_started = false;
bool ap_started = false;
bool ap_shutdown_pending = false;
int active_slot = -1;
int attempt_slot = -1;
int saved_order[2] = {0, 1};
size_t saved_order_count = 0u;
size_t saved_order_index = 0u;
uint32_t attempt_started_ms = 0u;
uint32_t ap_shutdown_at_ms = 0u;
char ap_password[16] = {};
String status_message = "Iniciando Wi-Fi...";

int ssidOffset(int slot) {
  return slot == 0 ? SSID_SLOT_0 : SSID_SLOT_1;
}

int passwordOffset(int slot) {
  return slot == 0 ? PASSWORD_SLOT_0 : PASSWORD_SLOT_1;
}

bool readEepromString(int offset, String &value) {
  value = "";
  for (size_t i = 0u; i < CREDENTIAL_FIELD_SIZE; ++i) {
    const uint8_t byte_value = EEPROM.read(offset + static_cast<int>(i));
    if (byte_value == 0u) return true;
    if (byte_value == 0xFFu) return false;
    value += static_cast<char>(byte_value);
  }
  return false;
}

bool readCredentials(int slot, Credentials &credentials) {
  return readEepromString(ssidOffset(slot), credentials.ssid) &&
         !credentials.ssid.isEmpty() &&
         readEepromString(passwordOffset(slot), credentials.password);
}

bool writeEepromString(int offset, const String &value) {
  if (value.length() >= CREDENTIAL_FIELD_SIZE) return false;
  for (size_t i = 0u; i < CREDENTIAL_FIELD_SIZE; ++i) {
    const uint8_t byte_value = i < value.length()
                                   ? static_cast<uint8_t>(value[i])
                                   : 0u;
    EEPROM.write(offset + static_cast<int>(i), byte_value);
  }
  return true;
}

bool saveCredentials(int slot, const Credentials &credentials) {
  if (!eeprom_ready ||
      !writeEepromString(ssidOffset(slot), credentials.ssid) ||
      !writeEepromString(passwordOffset(slot), credentials.password)) {
    return false;
  }

  EEPROM.write(CREDENTIAL_MARKER, slot == 1 ? 'a' : 'b');
  EEPROM.write(CREDENTIAL_MARKER + 1, 0u);
  return EEPROM.commit();
}

int preferredSlot() {
  const uint8_t marker = EEPROM.read(CREDENTIAL_MARKER);
  if (marker == 'a') return 1;
  return 0;
}

void handleRoot() {
  const String html =
      F("<!doctype html><html lang='es'><meta name='viewport' "
        "content='width=device-width,initial-scale=1'><title>Cogni Desk</title>"
        "<h2>Configurar Wi-Fi</h2><form method='POST' action='/wifi'>"
        "<label>Red Wi-Fi <input name='ssid' maxlength='32' required></label><br>"
        "<label>Contrasena <input name='password' type='password' "
        "maxlength='49'></label><br><button>Conectar</button></form>"
        "<p id='status'>Preparando...</p><script>"
        "async function refresh(){try{const r=await fetch('/status');"
        "const s=await r.json();document.getElementById('status').textContent="
        "s.message;}catch(e){}}setInterval(refresh,1500);refresh();"
        "</script></html>");
  server.send(200, "text/html; charset=utf-8", html);
}

void handleStatus() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  const String response = String("{\"connected\":") +
                          (connected ? "true" : "false") +
                          ",\"message\":\"" + status_message + "\"}";
  server.send(200, "application/json; charset=utf-8", response);
}

void handleWifi() {
  if (state == WifiState::kConnectingNew) {
    server.send(409, "text/plain; charset=utf-8",
                "Ya hay un intento de conexion en curso.");
    return;
  }

  const String ssid = server.arg("ssid");
  const String password = server.arg("password");
  if (ssid.isEmpty() || ssid.length() > 32u || password.length() > 49u) {
    server.send(400, "text/plain; charset=utf-8",
                "SSID o contrasena fuera del limite permitido.");
    return;
  }

  pending_credentials.ssid = ssid;
  pending_credentials.password = password;
  WiFi.disconnect(false, false);
  WiFi.begin(pending_credentials.ssid.c_str(),
             pending_credentials.password.c_str(), DESK_WIFI_STA_CHANNEL);
  attempt_started_ms = millis();
  state = WifiState::kConnectingNew;
  status_message = "Intentando conectar a la nueva red...";
  server.send(202, "text/plain; charset=utf-8",
              "Intento iniciado. Esta pagina mostrara el resultado.");
}

void configureServer() {
  if (server_started) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();
  server_started = true;
}

void beginProvisioning() {
  if (!ap_started) {
    const uint32_t random_value = esp_random();
    snprintf(ap_password, sizeof(ap_password), "ALBA-%08lX",
             static_cast<unsigned long>(random_value));
    if (!WiFi.mode(WIFI_AP_STA) ||
        !WiFi.softAP(AP_SSID, ap_password, DESK_WIFI_CHANNEL)) {
      state = WifiState::kProvisioning;
      status_message = "No se pudo iniciar el punto de acceso.";
      Serial.println(F("[wifi] error al iniciar el punto de acceso"));
      return;
    }

    ap_started = true;
    configureServer();
    Serial.printf("[wifi] AP: %s, clave temporal: %s, IP: %s, canal: %u\n",
                  AP_SSID, ap_password, WiFi.softAPIP().toString().c_str(),
                  static_cast<unsigned>(DESK_WIFI_CHANNEL));
  }

  state = WifiState::kProvisioning;
  ap_shutdown_pending = false;
  status_message = "Conectate al AP CogniDesk-Setup y abre 192.168.4.1.";
}

void beginSavedAttempt(int slot, uint32_t now_ms) {
  attempt_slot = slot;
  attempt_started_ms = now_ms;
  state = WifiState::kConnectingSaved;
  status_message = "Intentando conectar con una red guardada...";
  WiFi.disconnect(false, false);
  WiFi.begin(saved_credentials[slot].ssid.c_str(),
             saved_credentials[slot].password.c_str(), DESK_WIFI_STA_CHANNEL);
}

void setSavedOrder(int first_slot) {
  saved_order_count = 0u;
  const int candidates[2] = {first_slot, 1 - first_slot};
  for (int slot : candidates) {
    if (saved_slot_valid[slot]) {
      saved_order[saved_order_count++] = slot;
    }
  }
  saved_order_index = 0u;
}

void tryNextSavedNetwork(uint32_t now_ms) {
  while (saved_order_index < saved_order_count) {
    const int slot = saved_order[saved_order_index++];
    if (saved_slot_valid[slot]) {
      beginSavedAttempt(slot, now_ms);
      return;
    }
  }
  beginProvisioning();
}

void acceptSavedConnection(int slot) {
  active_slot = slot;
  state = WifiState::kConnected;
  status_message = "Conectado a la red guardada.";
  Serial.printf("[wifi] conectado; IP: %s, canal: %u\n",
                WiFi.localIP().toString().c_str(),
                static_cast<unsigned>(WiFi.channel()));
}

void acceptNewConnection(uint32_t now_ms) {
  const int target_slot = active_slot >= 0 ? 1 - active_slot : 1 - preferredSlot();
  if (saveCredentials(target_slot, pending_credentials)) {
    saved_credentials[target_slot] = pending_credentials;
    saved_slot_valid[target_slot] = true;
    active_slot = target_slot;
    status_message = "Conectado. Las nuevas credenciales quedaron guardadas.";
    ap_shutdown_pending = true;
    ap_shutdown_at_ms = now_ms + AP_SUCCESS_GRACE_MS;
    Serial.println(F("[wifi] conexion establecida y credenciales guardadas"));
  } else {
    status_message = "Conectado, pero no se pudieron guardar las credenciales.";
    Serial.println(F("[wifi] conexion establecida; fallo al guardar credenciales"));
  }
  state = WifiState::kConnected;
  pending_credentials = Credentials{};
}

void stopProvisioningAp() {
  server.stop();
  server_started = false;
  WiFi.softAPdisconnect(true);
  ap_started = false;
  ap_shutdown_pending = false;
}
}

bool initWifiManager(void) {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_STA)) {
    Serial.println(F("[wifi] no se pudo activar el modo STA"));
    return false;
  }

  eeprom_ready = EEPROM.begin(EEPROM_SIZE);
  if (!eeprom_ready) {
    Serial.println(F("[wifi] EEPROM no disponible; no se guardaran credenciales"));
    beginProvisioning();
    return false;
  }

  const int preferred = preferredSlot();
  for (int slot = 0; slot < 2; ++slot) {
    saved_slot_valid[slot] = readCredentials(slot, saved_credentials[slot]);
  }

  setSavedOrder(preferred);
  tryNextSavedNetwork(millis());
  return true;
}

void actualizarWifiManager(uint32_t now_ms) {
  if (server_started) server.handleClient();

  if (state == WifiState::kConnectingSaved) {
    if (WiFi.status() == WL_CONNECTED) {
      acceptSavedConnection(attempt_slot);
    } else if (now_ms - attempt_started_ms >= CONNECTION_TIMEOUT_MS) {
      WiFi.disconnect(false, false);
      tryNextSavedNetwork(now_ms);
    }
    return;
  }

  if (state == WifiState::kConnectingNew) {
    if (WiFi.status() == WL_CONNECTED) {
      acceptNewConnection(now_ms);
    } else if (now_ms - attempt_started_ms >= CONNECTION_TIMEOUT_MS) {
      WiFi.disconnect(false, false);
      pending_credentials = Credentials{};
      state = WifiState::kProvisioning;
      status_message = "No se pudo conectar. Verifica los datos e intenta otra vez.";
    }
    return;
  }

  if (state == WifiState::kConnected && WiFi.status() != WL_CONNECTED) {
    status_message = "Se perdio la conexion; reintentando...";
    if (active_slot >= 0) {
      setSavedOrder(active_slot);
      tryNextSavedNetwork(now_ms);
    } else {
      beginProvisioning();
    }
    return;
  }

  if (state == WifiState::kConnected && ap_shutdown_pending &&
      static_cast<int32_t>(now_ms - ap_shutdown_at_ms) >= 0) {
    stopProvisioningAp();
  }
}

bool wifiManagerConnected(void) {
  return WiFi.status() == WL_CONNECTED;
}

bool wifiManagerProvisioning(void) {
  return ap_started;
}
