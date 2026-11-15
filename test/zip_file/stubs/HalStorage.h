#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../hal_fake_strict.h"

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
  size_t size() {
    hal_fake::requireImpl(hasImpl, "size", "");
    return data ? data->size() : 0;
  }
  size_t position() const {
    hal_fake::requireImpl(hasImpl, "position", "");
    return pos;
  }
  int available() const {
    hal_fake::requireImpl(hasImpl, "available", "");
    return data ? static_cast<int>(data->size() - pos) : 0;
  }
  bool seek(size_t offset) { return seekSet(offset); }
  bool seekSet(size_t offset) {
    hal_fake::requireImpl(hasImpl, "seekSet", "");
    if (!ziptest::step("seek", offset) || !data || offset > data->size()) return false;
    pos = offset;
    return true;
  }
  bool seekCur(int64_t offset) {
    hal_fake::requireImpl(hasImpl, "seekCur", "");
    const int64_t target = static_cast<int64_t>(pos) + offset;
    if (target < 0) return false;
    return seekSet(static_cast<size_t>(target));
  }
  int read(void* dst, size_t len) {
    hal_fake::requireImpl(hasImpl, "read", "");
    if (!ziptest::step("read", pos, len) || !data || pos == failReadOffset) return -1;
    readOffsets.push_back(pos);
    const size_t remaining = data->size() - pos;
    size_t count = len < remaining ? len : remaining;
    if (pos == shortReadOffset && count > shortReadCount) count = shortReadCount;
    std::memcpy(dst, data->data() + pos, count);
    pos += count;
    return static_cast<int>(count);
  }
  size_t write(uint8_t) override {
    hal_fake::requireImpl(hasImpl, "write", "");
    return 0;
  }
  size_t write(const uint8_t*, size_t) override {
    hal_fake::requireImpl(hasImpl, "write", "");
    return 0;
  }
  bool close() {
    hal_fake::requireImpl(hasImpl, "close", "");
    ziptest::step("close");
    data = nullptr;
    pos = 0;
    return true;
  }
  bool isOpen() const { return hasImpl && data != nullptr; }
  explicit operator bool() const { return isOpen(); }

  static inline std::vector<size_t> readOffsets;
  static inline size_t failReadOffset = SIZE_MAX;
  static inline size_t shortReadOffset = SIZE_MAX, shortReadCount = 0;

 private:
  const std::vector<uint8_t>* data = nullptr;
  size_t pos = 0;
  hal_fake::Impl hasImpl;  // Stands in for HalFile::impl: a move leaves the source without one.
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    openCount++;
    file.hasImpl = true;  // HalStorage installs an impl even when the open fails.
    if (!ziptest::step("open")) return false;
    if (path.empty() || path != filePath) return false;
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
    HalFile::shortReadOffset = SIZE_MAX;
  }

  int openCount = 0;

 private:
  std::string filePath;
  std::vector<uint8_t> fileData;
};

#define Storage HalStorage::getInstance()
