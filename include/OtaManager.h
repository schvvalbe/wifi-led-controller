#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <Updater.h>
#include <functional>
#include "FirmwareUpload.h"

class OtaManager {
 public:
  OtaManager() : upload_(Update) {}
  void begin(ESP8266WebServer& server, std::function<bool()> prepare);
  void tick();
  void writeState(JsonObject json) const;
  bool busy() const { return receiving_ || rebootPending_; }
  static size_t maxSize();
 private:
  ESP8266WebServer* server_ = nullptr;
  std::function<bool()> prepare_;
  Firmware::Upload<UpdaterClass> upload_;
  bool requestOpen_ = false, receiving_ = false, fileEnded_ = false, rebootPending_ = false;
  uint32_t rebootAt_ = 0;
  size_t expected_ = 0;
  int errorStatus_ = 0;
  String error_;
  void receive();
  void finish();
  void fail(int status, const String& message);
  void resetRequest();
};
