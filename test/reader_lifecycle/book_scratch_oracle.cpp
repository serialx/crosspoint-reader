#include <BuildScratch.h>
#include <Memory.h>
#include <xp_epub_session.h>

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

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
// ACTUAL_INFLATE_OOM
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
struct HalFile {
  ~HalFile() { ++closes; }
  bool close() { return true; }
  bool seek(uint32_t) { return true; }
  int read(uint8_t*, uint32_t) { return -1; }
};
struct PhysicalStorage {
  bool openFileForRead(const char*, const char*, HalFile&) { return true; }
} Storage;
struct BookMetadataCache {
  static void lockReadCursor() {}
  static void unlockReadCursor() {}
};
// ACTUAL_TEXT_HELPERS
static const XpEpubLeaves leaves = [] {
  XpEpubLeaves value = unexpectedEpubLeaves();
  // ACTUAL_LEAVES
  return value;
}();
static uint8_t inspect(void*, const uint8_t* state, const uint8_t* window, size_t size, uint8_t borrowed) {
  assert(bool(borrowed) == expectBorrow);
  assert(reinterpret_cast<uintptr_t>(state) % alignof(void*) == 0);
  if (expectBorrow) {
    assert(state == block);
    assert(size == 0 || (window > state && window + size <= block + sizeof(block)));
    assert(xp_test_live_allocations() == 0 && nativeLive == 0);
    assert(buildscratch::claim(1) == nullptr && "nested consumer cannot double claim");
  } else {
    assert(state != block && nativeLive == (size ? 2 : 1) && xp_test_live_allocations() == 0);
  }
  for (size_t i = 0; i < size; ++i) assert(window[i] == 0);
  if (failAfterInspection) xp_test_fail_allocation_after(0);
  if (reclaimEarly) buildscratch::reclaim();
  return !rejectSink;
}
static void run(uint8_t kind, uint8_t expected) {
  const int before = releases;
  assert(xp_epub_test_scratch(&resources, &leaves, kind, inspect, nullptr) == expected);
  assert(releases - before == (expectBorrow ? 1 : 0));
  assert(xp_test_live_allocations() == 0 && nativeLive == 0);
  xp_test_fail_allocation_after(SIZE_MAX);
  nativeFailAfter = SIZE_MAX;
  if (lent && expectBorrow) {
    auto* available = buildscratch::claim(1);
    assert(available == block);
    buildscratch::release(available);
  }
}
int main(int argc, char**) {
  if (argc == 2) {
    buildscratch::lend(block, sizeof(block));
    expectBorrow = true;
    reclaimEarly = true;
    run(0, 0);
    return 0;
  }
  run(0, 0);  // Absent lender: same two allocations.
  for (bool logging : {false, true}) {
    inflateLogging = logging;
    for (size_t fault : {0, 1}) {
      inflateEvents.clear();
      nativeFailAfter = fault;
      run(0, 1);
      assert(inflateEvents.size() == (logging ? 3 : 2));
      assert(inflateEvents[0] == "default:" + std::to_string(fault));
      assert(inflateEvents[1] == "psram:" + std::to_string(fault));
      if (logging) {
        const auto actual = inflateEvents[2];
        inflateEvents.clear();
        InflateStream::logAllocationFailure(fault ? "window" : "state", fault ? 32768 : xp_inflate_state_size());
        assert(inflateEvents[2] == actual);
        assert(actual.find("heap 11112 free/3334 max, PSRAM 5556 free/7778 total") != std::string::npos);
      }
    }
  }
  buildscratch::lend(block, 8);
  lent = true;
  run(0, 0);  // Undersized loan falls back.
  buildscratch::reclaim();
  buildscratch::lend(block, sizeof(block));
  auto* other = buildscratch::claim(1);
  assert(other == block);
  run(0, 0);  // Occupied by native consumer: do not release its lease.
  assert(buildscratch::claim(1) == nullptr);
  buildscratch::release(other);
  expectBorrow = true;
  std::memset(block, 0xa5, sizeof(block));
  xp_test_fail_allocation_after(0);
  run(0, 0);  // Claimed stream needs no heap storage.
  run(1, 2);  // Decoder failure.
  run(2, 2);
  assert(closes == 1);  // Actual File owner and read-failure return.
  rejectSink = true;
  run(0, 3);
  rejectSink = false;
  failAfterInspection = true;
  run(4, 1);
  failAfterInspection = false;
  run(5, 0);  // Non-streaming state uses the same loan, without window.
  buildscratch::reclaim();
  lent = false;
  expectBorrow = false;
  run(0, 0);
  assert(claims > releases);
}
