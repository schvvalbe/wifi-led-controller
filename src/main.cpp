#include <Arduino.h>
#include <ESP8266WebServer.h>
#include "Board.h"
#include "Configuration.h"
#include "LightController.h"
#include "NetworkManager.h"
#include "OtaManager.h"
#include "FirmwareInfo.h"
#include "WebAssets.h"

namespace {
ESP8266WebServer server(80);
Configuration config;
ConfigStore store;
LightController light;
NetworkManager network;
OtaManager ota;
bool dirty = false, saveFailed = false, motion1 = false, motion2 = false, sensorsReady = false;
uint32_t dirtyAt = 0, sampledAt = 0, bootAt = 0;
bool systemRestartPending = false;
uint32_t systemRestartAt = 0;
uint16_t ambient = 0;
uint32_t ambientFiltered = 0;
constexpr uint8_t OutputPins[] = {Board::Red, Board::Green, Board::Blue};

void markDirty() { dirty = true; dirtyAt = millis(); }

void sendJson(int status, const JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json; charset=utf-8", body);
}

void sendError(int status, const String& message) {
  StaticJsonDocument<384> doc;
  doc["error"] = message;
  sendJson(status, doc);
}

bool readRequest(JsonDocument& doc) {
  if (systemRestartPending) { sendError(409, F("Контроллер перезапускается.")); return false; }
  if (ota.busy()) { sendError(409, F("Дождитесь окончания обновления прошивки.")); return false; }
  // JSON-only mutation endpoints prevent cross-origin form submissions.
  const String origin = server.header("Origin");
  if (!origin.isEmpty() && origin != "http://" + server.hostHeader() && origin != "https://" + server.hostHeader()) {
    sendError(403, F("Недопустимый источник запроса")); return false;
  }
  if (!server.header("Content-Type").startsWith("application/json")) {
    sendError(415, F("Требуется application/json")); return false;
  }
  if (!server.hasArg("plain") || server.arg("plain").length() > 1024) {
    sendError(413, F("Размер запроса превышен или тело отсутствует")); return false;
  }
  if (deserializeJson(doc, server.arg("plain")) || !doc.is<JsonObject>()) {
    sendError(400, F("Некорректный JSON")); return false;
  }
  return true;
}

void sendState() {
  DynamicJsonDocument doc(2304);
  writeLight(doc.createNestedObject("settings"), light.settings);
  doc["on"] = light.on();
  doc["remainingSeconds"] = light.remainingSeconds(millis());
  JsonObject sensors = doc.createNestedObject("sensors");
  sensors["motion1"] = motion1; sensors["motion2"] = motion2;
  sensors["ambient"] = ambient; sensors["dark"] = light.dark();
  const uint32_t elapsed = millis() - bootAt;
  sensors["warmupSeconds"] = sensorsReady || elapsed >= Board::SensorWarmupMs ? 0 : (Board::SensorWarmupMs - elapsed + 999) / 1000;
  network.writeState(doc.createNestedObject("network"));
  doc["storage"] = !store.ready() || saveFailed ? "error" : (dirty ? "pending" : "saved");
  doc["version"] = Firmware::Version;
  ota.writeState(doc.createNestedObject("ota"));
  sendJson(200, doc);
}

void sendAsset(const char* type, const uint8_t* data, size_t size) {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'");
  server.send_P(200, type, reinterpret_cast<const char*>(data), size);
}

void systemAction(bool factoryReset) {
  StaticJsonDocument<128> request;
  if (!readRequest(request)) return;
  if (request.size() != 1 || !request["confirm"].is<bool>() || !request["confirm"].as<bool>()) {
    sendError(400, F("Подтвердите действие: confirm: true")); return;
  }
  if (!network.updateAllowed()) {
    sendError(409, F("Дождитесь окончания подключения или поиска Wi-Fi.")); return;
  }
  if (factoryReset) {
    // Stop deferred saves before erasing so old credentials cannot reappear.
    dirty = false;
    config = Configuration{};
    light.configure(config.light, millis());
    const bool filesCleared = store.reset();
    const bool wifiCleared = ESP.eraseConfig();
    if (!filesCleared || !wifiCleared) {
      saveFailed = true;
      sendError(500, F("Не удалось полностью очистить память. Повторите сброс.")); return;
    }
    saveFailed = false;
  } else if (dirty || saveFailed) {
    config.light = light.settings;
    if (!store.save(config)) {
      saveFailed = true;
      sendError(500, F("Не удалось сохранить настройки. Перезагрузка отменена.")); return;
    }
    dirty = saveFailed = false;
  }
  systemRestartPending = true;
  systemRestartAt = millis();
  for (uint8_t pin : OutputPins) analogWrite(pin, Board::PwmActiveHigh ? 0 : Board::PwmRange);
  StaticJsonDocument<128> response;
  response["rebooting"] = true;
  response["reset"] = factoryReset;
  sendJson(202, response);
}

void setupRoutes() {
  server.collectHeaders("Origin", "Content-Type", "X-OTA-Password", "X-Firmware-Size", "X-Firmware-MD5");
  server.on("/", HTTP_GET, [] { sendAsset("text/html; charset=utf-8", INDEX_HTML, sizeof(INDEX_HTML)); });
  server.on("/app.css", HTTP_GET, [] { sendAsset("text/css; charset=utf-8", APP_CSS, sizeof(APP_CSS)); });
  server.on("/app.js", HTTP_GET, [] { sendAsset("application/javascript; charset=utf-8", APP_JS, sizeof(APP_JS)); });
  server.on("/theme.js", HTTP_GET, [] { sendAsset("application/javascript; charset=utf-8", THEME_JS, sizeof(THEME_JS)); });
  server.on("/md5.js", HTTP_GET, [] { sendAsset("application/javascript; charset=utf-8", MD5_JS, sizeof(MD5_JS)); });
  server.on("/manifest.webmanifest", HTTP_GET, [] { sendAsset("application/manifest+json; charset=utf-8", MANIFEST, sizeof(MANIFEST)); });
  server.on("/sw.js", HTTP_GET, [] { sendAsset("application/javascript; charset=utf-8", SERVICE_WORKER, sizeof(SERVICE_WORKER)); });
  server.on("/icon.svg", HTTP_GET, [] { sendAsset("image/svg+xml", ICON_SVG, sizeof(ICON_SVG)); });
  server.on("/icon-192.png", HTTP_GET, [] { sendAsset("image/png", ICON_192, sizeof(ICON_192)); });
  server.on("/icon-512.png", HTTP_GET, [] { sendAsset("image/png", ICON_512, sizeof(ICON_512)); });
  server.on("/icon-maskable.png", HTTP_GET, [] { sendAsset("image/png", ICON_MASKABLE, sizeof(ICON_MASKABLE)); });
  server.on("/apple-touch-icon.png", HTTP_GET, [] { sendAsset("image/png", APPLE_ICON, sizeof(APPLE_ICON)); });
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/system/reboot", HTTP_POST, [] { systemAction(false); });
  server.on("/api/system/reset", HTTP_POST, [] { systemAction(true); });
  server.on("/api/settings", HTTP_POST, [] {
    StaticJsonDocument<1024> doc;
    if (!readRequest(doc)) return;
    LightSettings next = light.settings;
    String error;
    if (!patchLight(doc.as<JsonObjectConst>(), next, error)) { sendError(400, error); return; }
    light.configure(next, millis());
    config.light = light.settings; markDirty(); sendState();
  });
  server.on("/api/power", HTTP_POST, [] {
    StaticJsonDocument<128> doc;
    if (!readRequest(doc)) return;
    if (doc.size() != 1 || !doc["on"].is<bool>()) { sendError(400, F("Требуется поле on: true/false")); return; }
    light.power(doc["on"], millis());
    config.light = light.settings; markDirty(); sendState();
  });
  server.on("/api/wifi", HTTP_POST, [] {
    StaticJsonDocument<512> doc;
    if (!readRequest(doc)) return;
    if (doc.size() != 2 || !doc["ssid"].is<String>() || !doc["password"].is<String>()) {
      sendError(400, F("Требуются имя сети и пароль")); return;
    }
    const String ssid = doc["ssid"].as<String>(), password = doc["password"].as<String>();
    bool valid = ssid.length() >= 1 && ssid.length() <= 32 && strlen(ssid.c_str()) == ssid.length();
    valid = valid && strlen(password.c_str()) == password.length() && (password.isEmpty() || (password.length() >= 8 && password.length() <= 63));
    if (password.length() == 64) {
      valid = ssid.length() >= 1 && ssid.length() <= 32 && strlen(ssid.c_str()) == ssid.length();
      for (char c : password) if (!isxdigit(static_cast<unsigned char>(c))) valid = false;
    }
    if (!valid) { sendError(400, F("SSID: 1–32 байта. Пароль: 8–63 байта, 64 HEX-символа или пустой для открытой сети.")); return; }
    if (!network.connect(ssid, password)) { sendError(409, F("Дождитесь завершения подключения или поиска сетей")); return; }
    sendState();
  });
  server.on("/api/wifi/scan", HTTP_POST, [] {
    StaticJsonDocument<64> doc;
    if (!readRequest(doc)) return;
    if (!network.startScan()) { sendError(409, F("Поиск доступен после завершения подключения")); return; }
    doc.clear(); doc["scanning"] = true; sendJson(202, doc);
  });
  server.on("/api/wifi/scan", HTTP_GET, [] {
    DynamicJsonDocument doc(4096);
    network.writeScan(doc.to<JsonObject>()); sendJson(200, doc);
  });
  server.onNotFound([] {
    if (network.apActive() && server.method() == HTTP_GET && !server.uri().startsWith("/api/")) {
      server.sendHeader("Location", "http://192.168.4.1/"); server.send(302, "text/plain", "Open setup");
    } else sendError(404, F("Не найдено"));
  });
  ota.begin(server, [] {
    if (systemRestartPending || !network.updateAllowed()) return false;
    if (dirty || saveFailed) {
      config.light = light.settings;
      if (!store.save(config)) { saveFailed = true; return false; }
      dirty = saveFailed = false;
    }
    // Freeze the light off while flash is being written. Settings stay intact.
    for (uint8_t pin : OutputPins) analogWrite(pin, Board::PwmActiveHigh ? 0 : Board::PwmRange);
    return true;
  });
  server.begin();
}
}

