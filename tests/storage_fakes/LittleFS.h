#pragma once
#include <map>
#include <string>
#include <cstring>
#include <cstdint>

class File {
 public:
  File() = default;
  File(std::string* data, bool failWrite) : data_(data), failWrite_(failWrite) {}
  explicit operator bool() const { return data_ != nullptr; }
  size_t size() const { return data_ ? data_->size() : 0; }
  int read() { return data_ && offset_ < data_->size() ? static_cast<unsigned char>((*data_)[offset_++]) : -1; }
  size_t readBytes(char* destination, size_t count) {
    size_t n = 0; int value;
    while (n < count && (value = read()) >= 0) destination[n++] = static_cast<char>(value);
    return n;
  }
  size_t write(uint8_t value) { return write(&value, 1); }
  size_t write(const uint8_t* bytes, size_t count) {
    if (!data_ || failWrite_) return 0;
    data_->append(reinterpret_cast<const char*>(bytes), count); return count;
  }
  void flush() {}
  void close() { data_ = nullptr; }
 private:
  std::string* data_ = nullptr;
  size_t offset_ = 0;
  bool failWrite_ = false;
};
struct FakeLittleFS {
  std::map<std::string, std::string> files;
  bool mountOk = true, failWrites = false, failRename = false, failFormat = false;
  void end() {}
  bool format() { if (failFormat) return false; files.clear(); return true; }
  bool begin() const { return mountOk; }
  File open(const char* path, const char* mode) {
    if (mode[0] == 'w') files[path].clear();
    auto it = files.find(path);
    return it == files.end() ? File{} : File{&it->second, failWrites};
  }
  bool remove(const char* path) { return files.erase(path) > 0; }
  bool rename(const char* from, const char* to) {
    if (failRename || !files.count(from)) return false;
    files[to] = files.at(from); files.erase(from); return true;
  }
};
inline FakeLittleFS LittleFS;
