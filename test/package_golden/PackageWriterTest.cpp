#include <xp_ffi.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();
extern "C" size_t xp_test_allocation_attempts();

namespace {
struct File {
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  bool open = false;
};
struct Storage {
  std::array<File, 4> files;
  bool shortWrite = false;
  bool failFlush = false;
  size_t sizeLookups = 0;
  XpPkgIo io() { return {this, control, read, write, seek, itemSize, nullptr}; }
  static uint32_t control(void* ptr, uint32_t id, uint32_t op) {
    auto& self = *static_cast<Storage*>(ptr);
    auto& file = self.files.at(id);
    switch (op) {
      case XP_PKG_OPEN_WRITE:
        file.bytes.clear();
        [[fallthrough]];
      case XP_PKG_OPEN_READ:
        file.pos = 0;
        file.open = true;
        return 1;
      case XP_PKG_CLOSE:
        file.open = false;
        return 1;
      case XP_PKG_REMOVE:
        file.bytes.clear();
        return 1;
      case XP_PKG_IS_OPEN:
        return file.open;
      case XP_PKG_POSITION:
        return file.pos;
      case XP_PKG_SIZE:
        return file.bytes.size();
      case XP_PKG_END_BUILD:
        return !self.failFlush;
      default:
        return 1;
    }
  }
  static size_t read(void* ptr, uint32_t id, uint8_t* bytes, size_t len) {
    auto& file = static_cast<Storage*>(ptr)->files.at(id);
    const auto count = std::min(len, file.bytes.size() - std::min(file.pos, file.bytes.size()));
    if (count) std::memcpy(bytes, file.bytes.data() + file.pos, count);
    file.pos += count;
    return count;
  }
  static size_t write(void* ptr, uint32_t id, const uint8_t* bytes, size_t len) {
    auto& self = *static_cast<Storage*>(ptr);
    if (self.shortWrite && len) return len - 1;
    auto& file = self.files.at(id);
    file.bytes.resize(file.pos + len);
    if (len) std::memcpy(file.bytes.data() + file.pos, bytes, len);
    file.pos += len;
    return len;
  }
  static uint8_t seek(void* ptr, uint32_t id, uint32_t pos) {
    auto& file = static_cast<Storage*>(ptr)->files.at(id);
    if (pos > file.bytes.size()) return 0;
    file.pos = pos;
    return 1;
  }
  static uint32_t itemSize(void* ptr, uint16_t index, XpPkgSpan) {
    ++static_cast<Storage*>(ptr)->sizeLookups;
    return index + 10;
  }
};
XpPkgSpan span(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
bool run(size_t limit, size_t count, bool shortWrite = false, bool failFlush = false) {
  const std::string title = "Cafe\xcc\x81";
  std::string opf = "<package><metadata><title>Book</title></metadata><manifest>";
  for (size_t i = 0; i < count; ++i)
    opf += "<item id='id" + std::to_string(i) + "' href='chapter" + std::to_string(i) + ".xhtml'/>";
  opf += "</manifest><spine>";
  for (size_t i = 0; i < count; ++i) opf += "<itemref idref='id" + std::to_string(i) + "'/>";
  opf += "</spine></package>";
  Storage store;
  auto io = store.io();
  XpPkgCache* cache = nullptr;
  XpPkgPass* pass = nullptr;
  xp_test_fail_allocation_after(limit);
  bool ok = xp_pkg_cache_create(&cache) == 0;
  if (ok) ok = xp_pkg_cache_control(cache, &io, 0) == 0;
  if (ok) ok = xp_pkg_pass_create(XP_PKG_OPF_PASS, {}, opf.size(), 0, &io, cache, &io, &pass) == 0;
  if (ok) ok = xp_pkg_pass_write(pass, span(opf)) == opf.size();
  xp_pkg_pass_destroy(pass);
  if (ok) ok = xp_pkg_cache_count(cache, 0) == count && xp_pkg_cache_control(cache, &io, 1) == 0;
  if (ok) ok = xp_pkg_cache_control(cache, &io, 2) == 0;
  if (ok) {
    std::string href = "chapter0.xhtml";
    ok = xp_pkg_cache_entry(cache, &io, 1, span(title), span(href), {}, 1) == 0;
  }
  if (ok) ok = xp_pkg_cache_control(cache, &io, 3) == 0 && xp_pkg_cache_control(cache, &io, 4) == 0;
  if (ok) {
    store.shortWrite = shortWrite;
    store.failFlush = failFlush;
    XpPkgSpan fields[5] = {span(title), {}, {}, {}, {}};
    ok = xp_pkg_cache_build(cache, &io, fields) == 0;
    if (!ok) assert(store.files[XP_PKG_BOOK_FILE].bytes.empty());
    if (ok) {
      assert(store.sizeLookups == count);
      const auto& output = store.files[XP_PKG_BOOK_FILE].bytes;
      assert(output.size() > 9 && output[0] == 10);
      uint32_t offset = 0;
      std::memcpy(&offset, output.data() + 1, 4);
      uint32_t spine = 0;
      std::memcpy(&spine, output.data() + offset, 4);
      uint32_t hrefLength = 0;
      std::memcpy(&hrefLength, output.data() + spine, 4);
      uint32_t cumulative = 0;
      std::memcpy(&cumulative, output.data() + spine + 4 + hrefLength, 4);
      assert(cumulative == 10);
    }
  }
  xp_pkg_cache_destroy(cache);
  assert(xp_test_live_allocations() == 0);
  return ok;
}
}  // namespace
int main() {
  bool success = false;
  for (size_t limit = 0; limit < 2048 && !success; ++limit) success = run(limit, 4);
  assert(success);
  assert(run(SIZE_MAX, 405));
  assert(!run(SIZE_MAX, 4, true));
  assert(!run(SIZE_MAX, 4, false, true));
  xp_test_fail_allocation_after(SIZE_MAX);
}
