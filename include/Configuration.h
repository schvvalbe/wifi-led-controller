#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "LightController.h"

struct Configuration {
  LightSettings light;
  String ssid;
  String password;
};

void writeLight(JsonObject json, const LightSettings& settings);
bool patchLight(JsonObjectConst json, LightSettings& settings, String& error);

class ConfigStore {
 public:
  bool begin(Configuration& config);
  bool save(const Configuration& config);
  bool reset();
  bool ready() const { return ready_; }
 private:
  bool ready_ = false;
  bool read(const char* path, Configuration& config);
};
