#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../hal_fake_strict.h"

inline std::vector<uint8_t> xtcTestFile;
inline int h6FailRead = -1, h6FailSeek = -1;
class HalFile {
 public:
  bool seek64(uint64_t offset) {
    hal_fake::requireImpl(hasImpl_, "seek64", "");
    if (seeks++ == h6FailSeek || !opened || offset > xtcTestFile.size()) return false;
    position = offset;
    return true;
  }
  bool seek(uint32_t offset) { return seek64(offset); }
  size_t read(void* out, size_t size) {
    hal_fake::requireImpl(hasImpl_, "read", "");
    if (reads++ == h6FailRead || !opened) return 0;
    size = std::min(size, xtcTestFile.size() - position);
    if (size) memcpy(out, xtcTestFile.data() + position, size);
    position += size;
    return size;
  }
  uint64_t fileSize64() const {
    hal_fake::requireImpl(hasImpl_, "fileSize64", "");
    return xtcTestFile.size();
  }
  bool isOpen() const { return hasImpl_ && opened; }
  void close() {
    hal_fake::requireImpl(hasImpl_, "close", "");
    opened = false;
  }
  void open() {
    hasImpl_ = true;  // HalStorage installs an impl even when the open fails.
    opened = true;
    position = 0;
  }

 private:
  int reads = 0, seeks = 0;
  size_t position = 0;
  bool opened = false;
  hal_fake::Impl hasImpl_;  // Stands in for HalFile::impl: a move leaves the source without one.
};
struct HalStorage {
  bool openFileForRead(const char*, const char*, HalFile& file) {
    file.open();
    return true;
  }
};
inline HalStorage Storage;
