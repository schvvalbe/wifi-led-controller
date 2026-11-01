#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#define F(text) text
#define HEX 16
inline uint32_t fakeMillis = 0;
inline uint32_t millis() { return fakeMillis; }
class String {
 public:
  String() = default;
  String(const char* s) : value_(s) {}
  String(const std::string& s) : value_(s) {}
  String(uint32_t n, int) { char buf[16]; snprintf(buf, sizeof(buf), "%x", n); value_ = buf; }
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.length(); }
  char operator[](size_t index) const { return value_[index]; }
  bool concat(const char* text) { value_ += text; return true; }
  String& operator+=(const String& other) { value_ += other.value_; return *this; }
  bool isEmpty() const { return value_.empty(); }
  void toUpperCase() { std::transform(value_.begin(), value_.end(), value_.begin(), [](unsigned char c) { return std::toupper(c); }); }
  void toLowerCase() { std::transform(value_.begin(), value_.end(), value_.begin(), [](unsigned char c) { return std::tolower(c); }); }
  friend String operator+(const String& a, const String& b) { return a.value_ + b.value_; }
  friend bool operator==(const String& a, const String& b) { return a.value_ == b.value_; }
 private:
  std::string value_;
};
struct FakeSerial { template<typename... T> void printf(const char*, T...) {} };
struct FakeEsp { uint32_t getChipId() const { return 0x123abc; } };
inline FakeSerial Serial;
inline FakeEsp ESP;
