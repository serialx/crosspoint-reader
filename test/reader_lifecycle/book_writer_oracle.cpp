#include <BuildScratch.h>
#include <Memory.h>
#include <xp_epub_session.h>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "physical_defaults.h"

extern "C" {
using Inspect = uint8_t (*)(void*, const uint8_t*, const uint8_t*, size_t, uint8_t);
uint8_t xp_epub_test_scratch(const XpAppResources*, const XpEpubLeaves*, uint8_t, Inspect, void*);
void xp_test_fail_allocation_after(size_t);
size_t xp_test_live_allocations();
}
alignas(std::max_align_t) static uint8_t block[50000];
static int releases = 0, claims = 0, closes = 0;
static size_t nativeLive = 0, nativeFailAfter = SIZE_MAX;
static void* scratchMalloc(size_t bytes) {
  if (nativeFailAfter == 0) return nullptr;
  if (nativeFailAfter != SIZE_MAX) --nativeFailAfter;
  void* result = std::malloc(bytes);
  if (result) ++nativeLive;
  return result;
}
static void scratchFree(void* bytes) {
  if (bytes) --nativeLive;
  std::free(bytes);
}
namespace HalMemory {
struct Unexpected {
  uint8_t* release() { std::abort(); }
};
inline Unexpected allocatePsram(size_t) { std::abort(); }
struct PsramDeleter {
  void operator()(uint8_t*) { std::abort(); }
};
}  // namespace HalMemory
#define malloc scratchMalloc
#define free scratchFree
static const XpAppResources resources = [] {
  XpAppResources value = unexpectedAppResources();
  // ACTUAL_ALLOCATOR_LEAVES
  return value;
}();
#undef malloc
#undef free
static bool lent = false, expectBorrow = false, failAfterInspection = false, rejectSink = false, reclaimEarly = false;
#define HalFile HostFile
#include <HalStorage.h>
#undef HalFile
static size_t liveFiles = 0;
struct HalFile : HostFile {
  HalFile() { ++liveFiles; }
  ~HalFile() {
    --liveFiles;
    ++closes;
  }
};
struct BookMetadataCache {
  static void lockReadCursor() {}
  static void unlockReadCursor() {}
};
// ACTUAL_TEXT_HELPERS
static const XpEpubLeaves leaves = [] {
  XpEpubLeaves value = unexpectedEpubLeaves();
  value.log_enabled = [](uint8_t) -> uint8_t { return 0; };
  // ACTUAL_LEAVES
  return value;
}();

struct Row {
  XpAppText href, title, anchor;
  uint8_t level, toc;
};
extern "C" uint32_t xp_epub_test_writer(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText, const Row*,
                                        uint32_t);
int main(int argc, char** argv) {
  assert(argc == 5);
  const std::string path = argv[1], cache = argv[2], mode = argv[4];
  const size_t count = std::stoul(argv[3]);
  std::vector<std::string> hrefs, titles, anchors;
  hrefs.reserve(count);
  titles.reserve(count);
  anchors.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), i % 2 ? "OPS//X/../ch%04zu.xhtml" : "OPS/ch%04zu.xhtml", i);
    hrefs.emplace_back(name);
    titles.emplace_back("Cafe\xcc\x81 " + std::to_string(i));
    anchors.emplace_back("a" + std::to_string(i));
  }
  std::vector<Row> rows;
  for (size_t i = 0; i < count; ++i)
    rows.push_back({text(hrefs[i]), text(titles[i]), text(anchors[i]), uint8_t(i % 4), uint8_t(i % 3 == 0)});
  auto run = [&](const std::string& directory) {
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto result = xp_epub_test_writer(&resources, &leaves, text(path), text(directory), rows.data(), rows.size());
    assert(nativeLive == 0 && xp_test_live_allocations() == 0 && liveFiles == 0);
    if (result == 9) assert(!std::filesystem::exists(directory + "/book.bin"));
    return result;
  };
  if (mode == "bytes") {
    assert(run(cache) == 0);
    return 0;
  }
  assert(mode == "faults");
  // Fails a target payload after earlier targets have already been appended.
  nativeFailAfter = 16;
  assert(run(cache + "/partial-targets") == 9);
  nativeFailAfter = SIZE_MAX;
  for (size_t fail = 0; fail < 35; ++fail) {
    nativeFailAfter = fail;
    run(cache + "/native-" + std::to_string(fail));
    nativeFailAfter = SIZE_MAX;
  }
  for (size_t fail = 0; fail < 40; ++fail) {
    xp_test_fail_allocation_after(fail);
    run(cache + "/rust-" + std::to_string(fail));
    xp_test_fail_allocation_after(SIZE_MAX);
  }
  HalFileTest::writeLimit = [](const char* path, size_t, size_t count) -> size_t {
    return std::string(path).ends_with("/book.bin") ? 0 : count;
  };
  assert(run(cache + "/short-book") == 9);
  HalFileTest::writeLimit = [](const char* path, size_t, size_t count) -> size_t {
    return std::string(path).ends_with("/spine.bin.tmp") ? 0 : count;
  };
  assert(run(cache + "/short-spine") == 4);
  HalFileTest::writeLimit = nullptr;
  HalFileTest::allowOpen = [](const char* path, const char*) { return !std::string(path).ends_with("/toc.bin.tmp"); };
  assert(run(cache + "/open-toc") == 5);
  HalFileTest::allowOpen = nullptr;
}
