#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Firmware {
inline bool parseSize(const char* text, size_t limit, size_t& result) {
  result = 0;
  if (!text || !*text) return false;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    const size_t digit = static_cast<size_t>(*text - '0');
    if (digit > limit || result > (limit - digit) / 10) return false;
    result = result * 10 + digit;
  }
  return result >= 16;
}

inline bool validMd5(const char* text) {
  if (!text || std::strlen(text) != 32) return false;
  for (size_t i = 0; i < 32; ++i)
    if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f') ||
          (text[i] >= 'A' && text[i] <= 'F'))) return false;
  return true;
}

// Keep the final byte out of the updater until the ENTIRE HTTP request ends.
// ESP8266 Update.end() can commit a full image even when used to abort; holding
// one byte makes cancellation safe, including a broken final multipart boundary.
template<class Writer>
class Upload {
 public:
  explicit Upload(Writer& writer) : writer_(writer) {}
  bool begin(size_t size, const char* md5) {
    abort();
    if (size < 16 || !validMd5(md5)) return false;
    expected_ = size;
    received_ = 0;
    if (!writer_.begin(size, 0)) return false; // U_FLASH only, never LittleFS
    active_ = true;
    if (!writer_.setMD5(md5)) { abort(); return false; }
    return true;
  }
  bool append(uint8_t* data, size_t size) {
    if (!active_ || size > expected_ - received_) return false;
    if (!size) return true;
    if (!received_ && data[0] != 0xE9) return false; // uncompressed ESP8266 .bin
    const bool last = received_ + size == expected_;
    const size_t toWrite = size - (last ? 1 : 0);
    if (toWrite && writer_.write(data, toWrite) != toWrite) return false;
    if (last) tail_ = data[size - 1];
    received_ += size;
    return true;
  }
  bool complete() {
    if (!active_ || received_ != expected_) { abort(); return false; }
    const bool ok = writer_.write(&tail_, 1) == 1 && writer_.end();
    // The core leaves the updater running after an MD5 error; end again
    // clears that error's buffers without scheduling a bootloader copy.
    if (!ok && writer_.isRunning()) writer_.end();
    active_ = false;
    return ok;
  }
  void abort() {
    if (active_ && writer_.isRunning()) writer_.end();
    active_ = false;
    received_ = 0;
  }
  size_t received() const { return received_; }
 private:
  Writer& writer_;
  size_t expected_ = 0, received_ = 0;
  uint8_t tail_ = 0;
  bool active_ = false;
};
}