void setup() {
  // Set safe electrical output before networking and filesystem operations.
  for (uint8_t pin : OutputPins) {
    digitalWrite(pin, Board::PwmActiveHigh ? LOW : HIGH); pinMode(pin, OUTPUT);
  }
  analogWriteRange(Board::PwmRange); analogWriteFreq(1000);
  pinMode(Board::Motion1, INPUT); pinMode(Board::Motion2, INPUT);
  Serial.begin(115200); Serial.printf("\nWiFiLedController %s\n", Firmware::Version);
  bootAt = millis();
  store.begin(config);
  light.configure(config.light, millis()); // Power always starts off, including manual mode.
  ambient = analogRead(Board::Light);
  ambientFiltered = static_cast<uint32_t>(ambient) * 8;
  network.begin(config);
  setupRoutes();
}

void loop() {
  if (systemRestartPending) {
    if (static_cast<uint32_t>(millis() - systemRestartAt) >= 1000) ESP.restart();
    delay(1);
    return;
  }
  ota.tick();
  if (ota.busy()) { delay(1); return; }
  const uint32_t now = millis();
  if (now - sampledAt >= 25) {
    sampledAt = now;
    motion1 = (digitalRead(Board::Motion1) == (Board::MotionActiveHigh ? HIGH : LOW));
    motion2 = (digitalRead(Board::Motion2) == (Board::MotionActiveHigh ? HIGH : LOW));
    ambientFiltered = ambientFiltered - ambientFiltered / 8 + analogRead(Board::Light);
    ambient = ambientFiltered / 8;
  }
  if (now - bootAt >= Board::SensorWarmupMs) sensorsReady = true;
  light.tick(now, motion1 || motion2, ambient, sensorsReady);
  for (unsigned i = 0; i < 3; ++i) {
    const uint16_t duty = light.channel(i);
    analogWrite(OutputPins[i], Board::PwmActiveHigh ? duty : Board::PwmRange - duty);
  }
  if (network.tick(config)) markDirty();
  server.handleClient();
  if (systemRestartPending) { delay(1); return; }
  if (dirty && static_cast<uint32_t>(millis() - dirtyAt) >= (saveFailed ? 5000U : 1500U)) {
    config.light = light.settings;
    if (store.save(config)) { dirty = false; saveFailed = false; }
    else { saveFailed = true; dirtyAt = millis(); }
  }
  delay(1); // Yield to the ESP8266 networking stack and watchdog.
}
