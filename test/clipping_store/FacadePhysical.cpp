#include <ClippingStore.h>

#include <cstdio>

#include "clipping_store/reader_fixture.h"
#include "clipping_store/reader_storage.h"
#include "owner_tables.h"
#include "reader_lifecycle/read_folder_session_fixture.h"

extern "C" void xp_test_fail_allocation_after(size_t);
namespace {
using Store = ClippingStoreFacade;
unsigned renderDepth;
void* reader;
XpAppText wire(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
void reset() {
  Store::unload();
  fake::reset();
}
void load(const std::string& path = "/book.epub", const std::string& title = "Book") {
  assert(Store::loadForBook(path, title, "Author", "epub"));
}
uint8_t add(const std::string& text = "one", const std::string& chapter = "Chapter") {
  XpClippingRecord row{};
  row.page_count = 1;
  row.signature = 1;
  row.pending = 1;
  row.end_offset = 3;
  return xp_epub_test_clipping_insert(reader, &row, wire(chapter), {wire(text), &text});
}
Clipping row(unsigned index) {
  Clipping value;
  assert(Store::clippingAt(index, value));
  return value;
}
std::string text(unsigned index) {
  std::string value;
  assert(Store::readClippingText(index, value));
  return value;
}
bool remove(unsigned index) { return xp_epub_test_clipping_remove(reader, index); }
std::string path() {
  for (const auto& [name, node] : fake::files)
    if (name.ends_with(".bin")) return name;
  assert(false);
  return {};
}
void corruptLoad() {
  reset();
  load();
  assert(add() == 0 && add("two") == 0);
  Store::unload();
  auto& bytes = fake::files.at(path())->bytes;
  bytes.pop_back();
  const auto damaged = bytes;
  assert(!Store::loadForBook("/book.epub", "Book", "Author", "epub"));
  assert(Store::clippingCount() == 0 && add() == 2 && !Store::prepareSync());
  Store::unload();
  assert(bytes == damaged);
  load("/other.epub");
  assert(add() == 0);
  assert(!Store::loadForBook("/bad.txt", "", "", "unsupported"));
  assert(Store::clippingCount() == 0 && add() == 2);
}
void capacityAndHeader() {
  reset();
  load();
  for (unsigned i = 0; i < 255; ++i) assert(add() == 0);
  Store::unload();
  load();
  assert(add() == 0 && add() == 1);
  Store::unload();
  load();
  assert(Store::clippingCount() == 256);
  const auto original = fake::files.at(path())->bytes;
  Store::unload();
  load("/book.epub", std::string(4097, 'x'));
  assert(!remove(0));
  Store::unload();
  assert(fake::files.at(path())->bytes == original);
}
void crossSlabRollback() {
  reset();
  load();
  for (unsigned i = 0; i < 33; ++i) {
    Clipping value;
    std::snprintf(value.id, sizeof(value.id), "clip-%u", i);
    const auto contents = std::to_string(i);
    value.textLength = contents.size();
    assert(Store::applyRemote(value, contents, false));
    assert(std::string(row(0).id) == "clip-0");
  }
  const auto file = path();
  const auto original = fake::files.at(file)->bytes;
  fake::failWrite = 1;
  assert(!remove(15));
  assert(Store::clippingCount() == 33 && fake::files.at(file)->bytes == original);
  for (unsigned i = 0; i < 33; ++i) assert(text(i) == std::to_string(i));
  const auto removed = row(15);
  fake::failWrite = 0;
  assert(!Store::applyRemote(removed, "", true));
  assert(Store::clippingCount() == 33);
  assert(Store::applyRemote(removed, "", true) && remove(15));
  Store::unload();
  load();
  assert(Store::clippingCount() == 31);
  for (unsigned i = 0; i < 31; ++i) assert(text(i) == std::to_string(i < 15 ? i : i + 2));
}
void allocationFailure() {
  reset();
  load();
  assert(add() == 0);
  const auto file = path();
  const auto original = fake::files.at(file)->bytes;
  xp_test_fail_allocation_after(0);
  const auto result = add("two");
  xp_test_fail_allocation_after(SIZE_MAX);
  assert(result == 2 && Store::clippingCount() == 1 && fake::files.at(file)->bytes == original);
  assert(!fake::files.count(file + ".tmp") && add("two") == 0);
  Store::unload();
  xp_test_fail_allocation_after(0);
  const bool loaded = Store::loadForBook("/book.epub", "Book", "Author", "epub");
  xp_test_fail_allocation_after(SIZE_MAX);
  assert(!loaded && Store::clippingCount() == 0);
  load();
  assert(Store::clippingCount() == 2);
}
void journalFailures() {
  for (bool failRollback : {false, true}) {
    reset();
    load();
    assert(add() == 0 && add("two") == 0 && Store::prepareSync());
    const std::string first = row(0).id, second = row(1).id;
    assert(remove(0));
    const auto journal = path() + ".deleted";
    const auto original = fake::files.at(journal)->bytes;
    fake::shortWrite = 1;
    if (failRollback) fake::failTruncate = 0;
    assert(!remove(0) && Store::clippingCount() == 1);
    if (failRollback)
      assert(fake::files.at(journal)->bytes.size() == original.size() + 1);
    else
      assert(fake::files.at(journal)->bytes == original);
    assert(remove(0));
    Store::unload();
    load();
    uint32_t offset = 0;
    char id[65];
    assert(Store::nextDeletion(offset, id) && id == first);
    assert(Store::nextDeletion(offset, id) && id == second);
    assert(Store::nextDeletion(offset, id) && !id[0]);
    assert(fake::files.at(journal)->bytes.size() == 130 && Store::finishDeletions());
  }
  reset();
  load();
  assert(add() == 0 && Store::prepareSync());
  const std::string expected = row(0).id;
  assert(remove(0));
  const auto journal = path() + ".deleted";
  const auto original = fake::files.at(journal)->bytes;
  fake::files.at(journal)->bytes.push_back('x');
  Store::unload();
  load();
  uint32_t offset = 0;
  char id[65];
  fake::failTruncate = 0;
  assert(!Store::nextDeletion(offset, id) && offset == 0);
  assert(Store::nextDeletion(offset, id) && id == expected && fake::files.at(journal)->bytes == original);
  assert(Store::nextDeletion(offset, id) && !id[0] && Store::finishDeletions());
  fake::add(journal, "x");
  offset = 0;
  assert(Store::nextDeletion(offset, id) && !id[0] && fake::files.at(journal)->bytes.empty());
}
void utf8AndSidecars() {
  reset();
  load();
  assert(add("one", std::string(46, 'x') + "가") == 0);
  assert(std::string(row(0).chapterTitle) == std::string(46, 'x'));
  fake::failWrite = 0;
  assert(!remove(0) && Store::clippingCount() == 1 && text(0) == "one");
  Store::unload();
  const auto file = path();
  for (const char* suffix : {".deleted", ".bak", ".tmp"}) fake::add(file + suffix, "x");
  assert(Store::deleteForFilePath("/book.epub", "epub"));
  for (const char* suffix : {"", ".deleted", ".bak", ".tmp"}) assert(!fake::files.count(file + suffix));
}
}  // namespace
int main() {
  auto leaves = retirement_fixture::leavesDefaults();
  auto runtime = retirement_fixture::modelRuntime();
  auto resources = retirement_fixture::resourcesDefaults();
  runtime.render_lock = []() -> void* {
    assert(!renderDepth);
    ++renderDepth;
    return reinterpret_cast<void*>(1);
  };
  runtime.render_unlock = [](void*) {
    assert(renderDepth == 1);
    --renderDepth;
  };
  leaves.log_enabled = [](uint8_t) -> uint8_t { return 0; };
  clipping_fixture::StorageAdapter storage(leaves, resources);
  storage.quietLog = true;
  storage.io = [] { assert(!retirement_fixture::modelDepth); };
  XpAppLegacy legacy{};
  XpAppPlatform platform{&legacy, &resources, &runtime, &leaves};
  XpAppCompatibility compatibility{};
  assert(xp_app_init(&platform, &compatibility) == XP_APP_OK);
  reader = xp_epub_test_read_folder_create(&leaves, &runtime, &resources);
  assert(reader);
  corruptLoad();
  capacityAndHeader();
  crossSlabRollback();
  allocationFailure();
  journalFailures();
  utf8AndSidecars();
  Store::unload();
  xp_epub_test_read_folder_destroy(reader);
  assert(storage.files.empty() && !renderDepth && !retirement_fixture::modelDepth);
  std::puts(
      "Real App store/native facade: capacity, corrupt load, cross-slab rollback, OOM, short journal, recovery, UTF-8 "
      "and sidecars passed");
}
