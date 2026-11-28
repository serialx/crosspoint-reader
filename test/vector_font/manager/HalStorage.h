#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
namespace manager_test {
extern std::vector<uint8_t> font;
extern size_t fileSize, opens, closes, reads, seeks;
extern int mode;
inline size_t impls = 0, implPeak = 0, implAllocations = 0;
inline const char* families[] = {"PTVec", "Long_Vector_Family_123456789"};
}  // namespace manager_test
// Match HalFile: default/moved-from has no Impl; close retains its Impl.
struct HalFile {
  struct Impl {
    int family, entry;
    size_t position;
    Impl(int f, int e, size_t p) : family(f), entry(e), position(p) {
      ++manager_test::implAllocations;
      manager_test::implPeak = std::max(manager_test::implPeak, ++manager_test::impls);
    }
    ~Impl() {
      close();
      --manager_test::impls;
    }
    void close() {
      if (family != -2 && entry == 0) ++manager_test::closes;
      family = -2;
    }
  };
  std::unique_ptr<Impl> impl;
  HalFile() = default;
  HalFile(int f, int e, size_t p) : impl(std::make_unique<Impl>(f, e, p)) {}
  bool isOpen() const { return impl && impl->family != -2; }
  explicit operator bool() const { return isOpen(); }
  bool isDirectory() const {
    assert(impl != nullptr);
    return impl->entry == -2;
  }
  void close() {
    assert(impl != nullptr);
    impl->close();
  }
  size_t size() const {
    assert(impl != nullptr);
    return manager_test::fileSize;
  }
  bool seek(size_t offset) {
    assert(impl != nullptr);
    ++manager_test::seeks;
    impl->position = offset;
    return isOpen() && offset <= size();
  }
  int read(uint8_t* out, size_t count) {
    assert(impl != nullptr);
    ++manager_test::reads;
    if (!isOpen() || manager_test::mode == 12) return -1;
    size_t& position = impl->position;
    count = std::min(count, position <= size() ? size() - position : 0);
    if (manager_test::mode == 13 && count) --count;
    const auto& bytes = manager_test::font;
    const size_t copy = std::min(count, position < bytes.size() ? bytes.size() - position : 0);
    if (copy) memcpy(out, bytes.data() + position, copy);
    if (count > copy) memset(out + copy, 0, count - copy);
    position += count;
    return static_cast<int>(count);
  }
  HalFile openNextFile() {
    assert(impl != nullptr);
    auto& f = *impl;
    if (f.family == -1 && f.position < 2) return {static_cast<int>(f.position++), -2, 0};
    if (f.family >= 0 && f.position++ == 0) return {f.family, -1, 0};
    return {-2, -2, 0};
  }
  void getName(char* out, size_t capacity) const {
    assert(impl != nullptr);
    snprintf(out, capacity, "%s", impl->entry == -2 ? manager_test::families[impl->family] : "NotoSans-Regular.ttf");
  }
};
struct StorageFixture {
  HalFile open(const char* path) const {
    if (!strcmp(path, "/.fonts")) return {-1, -2, 0};
    for (int i = 0; i != 2; ++i) {
      char prefix[128];
      snprintf(prefix, sizeof(prefix), "/.fonts/%s", manager_test::families[i]);
      if (!strcmp(path, prefix)) return {i, -2, 0};
      strcat(prefix, "/NotoSans-Regular.ttf");
      if (!strcmp(path, prefix)) {
        if (manager_test::mode == 11) return {-2, -2, 0};
        ++manager_test::opens;
        return {i, 0, 0};
      }
    }
    return {-2, -2, 0};
  }
  bool exists(const char* path) const { return bool(open(path)); }
};
inline StorageFixture Storage;
