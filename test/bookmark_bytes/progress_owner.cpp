#include <Epub/BookMetadataCache.h>
#include <Epub/css/CssParser.h>
#include <Epub/parsers/PackageParser.h>
#include <Txt.h>
#include <Utf8.h>

#include <optional>
// COMMON_ITEM_COMPOSITION
// ACTUAL_LOAD_METHODS
extern "C" {
uint8_t xp_epub_test_book_progress(const XpAppResources*, const XpEpubLeaves*, const XpAppRuntime*, XpAppText,
                                   XpAppText, uint8_t, void (*)(XpAppText, float, int32_t, int32_t));
size_t xp_test_live_allocations();
}
static bool locked;
static unsigned locks, unlocks, results;
static void observe(XpAppText path, float percentage, int32_t spine, int32_t page) {
  assert(!locked && locks == unlocks && path.len && spine == 0 && page >= 0 && page < 3);
  assert(percentage == 0.5f);
  const std::string value(reinterpret_cast<const char*>(path.data), path.len);
  assert(value.starts_with("/body/DocFragment[1]/body"));
  printf("progress %s %.3f %d %d\n", value.c_str(), percentage, spine, page);
  ++results;
}
int main(int argc, char** argv) {
  assert(argc == 3);
  static const XpAppRuntime runtime = [] {
    auto value = unexpectedAppRuntime();
    value.now_ms = []() -> uint32_t { return 0; };
    value.render_lock = []() -> void* {
      assert(!locked);
      locked = true;
      ++locks;
      return &locked;
    };
    value.render_unlock = [](void* token) {
      assert(token == &locked && locked);
      locked = false;
      ++unlocks;
    };
    return value;
  }();
  for (unsigned visit = 0; visit < 3; ++visit) {
    for (uint8_t mode = 0; mode < 3; ++mode) {
      assert(xp_epub_test_book_progress(&resources, &leaves, &runtime, text(argv[1]), text(argv[2]), mode, observe));
      assert(xp_test_live_allocations() == 0 && nativeBuffers.empty() && !locked);
    }
  }
  assert(results == 9 && locks == 9 && unlocks == 9);
  puts("progress owner: 9 visits, balanced resource guards and zero retained allocations");
}
