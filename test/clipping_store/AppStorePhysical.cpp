#include <cstdio>

#include "clipping_store/reader_fixture.h"
#include "clipping_store/reader_storage.h"
#include "owner_tables.h"
#include "reader_lifecycle/read_folder_session_fixture.h"

extern "C" void xp_app_test_clipping_capacities(size_t*);
namespace {
unsigned renderDepth;
XpAppText wire(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
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
  void* owner = xp_epub_test_read_folder_create(&leaves, &runtime, &resources);
  assert(owner);
  fake::reset();
  const std::string overlong = "/" + std::string(4096, 'p') + ".epub";
  const std::string maximum = "/" + std::string(4090, 'p') + ".epub";
  const std::string selection(4096, 's');
  XpClippingRecord row{};
  row.page_count = 1;
  row.id[0] = 'a';
  assert(overlong.size() == 4102 && maximum.size() == 4096);
  assert(xp_clippings_p18_load(wire(overlong), wire("Fixture"), wire("Author"), wire("epub")));
  assert(xp_epub_test_clipping_insert(owner, &row, wire("Chapter"), {wire(selection), &selection}) == 2);
  assert(xp_clippings_p18_count() == 0 && fake::files.empty() && storage.files.empty());
  xp_clippings_p18_unload();
  assert(fake::files.empty() && storage.files.empty());
  assert(xp_clippings_p18_load(wire(maximum), wire("Fixture"), wire("Author"), wire("epub")));
  assert(xp_epub_test_clipping_insert(owner, &row, wire("Chapter"), {wire(selection), &selection}) == 0);
  assert(xp_clippings_p18_count() == 1 && storage.files.empty());
  size_t before[4], after[4];
  xp_app_test_clipping_capacities(before);
  xp_clippings_p18_unload();
  xp_app_test_clipping_capacities(after);
  assert(xp_clippings_p18_count() == 0);
  for (unsigned i = 0; i < 4; ++i) assert(after[i] == before[i] && after[i] != 0);
  xp_epub_test_read_folder_destroy(owner);
  assert(!renderDepth && !retirement_fixture::modelDepth && storage.files.empty());
  std::printf(
      "App store: 4102-byte path rejected without files; 4096-byte path/selection persisted; retained "
      "book/title/author/path capacities=%zu/%zu/%zu/%zu\n",
      after[0], after[1], after[2], after[3]);
}
