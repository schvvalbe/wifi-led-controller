#include "FirmwareUpload.h"
#include <cassert>
#include <cstdio>
#include <vector>

struct Writer {
  size_t expected = 0, endCalls = 0, commits = 0;
  bool running = false, checksumOk = true, failWrite = false, error = false;
  std::vector<uint8_t> bytes;
  bool begin(size_t size, int command) {
    assert(command == 0); assert(!running);
    expected = size; running = true; error = false; bytes.clear(); return true;
  }
  bool setMD5(const char* md5) { return Firmware::validMd5(md5); }
  size_t write(uint8_t* data, size_t size) {
    if (failWrite) { error = true; return 0; }
    assert(running && bytes.size() + size <= expected);
    bytes.insert(bytes.end(), data, data + size); return size;
  }
  bool end() {
    ++endCalls;
    if (error || bytes.size() != expected) { running = false; return false; }
    if (!checksumOk) { error = true; return false; } // ESP8266 MD5-error behavior
    running = false; ++commits; return true;
  }
  bool isRunning() const { return running; }
};

int main() {
  constexpr const char* md5 = "0123456789abcdef0123456789abcdef";
  size_t size;
  assert(Firmware::parseSize("500000", 500000, size) && size == 500000);
  for (const char* bad : {"", "0", "15", "-1", "+16", " 16", "16.0", "500001", "99999999999999999999999"})
    assert(!Firmware::parseSize(bad, 500000, size));
  assert(!Firmware::validMd5("0123"));
  assert(!Firmware::validMd5("0123456789abcdef0123456789abcdeg"));
  Writer writer;
  Firmware::Upload<Writer> upload(writer);
  std::vector<uint8_t> image(4097, 0x55);
  image[0] = 0xE9;
  assert(upload.begin(image.size(), md5));
  assert(upload.append(image.data(), 1024));
  assert(upload.append(image.data() + 1024, image.size() - 1024));
  assert(writer.bytes.size() == image.size() - 1);
  assert(upload.complete());
  assert(writer.commits == 1 && writer.bytes == image);

  // Full payload received but HTTP request aborted: no bootloader commit.
  assert(upload.begin(image.size(), md5));
  assert(upload.append(image.data(), image.size()));
  upload.abort();
  assert(writer.commits == 1 && !writer.running);

  assert(upload.begin(image.size(), md5));
  assert(upload.append(image.data(), 33));
  assert(!upload.complete());
  assert(writer.commits == 1 && !writer.running);

  assert(upload.begin(image.size(), md5));
  assert(!upload.append(image.data(), image.size() + 1));
  upload.abort();
  image[0] = 0;
  assert(upload.begin(image.size(), md5));
  assert(!upload.append(image.data(), image.size()));
  upload.abort();
  image[0] = 0xE9;

  writer.checksumOk = false;
  assert(upload.begin(image.size(), md5));
  assert(upload.append(image.data(), image.size()));
  assert(!upload.complete());
  assert(writer.commits == 1 && !writer.running);
  writer.checksumOk = true;
  writer.failWrite = true;
  assert(upload.begin(image.size(), md5));
  assert(!upload.append(image.data(), 1024));
  upload.abort();
  assert(writer.commits == 1 && !writer.running);
  writer.failWrite = false;
  assert(upload.begin(image.size(), md5));
  assert(upload.append(image.data(), image.size()));
  assert(upload.complete() && writer.commits == 2);
  puts("PASS: OTA size/MD5 metadata, streaming, final-byte hold, truncation, abort, overflow, invalid image, checksum/write failure and retry");
}
