#pragma once
#include "Arduino.h"
constexpr int WIFI_STA = 1, WIFI_AP_STA = 3, WL_CONNECTED = 3, WL_DISCONNECTED = 6;
constexpr int WIFI_SCAN_RUNNING = -1, ENC_TYPE_NONE = 0;
struct IPAddress {
  IPAddress(int, int, int, int) {}
  String toString() const { return "192.168.1.42"; }
};
struct FakeWiFi {
  int statusValue = WL_DISCONNECTED, scanValue = -2, attempts = 0;
  bool ap = false;
  String attemptedSsid, attemptedPassword;
  void persistent(bool) {}
  void setAutoReconnect(bool) {}
  void mode(int) {}
  void hostname(const String&) {}
  void softAPConfig(IPAddress, IPAddress, IPAddress) {}
  bool softAP(const char*, const char*) { ap = true; fakeMillis += 5; return true; }
  void softAPdisconnect(bool) { ap = false; }
  void disconnect(bool) { statusValue = WL_DISCONNECTED; }
  void begin(const char* ssid, const char* password) {
    attemptedSsid = ssid; attemptedPassword = password; attempts++; fakeMillis += 5;
  }
  int status() const { return statusValue; }
  IPAddress localIP() const { return {192, 168, 1, 42}; }
  String SSID(int = 0) const { return attemptedSsid; }
  int RSSI(int = 0) const { return -50; }
  int encryptionType(int) const { return 1; }
  int scanComplete() const { return scanValue; }
  void scanDelete() { scanValue = -2; }
  void scanNetworks(bool, bool) { scanValue = WIFI_SCAN_RUNNING; }
};
inline FakeWiFi WiFi;
