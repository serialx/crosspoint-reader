#pragma once

#include <cstdio>
#include <cstring>

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
  explicit operator bool() const { return family != -2; }
  bool isDirectory() const { return entry == -2; }
  void close() { family = -2; }
  bool seek(size_t) { return true; }
  int read(void*, size_t) { return 0; }
  size_t size() const { return 0; }
  HalFile openNextFile() {
    if (family == -1) {
      if (cursor < fixture::FAMILIES) return {cursor++, -2, 0};
    } else if (family >= 0) {
      const int count = family == fixture::FAMILIES - 1 ? fixture::VECTORS + 1 : 1;
      if (cursor < count) return {family, cursor++, 0};
    }
    return {};
  }
  void getName(char* out, size_t capacity) const {
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
    if (strcmp(path, "/.fonts") == 0) return {-1, -2, 0};
    int family;
    if (sscanf(path, "/.fonts/F%d", &family) == 1 && family >= 0 && family < fixture::FAMILIES) {
      return {family, -2, 0};
    }
    return {};
  }
  bool exists(const char* path) { return bool(open(path)); }
};
inline HalStorage Storage;
