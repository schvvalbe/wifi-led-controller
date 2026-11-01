#pragma once
// No-op JSON views: network policy tests exercise the real NetworkManager,
// but serialization is covered by the target build and browser contract tests.
#include <vector>
class FakeJsonValue {
 public:
  template<class T> FakeJsonValue& operator=(const T&) { return *this; }
  template<class T> bool operator==(const T&) const { return false; }
};
class JsonArray;
class JsonObject {
 public:
  FakeJsonValue operator[](const char*) { return {}; }
  JsonArray createNestedArray(const char*);
};
class JsonObjectConst {};
class JsonArray {
 public:
  size_t size() const { return objects_.size(); }
  JsonObject createNestedObject() { objects_.push_back({}); return {}; }
  auto begin() { return objects_.begin(); }
  auto end() { return objects_.end(); }
 private:
  std::vector<JsonObject> objects_;
};
inline JsonArray JsonObject::createNestedArray(const char*) { return {}; }
