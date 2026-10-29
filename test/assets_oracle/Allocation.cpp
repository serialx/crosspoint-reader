#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
static size_t attempts = 0, live = 0;
struct Allocation {
  void* pointer;
  size_t size;
};
static Allocation tracked[64]{};
static size_t liveBytes = 0, peakBytes = 0, largest = 0;
static void record(void* pointer, size_t size) {
  if (!pointer) return;
  for (auto& entry : tracked)
    if (!entry.pointer) {
      entry = {pointer, size};
      liveBytes += size;
      if (liveBytes > peakBytes) peakBytes = liveBytes;
      if (size > largest) largest = size;
      return;
    }
  std::abort();
}
static void forget(void* pointer) {
  if (!pointer) return;
  for (auto& entry : tracked)
    if (entry.pointer == pointer) {
      liveBytes -= entry.size;
      entry = {};
      return;
    }
}
void operator delete(void* pointer) noexcept {
  forget(pointer);
  std::free(pointer);
}
void operator delete[](void* pointer) noexcept {
  forget(pointer);
  std::free(pointer);
}
static bool fail() {
  const char* setting = std::getenv("ASSET_FAIL_ALLOC");
  const size_t index = attempts++;
  return setting && index == std::strtoull(setting, nullptr, 10);
}
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  if (fail()) return nullptr;
  void* p = ::operator new(size);
  record(p, size);
  return p;
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (fail()) return nullptr;
  void* p = ::operator new[](size);
  record(p, size);
  return p;
}
extern "C" void* heap_caps_malloc(size_t size, uint32_t) {
  if (fail()) return nullptr;
  void* p = std::malloc(size);
  if (p) ++live;
  record(p, size);
  return p;
}
extern "C" void* heap_caps_aligned_alloc(size_t align, size_t size, uint32_t) {
  if (fail()) return nullptr;
  void* p = nullptr;
  if (posix_memalign(&p, align, size)) return nullptr;
  ++live;
  record(p, size);
  return p;
}
extern "C" void* heap_caps_realloc(void* p, size_t size, uint32_t) {
  if (fail()) return nullptr;
  const bool had = p;
  size_t oldSize = 0;
  for (const auto& entry : tracked)
    if (entry.pointer == p) oldSize = entry.size;
  forget(p);
  void* r = std::realloc(p, size);
  if (r && !had) ++live;
  if (r)
    record(r, size);
  else
    record(p, oldSize);
  return r;
}
extern "C" void heap_caps_free(void* p) {
  if (p) --live;
  forget(p);
  std::free(p);
}
extern "C" void xp_chapter_release_block(void*) { std::abort(); }
struct Report {
  ~Report() {
    if (std::getenv("ASSET_RESOURCES"))
      std::fprintf(stderr, "RESOURCE peak=%zu largest=%zu retained=%zu\n", peakBytes, largest, liveBytes);
    std::fprintf(stderr, "ALLOC %zu %zu\n", attempts, live);
  }
};
static Report report;
extern "C" size_t asset_allocation_attempts() { return attempts; }
