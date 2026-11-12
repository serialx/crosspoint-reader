#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../../hal_fake_strict.h"

constexpr int O_WRITE = 1, O_CREAT = 2, O_TRUNC = 4;

struct ReadFault {
  std::string path;
  size_t begin = 0, end = 0, failAt = 0, maxRead = 0;
  size_t shortReads = 0, failures = 0;
};
inline ReadFault readFault;

class HalFile {
 public:
  std::vector<uint8_t> bytes;
  size_t cursor = 0;
  std::string path;
  std::vector<uint8_t>* backing = nullptr;
  hal_fake::Impl hasImpl;  // Stands in for HalFile::impl: a move leaves the source without one.
  // The probes hand an already-open output sink to the decoder; there is no volume to open.
  bool opened = false;
  void markOpen() {
    hasImpl = true;
    opened = true;
  }
  explicit operator bool() const { return hasImpl && opened; }
  void close() {
    hal_fake::requireImpl(hasImpl, "close", path.c_str());
    opened = false;
  }
  size_t position() const {
    hal_fake::requireImpl(hasImpl, "position", path.c_str());
    return cursor;
  }
  size_t fileSize() const {
    hal_fake::requireImpl(hasImpl, "fileSize", path.c_str());
    return bytes.size();
  }
  bool seekSet(size_t offset) {
    hal_fake::requireImpl(hasImpl, "seekSet", path.c_str());
    if (offset > bytes.size()) return false;
    cursor = offset;
    return true;
  }
  int read(void* output, size_t length) {
    hal_fake::requireImpl(hasImpl, "read", path.c_str());
    size_t count = std::min(length, bytes.size() - cursor);
    if (path == readFault.path && cursor >= readFault.begin && cursor < readFault.end && readFault.maxRead) {
      if (cursor >= readFault.failAt) {
        ++readFault.failures;
        return -1;
      }
      count = std::min({count, readFault.maxRead, readFault.failAt - cursor});
      if (count && count < length) ++readFault.shortReads;
    }
    if (count) memcpy(output, bytes.data() + cursor, count);
    cursor += count;
    return static_cast<int>(count);
  }
  int read() {
    hal_fake::requireImpl(hasImpl, "read", path.c_str());
    if (cursor == bytes.size()) return -1;
    return bytes[cursor++];
  }
  size_t write(const uint8_t* data, size_t size) {
    hal_fake::requireImpl(hasImpl, "write", path.c_str());
    auto& target = backing ? *backing : bytes;
    target.insert(target.end(), data, data + size);
    return size;
  }
};
struct HalStorage {
  std::string temporaryPath;
  std::vector<uint8_t> temporary;
  size_t temporaryReads = 0, truncations = 0;
  HalFile open(const char* path, int flags) {
    temporaryPath = path;
    if (flags & O_TRUNC) {
      temporary.clear();
      ++truncations;
    }
    HalFile file;
    file.hasImpl = true;  // HalStorage installs an impl even when the open fails.
    file.opened = true;
    file.backing = &temporary;
    return file;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file.hasImpl = true;  // HalStorage installs an impl even when the open fails.
    file.opened = false;
    if (*path && path == temporaryPath) {
      file.opened = true;
      file.bytes = temporary;
      file.cursor = 0;
      ++temporaryReads;
      return true;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    file.opened = true;
    file.bytes.assign(std::istreambuf_iterator<char>(input), {});
    file.cursor = 0;
    file.path = path;
    return true;
  }
};
inline HalStorage Storage;
