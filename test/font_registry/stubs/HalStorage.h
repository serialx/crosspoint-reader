#pragma once

#include <cstdio>
#include <cstring>

#include "../../hal_fake_strict.h"

// Fixed directory entries keep storage-stub allocations out of the trace.
namespace fixture {
inline constexpr int FAMILIES = 9;
inline constexpr int VECTORS = 64;
}  // namespace fixture
class HalFile {
 public:
  int family = -2;
  int entry = -2;
  int cursor = 0;
  hal_fake::Impl hasImpl;  // Stands in for HalFile::impl: a move leaves the source without one.
  explicit operator bool() const { return hasImpl && family != -2; }
  static HalFile entryHandle(int family, int entry) { return {family, entry, 0, true}; }
  bool isDirectory() const {
    hal_fake::requireImpl(hasImpl, "isDirectory", "");
    return entry == -2;
  }
  void close() {
    hal_fake::requireImpl(hasImpl, "close", "");
    family = -2;
  }
  bool seek(size_t) {
    hal_fake::requireImpl(hasImpl, "seek", "");
    return true;
  }
  int read(void*, size_t) {
    hal_fake::requireImpl(hasImpl, "read", "");
    return 0;
  }
  size_t size() const {
    hal_fake::requireImpl(hasImpl, "size", "");
    return 0;
  }
  HalFile openNextFile() {
    hal_fake::requireImpl(hasImpl, "openNextFile", "");
    if (family == -1) {
      if (cursor < fixture::FAMILIES) return entryHandle(cursor++, -2);
    } else if (family >= 0) {
      const int count = family == fixture::FAMILIES - 1 ? fixture::VECTORS + 1 : 1;
      if (cursor < count) return entryHandle(family, cursor++);
    }
    return entryHandle(-2, -2);  // Production always returns an entry handle with an impl.
  }
  void getName(char* out, size_t capacity) const {
    hal_fake::requireImpl(hasImpl, "getName", "");
    if (entry == -2) {
      snprintf(out, capacity, "F%d", family);
    } else if (entry == 0) {
      snprintf(out, capacity, "F%d_14.cpfont", family);
    } else {
      snprintf(out, capacity, "Face%02d-abcdefghijklmnopqrstuvwxyz012.ttf", entry);
    }
  }
};
class HalStorage {
 public:
  HalFile open(const char* path) {
    // HalStorage installs an impl even when the path is unknown.
    if (strcmp(path, "/.fonts") == 0) return HalFile::entryHandle(-1, -2);
    int family;
    if (sscanf(path, "/.fonts/F%d", &family) == 1 && family >= 0 && family < fixture::FAMILIES) {
      return HalFile::entryHandle(family, -2);
    }
    return HalFile::entryHandle(-2, -2);
  }
  bool exists(const char* path) { return bool(open(path)); }
};
inline HalStorage Storage;
