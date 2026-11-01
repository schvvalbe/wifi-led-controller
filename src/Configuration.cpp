#include "Configuration.h"
#include <LittleFS.h>

void writeLight(JsonObject j, const LightSettings& s) {
  j["mode"] = s.automatic ? "auto" : "manual";
  j["useLightSensor"] = s.useLightSensor;
  j["holdSeconds"] = s.holdSeconds;
  j["darkThreshold"] = s.darkThreshold;
  j["brightness"] = s.brightness;
  char color[8];
  snprintf(color, sizeof(color), "#%02x%02x%02x", s.red, s.green, s.blue);
  j["color"] = color;
  j["fadeInMs"] = s.fadeInMs;
  j["fadeOutMs"] = s.fadeOutMs;
}

bool patchLight(JsonObjectConst j, LightSettings& s, String& error) {
  if (j.isNull()) { error = F("Ожидается JSON-объект"); return false; }
  LightSettings next = s;
  for (JsonPairConst kv : j) {
    const String key = kv.key().c_str();
    JsonVariantConst v = kv.value();
    if (key == "mode") {
      if (!v.is<const char*>() || (v != "auto" && v != "manual")) { error = F("Неизвестный режим"); return false; }
      next.automatic = v == "auto";
    } else if (key == "useLightSensor") {
      if (!v.is<bool>()) { error = F("Ожидается логическое значение"); return false; }
      next.useLightSensor = v.as<bool>();
    } else if (key == "color") {
      if (!v.is<const char*>()) { error = F("Неверный цвет"); return false; }
      const String color = v.as<String>();
      if (color.length() != 7 || color[0] != '#') { error = F("Цвет: #RRGGBB"); return false; }
      for (unsigned i = 1; i < 7; ++i) if (!isxdigit(static_cast<unsigned char>(color[i]))) { error = F("Цвет: #RRGGBB"); return false; }
      const uint32_t rgb = strtoul(color.c_str() + 1, nullptr, 16);
      next.red = rgb >> 16; next.green = rgb >> 8; next.blue = rgb;
    } else {
      uint32_t min = 0, max = 0;
      if (key == "holdSeconds") { min = 1; max = 3600; }
      else if (key == "darkThreshold") max = 1023;
      else if (key == "brightness") max = 100;
      else if (key == "fadeInMs" || key == "fadeOutMs") max = 10000;
      else { error = F("Неизвестное поле: "); error += key; return false; }
      if (!v.is<uint32_t>() || v.as<uint32_t>() < min || v.as<uint32_t>() > max) {
        error = F("Значение вне диапазона: "); error += key; return false;
      }
      const uint32_t value = v.as<uint32_t>();
      if (key == "holdSeconds") next.holdSeconds = value;
      else if (key == "darkThreshold") next.darkThreshold = value;
      else if (key == "brightness") next.brightness = value;
      else if (key == "fadeInMs") next.fadeInMs = value;
      else next.fadeOutMs = value;
    }
  }
  s = next;
  return true;
}

bool ConfigStore::read(const char* path, Configuration& config) {
  File file = LittleFS.open(path, "r");
  if (!file || file.size() > 2048) return false;
  DynamicJsonDocument doc(1536);
  if (deserializeJson(doc, file) || doc["version"] != 1) return false;
  Configuration next;
  String error;
  JsonObjectConst light = doc["light"];
  if (light.size() != 8 || !patchLight(light, next.light, error)) return false;
  if (!doc["ssid"].is<String>() || !doc["password"].is<String>()) return false;
  next.ssid = doc["ssid"].as<String>();
  next.password = doc["password"].as<String>();
  if (next.ssid.length() > 32 || next.password.length() > 64) return false;
  config = next;
  return true;
}

bool ConfigStore::begin(Configuration& config) {
  // First firmware upload may encounter an old SPIFFS partition. LittleFS autoformats it.
  ready_ = LittleFS.begin();
  if (!ready_) return false;
  return read("/config.json", config) || read("/config.bak", config);
}

bool ConfigStore::save(const Configuration& config) {
  if (!ready_) return false;
  DynamicJsonDocument doc(1536);
  doc["version"] = 1;
  writeLight(doc.createNestedObject("light"), config.light);
  doc["ssid"] = config.ssid;
  doc["password"] = config.password;
  File file = LittleFS.open("/config.tmp", "w");
  if (!file) return false;
  const size_t expected = measureJson(doc);
  const size_t written = serializeJson(doc, file);
  file.flush(); file.close();
  if (written != expected) return false;
  // Verify before replacing the last known configuration.
  Configuration verified;
  if (!read("/config.tmp", verified)) return false;
  Configuration previous;
  if (read("/config.json", previous)) {
    LittleFS.remove("/config.bak");
    if (!LittleFS.rename("/config.json", "/config.bak")) return false;
  } else LittleFS.remove("/config.json");
  return LittleFS.rename("/config.tmp", "/config.json");
}

// Clear the complete data partition, including backup and temporary files.
// Web assets and the firmware live in the sketch partition and remain intact.
bool ConfigStore::reset() {
  LittleFS.end();
  ready_ = false;
  if (!LittleFS.format()) return false;
  ready_ = LittleFS.begin();
  return ready_;
}
