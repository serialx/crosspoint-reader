#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "../../hal_fake_strict.h"

namespace fake {
inline size_t fileSize = 0;
inline int failSeek = -1;
inline bool openOk = true;
inline size_t readLimit = 1024;
inline std::vector<std::string> fileTrace;
}  // namespace fake
class HalFile {
  size_t offset = 0;

 public:
  hal_fake::Impl hasImpl;  // Stands in for HalFile::impl: a move leaves the source without one.
  size_t fileSize() {
    hal_fake::requireImpl(hasImpl, "fileSize", "");
    return fake::fileSize;
  }
  bool seekSet(size_t n) {
    hal_fake::requireImpl(hasImpl, "seekSet", "");
    fake::fileTrace.push_back("seek:" + std::to_string(n));
    offset = n;
    return int(n) != fake::failSeek;
  }
  size_t read(uint8_t* p, size_t n) {
    hal_fake::requireImpl(hasImpl, "read", "");
    n = std::min(n, fake::readLimit);
    fake::fileTrace.push_back("read:" + std::to_string(n));
    for (size_t i = 0; i < n; ++i) p[i] = uint8_t((offset + i) * 17 + (offset + i) / 256);
    return n;
  }
};
struct FakeStorage {
  bool openFileForRead(const char*, const std::string&, HalFile& file) {
    file.hasImpl = true;  // HalStorage installs an impl even when the open fails.
    return fake::openOk;
  }
};
inline FakeStorage Storage;
