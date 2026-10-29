#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
#include <unordered_map>
static std::unordered_map<void*, size_t> liveSizes;
extern "C" size_t xp_test_live_bytes() {
  size_t total = 0;
  for (const auto& entry : liveSizes) total += entry.second;
  return total;
}
#endif

#ifdef XP_FFI_HOST_FAULT_INJECTION
#ifdef XP_PARA_LINK_FAULT_INJECTION
static size_t failArrayBytes = 0;
static size_t arrayFailures = 0;
extern "C" void xp_test_fail_array_bytes(size_t bytes) {
  failArrayBytes = bytes;
  arrayFailures = 0;
}
extern "C" size_t xp_test_array_failures() { return arrayFailures; }
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (failArrayBytes && size == failArrayBytes) {
    ++arrayFailures;
    return nullptr;
  }
  return ::operator new[](size);
}
#endif
static size_t liveAllocations = 0;
static size_t allocationAttempts = 0;
extern "C" size_t xp_test_allocation_attempts() { return allocationAttempts; }
extern "C" size_t xp_test_live_allocations() { return liveAllocations; }
static size_t allocationsLeft = SIZE_MAX;
extern "C" void xp_test_fail_allocation_after(size_t count) {
  allocationsLeft = count;
  allocationAttempts = 0;
}
static bool failAllocation() {
  ++allocationAttempts;
  if (allocationsLeft == SIZE_MAX) return false;
  if (allocationsLeft == 0) return true;
  --allocationsLeft;
  return false;
}
#elif defined(XP_FFI_HOST_CSS_FAULT_INJECTION)
static thread_local size_t failAfter = SIZE_MAX;
static thread_local size_t attempts = 0;
extern "C" void cssTestFailAllocation(size_t index) {
  failAfter = index;
  attempts = 0;
}
extern "C" size_t cssTestAllocationAttempts() { return attempts; }
static bool failAllocation() {
  const bool result = attempts == failAfter;
  ++attempts;
  return result;
}
#else
static bool failAllocation() { return false; }
#endif

static void* recordAllocation(void* ptr, size_t size) {
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
  if (ptr) liveSizes[ptr] = size;
#else
  (void)size;
#endif
#ifdef XP_FFI_HOST_FAULT_INJECTION
  if (ptr) ++liveAllocations;
#endif
  return ptr;
}

extern "C" void* heap_caps_malloc(size_t size, uint32_t) {
  return failAllocation() ? nullptr : recordAllocation(std::malloc(size), size);
}
extern "C" void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
  if (failAllocation()) return nullptr;
  void* result = nullptr;
  return posix_memalign(&result, alignment, size) == 0 ? recordAllocation(result, size) : nullptr;
}
extern "C" void* heap_caps_realloc(void* ptr, size_t size, uint32_t) {
  if (failAllocation()) return nullptr;
  const auto address = reinterpret_cast<uintptr_t>(ptr);
  void* result = std::realloc(ptr, size);
  if (!result) return nullptr;
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
  if (address) liveSizes.erase(reinterpret_cast<void*>(address));
  liveSizes[result] = size;
#endif
#ifdef XP_FFI_HOST_FAULT_INJECTION
  if (!address) ++liveAllocations;
#else
  (void)address;
#endif
  return result;
}
extern "C" void heap_caps_free(void* ptr) {
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
  if (ptr) liveSizes.erase(ptr);
#endif
#ifdef XP_FFI_HOST_FAULT_INJECTION
  if (ptr) --liveAllocations;
#endif
  std::free(ptr);
}

// Isolated ABI suites link the whole Rust archive without the C++ page sink.
// Chapter suites override this with the real destructor; accidental use fails.
extern "C" __attribute__((weak)) void xp_chapter_release_block(void*) { std::abort(); }

// Asset DEFAULT allocations use the host heap while preserving fault accounting.
extern "C" void* xp_assets_platform_alloc(size_t bytes, uint32_t caps) {
  return caps == 3 ? heap_caps_malloc(bytes, 0) : nullptr;
}
extern "C" void* xp_assets_platform_realloc(void* data, size_t bytes, uint32_t caps) {
  return caps == 3 ? heap_caps_realloc(data, bytes, 0) : nullptr;
}
extern "C" void xp_assets_platform_free(void* data) { heap_caps_free(data); }
