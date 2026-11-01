#pragma once
#include "ESP8266WiFi.h"
struct DNSServer {
  void start(int, const char*, IPAddress) {}
  void stop() {}
  void processNextRequest() {}
};
