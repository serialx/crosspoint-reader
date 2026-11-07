#include "RustRecords.h"

#include <HalStorage.h>

#include <cassert>

namespace {
struct Bytes {
  const uint8_t* data;
  size_t length;
};
Bytes bytes(const std::string& text) { return {reinterpret_cast<const uint8_t*>(text.data()), text.size()}; }
std::string string(Bytes text) { return {reinterpret_cast<const char*>(text.data), text.length}; }
struct Row {
  Bytes xpath, summary, name;
  float percentage;
  uint16_t spine, count, page;
  uint8_t hasOffset;
  uint32_t offset;
};
using Receive = void (*)(void*, Bytes);
struct Operations {
  uint8_t (*exists)(Bytes);
  void (*mkdir)(Bytes);
  uint8_t (*read)(Bytes, Receive, void*);
  uint8_t (*write)(Bytes, Bytes);
};
const Operations operations{
    [](Bytes path) -> uint8_t { return Storage.exists(string(path).c_str()); },
    [](Bytes path) { Storage.mkdir(string(path).c_str()); },
    [](Bytes path, Receive receive, void* context) -> uint8_t {
      const auto input = Storage.readFile(string(path).c_str());
      if (input.isEmpty()) return 0;
      receive(context, {reinterpret_cast<const uint8_t*>(input.c_str()), input.length()});
      return 1;
    },
    [](Bytes path, Bytes data) -> uint8_t { return Storage.writeFileAtomic(string(path).c_str(), data.data, data.length); },
};
}  // namespace

extern "C" {
void* xp_bookmarks_test_create(const Operations*);
void xp_bookmarks_test_destroy(void*);
uint8_t xp_bookmarks_test_push(void*, const Row*);
size_t xp_bookmarks_test_count(const void*);
uint8_t xp_bookmarks_test_row(const void*, size_t, Row*);
uint8_t xp_bookmarks_test_load(void*, Bytes);
uint8_t xp_bookmarks_test_save(const void*, Bytes);
void xp_bookmarks_test_path(Bytes, Receive, void*);
}
RustRecords::RustRecords() : owner(xp_bookmarks_test_create(&operations)) { assert(owner); }
RustRecords::~RustRecords() { xp_bookmarks_test_destroy(owner); }
void RustRecords::push_back(const BookmarkEntry& b) {
  const Row row{bytes(b.xpath),
                bytes(b.summary),
                bytes(b.name),
                b.percentage,
                b.computedSpineIndex,
                b.computedChapterPageCount,
                b.computedChapterProgress,
                static_cast<uint8_t>(b.hasVisibleTextOffset),
                b.visibleTextOffset};
  assert(xp_bookmarks_test_push(owner, &row));
}
size_t RustRecords::size() const { return xp_bookmarks_test_count(owner); }
const BookmarkEntry& RustRecords::operator[](size_t index) const {
  Row row{};
  assert(xp_bookmarks_test_row(owner, index, &row));
  snapshot.xpath = string(row.xpath);
  snapshot.summary = string(row.summary);
  snapshot.name = string(row.name);
  snapshot.percentage = row.percentage;
  snapshot.computedSpineIndex = row.spine;
  snapshot.computedChapterPageCount = row.count;
  snapshot.computedChapterProgress = row.page;
  snapshot.hasVisibleTextOffset = row.hasOffset != 0;
  snapshot.visibleTextOffset = row.offset;
  return snapshot;
}
bool RustRecords::load(const std::string& book) { return xp_bookmarks_test_load(owner, bytes(book)); }
bool RustRecords::save(const std::string& book) const { return xp_bookmarks_test_save(owner, bytes(book)); }
std::string RustRecords::path(const std::string& book) {
  std::string result;
  xp_bookmarks_test_path(
      bytes(book), [](void* context, Bytes value) { *static_cast<std::string*>(context) = string(value); }, &result);
  return result;
}
