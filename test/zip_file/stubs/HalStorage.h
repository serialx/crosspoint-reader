#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ziptest {
inline bool trace = false;
inline size_t operations = 0, fail = 0;
inline bool step(const char* name, size_t a = 0, size_t b = 0) {
  const bool ok = ++operations != fail;
  if (trace) std::printf("io %s %zu %zu %d\n", name, a, b, ok);
  return ok;
}
}  // namespace ziptest

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t data) = 0;
  virtual size_t write(const uint8_t* data, size_t size) {
    size_t written = 0;
    while (written < size && write(data[written]) == 1) written++;
    return written;
  }
};

// In-memory read-only file. Records every read offset so tests can prove
// which regions of the archive a lookup touched.
class HalFile : public Print {
  friend class HalStorage;

 public:
  size_t size() { return data ? data->size() : 0; }
  size_t position() const { return pos; }
  int available() const { return data ? static_cast<int>(data->size() - pos) : 0; }
  bool seek(size_t offset) { return seekSet(offset); }
  bool seekSet(size_t offset) {
    if (!ziptest::step("seek", offset) || !data || offset > data->size()) return false;
    pos = offset;
    return true;
  }
  bool seekCur(int64_t offset) {
    const int64_t target = static_cast<int64_t>(pos) + offset;
    if (target < 0) return false;
    return seekSet(static_cast<size_t>(target));
  }
  int read(void* dst, size_t len) {
    if (!ziptest::step("read", pos, len) || !data || pos == failReadOffset) return -1;
    readOffsets.push_back(pos);
    const size_t remaining = data->size() - pos;
    const size_t count = len < remaining ? len : remaining;
    std::memcpy(dst, data->data() + pos, count);
    pos += count;
    return static_cast<int>(count);
  }
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t*, size_t) override { return 0; }
  bool close() {
    ziptest::step("close");
    data = nullptr;
    pos = 0;
    return true;
  }
  bool isOpen() const { return data != nullptr; }
  explicit operator bool() const { return isOpen(); }

  static inline std::vector<size_t> readOffsets;
  static inline size_t failReadOffset = SIZE_MAX;

 private:
  const std::vector<uint8_t>* data = nullptr;
  size_t pos = 0;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    openCount++;
    if (!ziptest::step("open")) return false;
    if (path != filePath) return false;
    file.data = &fileData;
    file.pos = 0;
    return true;
  }

  void setFile(const std::string& path, std::vector<uint8_t> bytes) {
    filePath = path;
    fileData = std::move(bytes);
    openCount = 0;
    HalFile::readOffsets.clear();
    HalFile::failReadOffset = SIZE_MAX;
  }

  int openCount = 0;

 private:
  std::string filePath;
  std::vector<uint8_t> fileData;
};

#define Storage HalStorage::getInstance()
