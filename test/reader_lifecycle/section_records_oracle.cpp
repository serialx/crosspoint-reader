#include <xp_epub_session.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "physical_defaults.h"

extern "C" uint8_t xp_epub_test_section_records(const XpAppResources*, uint8_t, uint32_t, uint32_t, size_t (*)[5]);
struct Allocation {
  size_t bytes;
  bool internal;
};
static std::unordered_map<void*, Allocation> live;
static size_t attempts, failAt = SIZE_MAX, peak, requested, defaultAttempts, internalAttempts;
static size_t maxDirectory;
static bool stringPayload;
static void* allocate(size_t bytes, bool internal) {
  const size_t attempt = attempts++;
  if (internal) {
    ++internalAttempts;
    if (!stringPayload) {
      assert(bytes == maxDirectory + sizeof(void*));
      maxDirectory = bytes;
    }
  } else {
    ++defaultAttempts;
  }
  if (attempt == failAt) return nullptr;
  void* result = std::malloc(bytes);
  assert(result);
  assert(live.emplace(result, Allocation{bytes, internal}).second);
  requested += bytes;
  peak = std::max(peak, requested);
  return result;
}
static void release(void* data, bool internal) {
  if (!data) return;
  const auto found = live.find(data);
  assert(found != live.end() && found->second.internal == internal);
  requested -= found->second.bytes;
  live.erase(found);
  std::free(data);
}
extern "C" void* heap_caps_malloc(size_t size, uint32_t caps) {
  assert(caps == ((1u << 2) | (1u << 11)));
  return allocate(size, true);
}
extern "C" void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
  assert(alignment <= alignof(std::max_align_t));
  return heap_caps_malloc(size, caps);
}
extern "C" void* heap_caps_realloc(void* data, size_t bytes, uint32_t caps) {
  // Keep both requests live: expose the worst-case directory movement peak.
  void* result = heap_caps_malloc(bytes, caps);
  if (result && data) {
    std::memcpy(result, data, std::min(bytes, live.at(data).bytes));
    release(data, true);
  }
  return result;
}
extern "C" void heap_caps_free(void* data) { release(data, true); }
extern "C" void xp_chapter_release_block(void*) { std::abort(); }
extern "C" void* xp_assets_platform_alloc(size_t, uint32_t) { std::abort(); }
extern "C" void* xp_assets_platform_realloc(void*, size_t, uint32_t) { std::abort(); }
extern "C" void xp_assets_platform_free(void*) { std::abort(); }
namespace HalMemory {
struct Unexpected {
  uint8_t* release() { std::abort(); }
};
inline Unexpected allocatePsram(size_t) { std::abort(); }
struct PsramDeleter {
  void operator()(uint8_t*) { std::abort(); }
};
}  // namespace HalMemory
static void* defaultAllocate(size_t bytes) { return allocate(bytes, false); }
static void defaultRelease(void* data) { release(data, false); }
#define malloc defaultAllocate
#define free defaultRelease
static const XpAppResources resources = [] {
  XpAppResources value = unexpectedAppResources();
  // ACTUAL_ALLOCATOR_LEAVES
  return value;
}();
#undef malloc
#undef free

static uint8_t run(bool anchors, uint32_t count, uint32_t textLength, size_t fault = SIZE_MAX) {
  assert(live.empty() && requested == 0);
  attempts = peak = defaultAttempts = internalAttempts = maxDirectory = 0;
  failAt = fault;
  stringPayload = textLength != 0 || count == 0;
  size_t output[5];
  uint8_t result = xp_epub_test_section_records(&resources, anchors, count, textLength, &output);
  assert(live.empty() && requested == 0);
  if (result && count) {
    assert(output[0] == count && output[1] == (count + (anchors ? 31 : 63)) / (anchors ? 32 : 64));
    assert(output[1] == output[2]);
    assert(output[3] == 12 && output[4] == 4 * sizeof(void*));
    if (textLength == 0) {
      const size_t chunks = output[1], chunkBytes = anchors ? output[4] * 32 : 12 * 64;
      const size_t before = chunks - 1;
      const size_t expected =
          std::max(before * chunkBytes + (before + chunks) * sizeof(void*), chunks * (chunkBytes + sizeof(void*)));
      assert(peak == expected);
    }
  }
  return result;
}
int main() {
  for (bool anchors : {false, true}) {
    for (uint32_t count : {1, 31, 32, 33, 63, 64, 65, 1024, 1025}) {
      assert(run(anchors, count, 0));
      const size_t requests = attempts;
      std::printf("%s count=%u requests=%zu default=%zu internal=%zu requested_peak=%zu\n", anchors ? "anchor" : "page",
                  count, attempts, defaultAttempts, internalAttempts, peak);
      for (size_t fault = 0; fault < requests; ++fault) assert(!run(anchors, count, 0, fault));
    }
    assert(run(anchors, 0, 0));
    const size_t requests = attempts;
    assert(requests == 4);  // Repeated unconsumed reservation does not allocate twice.
    for (size_t fault = 0; fault < requests; ++fault) run(anchors, 0, 0, fault);
  }
  for (uint32_t length : {1, 31, 257}) {
    assert(run(true, 1025, length));
    const size_t requests = attempts;
    std::printf("anchor text_length=%u requests=%zu requested_peak=%zu\n", length, requests, peak);
    for (size_t fault = 0; fault < requests; ++fault) assert(!run(true, 1025, length, fault));
  }
  std::puts(
      "Every allocation failure cleaned both domains; anchor Text moved without copying; all record values retained.");
}
