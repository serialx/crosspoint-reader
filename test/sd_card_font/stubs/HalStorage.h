#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../hal_fake_strict.h"

inline std::vector<uint8_t> sdFontTestFile;
inline size_t sdFontTestReads = 0;

struct SdFontTestEsp {
  size_t largestBlock = 200 * 1024;
  size_t getFreeHeap() const { return 200 * 1024; }
  size_t getMaxAllocHeap() const { return largestBlock; }
};
inline SdFontTestEsp ESP;
inline uint32_t millis() { return 0; }

class HalFile {
 public:
  bool seekSet(size_t position) {
    hal_fake::requireImpl(hasImpl_, "seekSet", "");
    position_ = position;
    return opened_ && position <= sdFontTestFile.size();
  }
  int read(void* output, size_t count) {
    hal_fake::requireImpl(hasImpl_, "read", "");
    if (!opened_ || position_ > sdFontTestFile.size()) return 0;
    count = std::min(count, sdFontTestFile.size() - position_);
    std::memcpy(output, sdFontTestFile.data() + position_, count);
    position_ += count;
    sdFontTestReads++;
    return static_cast<int>(count);
  }
  void close() {
    hal_fake::requireImpl(hasImpl_, "close", "");
    opened_ = false;
  }
  size_t fileSize() const {
    hal_fake::requireImpl(hasImpl_, "fileSize", "");
    return sdFontTestFile.size();
  }
  void open() {
    hasImpl_ = true;  // HalStorage installs an impl even when the open fails.
    opened_ = true;
    position_ = 0;
  }

 private:
  size_t position_ = 0;
  bool opened_ = false;
  hal_fake::Impl hasImpl_;  // Stands in for HalFile::impl: a move leaves the source without one.
};

class HalStorage {
 public:
  bool openFileForRead(const char*, const char*, HalFile& file) {
    file.open();
    return true;
  }
};
inline HalStorage Storage;
