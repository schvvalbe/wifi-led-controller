#pragma once
#include "Arduino.h"
struct FakeMdns {
  bool begin(const String&) { return true; }
  void addService(const char*, const char*, int) {}
  void close() {}
  void update() {}
};
inline FakeMdns MDNS;
