#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include "Configuration.h"

class NetworkManager {
 public:
  void begin(const Configuration& config);
  bool tick(Configuration& config); // true when tested credentials should be persisted
  bool connect(const String& ssid, const String& password);
  bool startScan();
  void writeScan(JsonObject json);
  void writeState(JsonObject json) const;
  bool apActive() const { return ap_; }
  bool updateAllowed() const { return !connecting_ && !pending_ && !testing_ && !scanning_; }
 private:
  DNSServer dns_;
  String savedSsid_, savedPassword_, candidateSsid_, candidatePassword_, apName_, hostname_;
  String error_;
  bool ap_ = false, connected_ = false, connecting_ = false, pending_ = false, testing_ = false;
  bool mdns_ = false, scanning_ = false;
  uint32_t attemptAt_ = 0, connectedAt_ = 0, disconnectedAt_ = 0, pendingAt_ = 0;
  void startAp();
  void startConnection(const String& ssid, const String& password);
};
