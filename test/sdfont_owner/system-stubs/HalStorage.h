#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
namespace systemtest {
inline bool present = true;
inline bool family_end = false;
inline constexpr uint8_t points[] = {8, 10, 12, 14, 18, 20, 22, 24, 26};
}
class HalFile {
 public:
  int family = -2, entry = -2, cursor = 0;
  explicit operator bool() const { return family != -2; }
  bool isDirectory() const { return entry == -2; }
  bool close() { family = -2; return true; }
  bool seek(size_t) { return true; }
  int read(void*, size_t) { return 0; }
  size_t size() const { return 0; }
  HalFile openNextFile() {
    if (family == -1 && cursor < 2) return {cursor++, -2, 0};
    if (family >= 0 && cursor < 9) return {family, cursor++, 0};
    if (family >= 0) systemtest::family_end = true;
    return {};
  }
  void getName(char* out, size_t capacity) const {
    if (entry == -2) snprintf(out, capacity, "F%d", family);
    else snprintf(out, capacity, "%s_%u.cpfont", family == 0 ? "F0" : "Alias", systemtest::points[entry]);
  }
};
struct StorageFixture {
  HalFile open(const char* path) const {
    systemtest::family_end = false;
    if (!systemtest::present) return {};
    if (!strcmp(path, "/.fonts")) return {-1, -2, 0};
    int family;
    if (sscanf(path, "/.fonts/F%d", &family) == 1 && family >= 0 && family < 2) return {family, -2, 0};
    return {};
  }
  bool exists(const char* path) const { return bool(open(path)); }
};
inline StorageFixture Storage;
