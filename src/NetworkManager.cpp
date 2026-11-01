#include "NetworkManager.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

namespace {
constexpr uint32_t ConnectTimeoutMs = 25000;
constexpr uint32_t RetryMs = 30000;
constexpr uint32_t ApGraceMs = 60000;
constexpr char ApPassword[] = "wifi-led-controller";
}

void NetworkManager::begin(const Configuration& config) {
  savedSsid_ = config.ssid; savedPassword_ = config.password;
  String suffix = String(ESP.getChipId(), HEX); suffix.toUpperCase();
  apName_ = "PhysTechLab-" + suffix;
  hostname_ = "wifi-led-controller-" + suffix; hostname_.toLowerCase();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname_);
  disconnectedAt_ = millis();
  if (savedSsid_.isEmpty()) startAp();
  else startConnection(savedSsid_, savedPassword_);
}

void NetworkManager::startAp() {
  if (ap_) return;
  WiFi.mode(WIFI_AP_STA);
  const IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  ap_ = WiFi.softAP(apName_.c_str(), ApPassword);
  if (ap_) {
    dns_.start(53, "*", ip);
    Serial.printf("Setup Wi-Fi: %s / password: %s / http://192.168.4.1\n", apName_.c_str(), ApPassword);
  } else error_ = F("Не удалось открыть точку доступа");
}

void NetworkManager::startConnection(const String& ssid, const String& password) {
  if (mdns_) { MDNS.close(); mdns_ = false; }
  WiFi.disconnect(false);
  connected_ = false;
  WiFi.begin(ssid.c_str(), password.c_str());
  connecting_ = true;
  attemptAt_ = millis();
}

bool NetworkManager::connect(const String& ssid, const String& password) {
  if (pending_ || testing_ || scanning_) return false;
  candidateSsid_ = ssid; candidatePassword_ = password;
  error_ = "";
  pending_ = true;
  pendingAt_ = millis();
  return true;
}

bool NetworkManager::tick(Configuration& config) {
  const uint32_t now = millis();
  bool changed = false;
  if (ap_) dns_.processNextRequest();
  if (mdns_) MDNS.update();
  if (scanning_ && WiFi.scanComplete() != WIFI_SCAN_RUNNING) scanning_ = false;
  if (pending_ && now - pendingAt_ >= 700) {
    pending_ = false; testing_ = true;
    startAp(); // Keep setup reachable while trying new credentials.
    startConnection(candidateSsid_, candidatePassword_);
    // WiFi.begin() may advance millis(); never compare its new timestamp to the old 'now'.
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!connected_) {
      connected_ = true; connecting_ = false; connectedAt_ = now;
      if (testing_) {
        config.ssid = savedSsid_ = candidateSsid_;
        config.password = savedPassword_ = candidatePassword_;
        candidatePassword_ = ""; testing_ = false; changed = true;
      }
      mdns_ = MDNS.begin(hostname_);
      if (mdns_) MDNS.addService("http", "tcp", 80);
      Serial.printf("Connected: http://%s / http://%s.local\n", WiFi.localIP().toString().c_str(), hostname_.c_str());
    }
    if (ap_ && !pending_ && now - connectedAt_ >= ApGraceMs) {
      dns_.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); ap_ = false;
    }
  } else {
    if (connected_) {
      connected_ = false; disconnectedAt_ = now; attemptAt_ = now - RetryMs;
      if (mdns_) { MDNS.close(); mdns_ = false; }
    }
    if (connecting_ && now - attemptAt_ >= ConnectTimeoutMs) {
      WiFi.disconnect(false);
      connecting_ = false;
      if (testing_) {
        error_ = F("Подключиться не удалось. Проверьте имя сети, пароль и сигнал. Прежняя сеть сохранена.");
        testing_ = false; candidatePassword_ = "";
      }
      startAp();
    }
    if (!ap_ && now - disconnectedAt_ >= ConnectTimeoutMs) startAp();
    if (!connecting_ && !pending_ && !scanning_ && !savedSsid_.isEmpty() && now - attemptAt_ >= RetryMs) {
      startConnection(savedSsid_, savedPassword_);
    }
  }
  return changed;
}

bool NetworkManager::startScan() {
  if (connecting_ || pending_ || testing_) return false;
  if (scanning_) return true;
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);
  scanning_ = true;
  return true;
}

void NetworkManager::writeScan(JsonObject j) {
  const int count = WiFi.scanComplete();
  j["scanning"] = count == WIFI_SCAN_RUNNING;
  JsonArray list = j.createNestedArray("networks");
  if (count < 0) return;
  // Bound the response and omit hidden SSIDs (manual entry is always available).
  for (int i = 0; i < count && list.size() < 20; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool duplicate = false;
    for (JsonObject item : list) if (item["ssid"] == ssid) duplicate = true;
    if (duplicate) continue;
    JsonObject item = list.createNestedObject();
    item["ssid"] = ssid; item["rssi"] = WiFi.RSSI(i);
    item["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
}

void NetworkManager::writeState(JsonObject j) const {
  j["connected"] = connected_;
  j["connecting"] = connecting_ || pending_;
  j["testing"] = testing_ || pending_;
  j["ssid"] = connected_ ? WiFi.SSID() : savedSsid_;
  j["ip"] = connected_ ? WiFi.localIP().toString() : String("");
  j["hostname"] = hostname_ + ".local";
  j["rssi"] = connected_ ? WiFi.RSSI() : 0;
  j["apActive"] = ap_; j["apSsid"] = apName_;
  j["apIp"] = "192.168.4.1";
  j["error"] = error_;
}
