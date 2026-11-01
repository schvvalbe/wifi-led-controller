#include "OtaManager.h"
#include "FirmwareInfo.h"

size_t OtaManager::maxSize() {
  const uint32_t free = ESP.getFreeSketchSpace();
  return free > 0x1000 ? (free - 0x1000) & 0xFFFFF000 : 0;
}

void OtaManager::writeState(JsonObject json) const {
  json["maxSize"] = maxSize();
  json["busy"] = busy();
  json["passwordRequired"] = true;
}

void OtaManager::begin(ESP8266WebServer& server, std::function<bool()> prepare) {
  server_ = &server;
  prepare_ = std::move(prepare);
  server.on("/api/ota", HTTP_GET, [this] {
    StaticJsonDocument<256> doc;
    writeState(doc.to<JsonObject>());
    doc["version"] = Firmware::Version;
    String body;
    serializeJson(doc, body);
    server_->sendHeader("Cache-Control", "no-store");
    server_->send(200, "application/json; charset=utf-8", body);
  });
  server.on("/api/ota", HTTP_POST, [this] { finish(); }, [this] { receive(); });
}

void OtaManager::fail(int status, const String& message) {
  if (!errorStatus_) { errorStatus_ = status; error_ = message; }
  upload_.abort();
  receiving_ = false;
}

void OtaManager::resetRequest() {
  upload_.abort();
  requestOpen_ = receiving_ = fileEnded_ = false;
  errorStatus_ = 0;
  error_ = "";
  expected_ = 0;
}

void OtaManager::receive() {
  HTTPUpload& file = server_->upload();
  if (file.status == UPLOAD_FILE_START) {
    if (requestOpen_) { fail(400, F("Загрузите ровно один файл прошивки.")); return; }
    resetRequest();
    requestOpen_ = true;
    if (rebootPending_) { fail(409, F("Контроллер уже перезапускается.")); return; }
    const String origin = server_->header("Origin");
    if (!origin.isEmpty() && origin != "http://" + server_->hostHeader() && origin != "https://" + server_->hostHeader()) {
      fail(403, F("Недопустимый источник запроса.")); return;
    }
    const String password = server_->header("X-OTA-Password");
    if (!password.equalsConstantTime(String(Firmware::OtaPassword))) {
      fail(403, F("Неверный пароль обновления.")); return;
    }
    String filename = file.filename;
    filename.toLowerCase();
    if (file.name != "firmware" || !filename.endsWith(".bin")) {
      fail(400, F("Выберите файл firmware.bin для этого контроллера.")); return;
    }
    if (!Firmware::parseSize(server_->header("X-Firmware-Size").c_str(), maxSize(), expected_)) {
      fail(413, F("Неверный размер файла или недостаточно места для OTA.")); return;
    }
    const String md5 = server_->header("X-Firmware-MD5");
    if (!Firmware::validMd5(md5.c_str())) { fail(400, F("Не указана контрольная сумма прошивки.")); return; }
    if (!prepare_()) { fail(409, F("Дождитесь сохранения настроек и завершения подключения или поиска Wi-Fi.")); return; }
    if (!upload_.begin(expected_, md5.c_str())) {
      fail(500, String(F("Не удалось начать обновление: ")) + Update.getErrorString()); return;
    }
    receiving_ = true;
  } else if (file.status == UPLOAD_FILE_WRITE && receiving_ && !errorStatus_) {
    if (!upload_.append(file.buf, file.currentSize))
      fail(400, F("Неверный образ прошивки, превышен размер или произошла ошибка записи."));
  } else if (file.status == UPLOAD_FILE_END && receiving_ && !errorStatus_) {
    if (file.totalSize != expected_ || upload_.received() != expected_)
      fail(400, F("Файл получен не полностью. Обновление отменено."));
    else fileEnded_ = true;
  } else if (file.status == UPLOAD_FILE_ABORTED) {
    // There may be no final HTTP callback after a disconnected upload.
    resetRequest();
  }
  yield();
}

void OtaManager::finish() {
  if (!requestOpen_) fail(400, F("Файл прошивки не получен."));
  if (!errorStatus_ && (!fileEnded_ || !receiving_)) fail(400, F("Загрузка файла не завершена."));
  if (!errorStatus_ && !upload_.complete())
    fail(400, String(F("Проверка прошивки не пройдена: ")) + Update.getErrorString());
  StaticJsonDocument<384> doc;
  const int status = errorStatus_ ? errorStatus_ : 202;
  if (errorStatus_) doc["error"] = error_;
  else {
    doc["rebooting"] = true;
    rebootPending_ = true;
    rebootAt_ = millis();
    Serial.println(F("OTA verified. Restarting."));
  }
  String body;
  serializeJson(doc, body);
  server_->sendHeader("Cache-Control", "no-store");
  server_->sendHeader("Connection", "close");
  server_->send(status, "application/json; charset=utf-8", body);
  resetRequest();
}

void OtaManager::tick() {
  if (rebootPending_ && static_cast<uint32_t>(millis() - rebootAt_) >= 1500) ESP.restart();
}
