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
// ACTUAL_CSTR_HELPER
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
static const XpAppResources resources = [] {
  XpAppResources value = unexpectedAppResources();
  // ACTUAL_ALLOCATOR_LEAVES
  return value;
}();
#undef malloc
#undef free
static bool lent = false, expectBorrow = false, failAfterInspection = false, rejectSink = false, reclaimEarly = false;
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

extern "C" uint32_t xp_epub_test_package(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText, size_t,
                                         uint8_t);
int main(int argc, char** argv) {
  assert(argc == 5);
  const std::string path = argv[1], cache = argv[2];
  std::filesystem::create_directories(cache);
  const uint32_t result = xp_epub_test_package(&resources, &leaves, text(path), text(cache), std::stoul(argv[3]),
                                               std::string(argv[4]) == "ncx");
  assert(nativeLive == 0 && xp_test_live_allocations() == 0 && liveFiles == 0);
  return result;
}
