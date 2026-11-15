#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../hal_fake_strict.h"

namespace systemtest {
inline bool present = true;
inline bool family_end = false;
inline constexpr uint8_t points[] = {8, 10, 12, 14, 18, 20, 22, 24, 26};
}  // namespace systemtest
class HalFile {
 public:
  int family = -2, entry = -2, cursor = 0;
  hal_fake::Impl hasImpl;  // Stands in for HalFile::impl: a move leaves the source without one.
  explicit operator bool() const { return hasImpl && family != -2; }
  static HalFile entryHandle(int family, int entry) { return {family, entry, 0, true}; }
  bool isDirectory() const {
    hal_fake::requireImpl(hasImpl, "isDirectory", "");
    return entry == -2;
  }
  bool close() {
    hal_fake::requireImpl(hasImpl, "close", "");
    family = -2;
    return true;
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
    if (family == -1 && cursor < 2) return entryHandle(cursor++, -2);
    if (family >= 0 && cursor < 9) return entryHandle(family, cursor++);
    if (family >= 0) systemtest::family_end = true;
    return entryHandle(-2, -2);  // Production always returns an entry handle with an impl.
  }
  void getName(char* out, size_t capacity) const {
    hal_fake::requireImpl(hasImpl, "getName", "");
    if (entry == -2)
      snprintf(out, capacity, "F%d", family);
    else
      snprintf(out, capacity, "%s_%u.cpfont", family == 0 ? "F0" : "Alias", systemtest::points[entry]);
  }
};
struct StorageFixture {
  HalFile open(const char* path) const {
    systemtest::family_end = false;
    // HalStorage installs an impl even when the path is unknown.
    if (!systemtest::present) return HalFile::entryHandle(-2, -2);
    if (!strcmp(path, "/.fonts")) return HalFile::entryHandle(-1, -2);
    int family;
    if (sscanf(path, "/.fonts/F%d", &family) == 1 && family >= 0 && family < 2) {
      return HalFile::entryHandle(family, -2);
    }
    return HalFile::entryHandle(-2, -2);
  }
  bool exists(const char* path) const { return bool(open(path)); }
};
inline StorageFixture Storage;
