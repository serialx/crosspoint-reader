#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// In-memory transport only; production Page/TextBlock/ImageBlock codecs are used.
class HalFile {
 public:
  HalFile() = default;
  HalFile(const uint8_t* bytes, size_t length) : data(bytes), length(length), opened(true) {}
  explicit HalFile(std::vector<uint8_t>& output) : output(&output), opened(true) { output.clear(); }
  size_t read(void* target, size_t count) {
    count = std::min(count, length - offset);
    if (count) std::memcpy(target, data + offset, count);
    offset += count;
    return count;
  }
  size_t write(const void* source, size_t count) {
    if (!output) return 0;
    const auto* bytes = static_cast<const uint8_t*>(source);
    output->insert(output->end(), bytes, bytes + count);
    offset += count;
    return count;
  }
  size_t write(uint8_t value) { return write(&value, 1); }
  size_t position() const { return offset; }
  size_t size() const { return output ? output->size() : length; }
  bool seek(size_t at) {
    if (at > size()) return false;
    offset = at;
    return true;
  }
  bool seekCur(size_t delta) { return seek(offset + delta); }
  int available() const { return static_cast<int>(size() - offset); }
  bool flush() { return true; }
  bool close() {
    opened = false;
    return true;
  }
  bool isOpen() const { return opened; }
  explicit operator bool() const { return opened; }

 private:
  const uint8_t* data = nullptr;
  size_t length = 0, offset = 0;
  std::vector<uint8_t>* output = nullptr;
  bool opened = false;
};
class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  bool openFileForRead(const char*, const std::string&, HalFile&) { return false; }
  bool openFileForWrite(const char*, const std::string&, HalFile&) { return false; }
  bool remove(const std::string&) { return false; }
  bool exists(const char*) const { return false; }
};
#define Storage HalStorage::getInstance()
inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
