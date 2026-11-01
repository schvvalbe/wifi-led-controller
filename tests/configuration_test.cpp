#include "Configuration.h"
#include <LittleFS.h>
#include <cassert>
#include <cstdio>

int main() {
  ConfigStore first;
  Configuration config;
  assert(!first.begin(config) && first.ready());
  config.ssid = "Example network"; config.password = "test-only-password";
  config.light.automatic = false; config.light.useLightSensor = false;
  config.light.brightness = 0; config.light.red = 7; config.light.green = 192; config.light.blue = 255;
  config.light.holdSeconds = 3600; config.light.darkThreshold = 1023;
  config.light.fadeInMs = 100; config.light.fadeOutMs = 9900;
  assert(first.save(config));
  ConfigStore reboot;
  Configuration restored;
  assert(reboot.begin(restored));
  assert(restored.ssid == config.ssid && restored.password == config.password);
  assert(!restored.light.automatic && !restored.light.useLightSensor);
  assert(restored.light.brightness == 0 && restored.light.red == 7 && restored.light.green == 192 && restored.light.blue == 255);
  assert(restored.light.holdSeconds == 3600 && restored.light.darkThreshold == 1023);
  assert(restored.light.fadeInMs == 100 && restored.light.fadeOutMs == 9900);
  config.light.brightness = 88;
  assert(reboot.save(config));
  ConfigStore secondReboot;
  assert(secondReboot.begin(restored) && restored.light.brightness == 88);
  LittleFS.failWrites = true;
  config.light.brightness = 15;
  assert(!secondReboot.save(config));
  ConfigStore afterFailedWrite;
  assert(afterFailedWrite.begin(restored) && restored.light.brightness == 88);
  LittleFS.failWrites = false;
  LittleFS.files["/config.json"] = "damaged file";
  ConfigStore recovery;
  assert(recovery.begin(restored) && restored.light.brightness == 0);
  assert(restored.ssid == config.ssid);
  // Malformed patches do not partially change the active settings.
  DynamicJsonDocument doc(512); String error;
  deserializeJson(doc, "{\"brightness\":50,\"holdSeconds\":-1}");
  assert(!patchLight(doc.as<JsonObjectConst>(), restored.light, error));
  assert(restored.light.brightness == 0);
  LittleFS.files["/config.tmp"] = "old credentials";
  assert(recovery.reset());
  assert(LittleFS.files.empty());
  ConfigStore afterReset;
  Configuration defaults;
  assert(!afterReset.begin(defaults) && afterReset.ready());
  assert(defaults.ssid.length() == 0 && defaults.password.length() == 0);
  assert(defaults.light.brightness == LightSettings{}.brightness);
  LittleFS.failFormat = true;
  assert(!afterReset.reset() && !afterReset.ready());
  LittleFS.failFormat = false;
  assert(afterReset.reset() && afterReset.ready());
  puts("PASS: real ArduinoJson persistence, reboot restore of all settings and Wi-Fi, failed-write preservation, backup recovery, atomic validation");
}
