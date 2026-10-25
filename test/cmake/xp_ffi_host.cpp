#include <cstddef>
#include <cstdint>
#include <cstdlib>

#ifdef XP_FFI_HOST_FAULT_INJECTION
static size_t liveAllocations = 0;
extern "C" size_t xp_test_live_allocations() { return liveAllocations; }
static size_t allocationsLeft = SIZE_MAX;
extern "C" void xp_test_fail_allocation_after(size_t count) { allocationsLeft = count; }
static bool failAllocation() {
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

static void* recordAllocation(void* ptr) {
#ifdef XP_FFI_HOST_FAULT_INJECTION
  if (ptr) ++liveAllocations;
#endif
  return ptr;
}

extern "C" void* heap_caps_malloc(size_t size, uint32_t) {
  return failAllocation() ? nullptr : recordAllocation(std::malloc(size));
}
extern "C" void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
  if (failAllocation()) return nullptr;
  void* result = nullptr;
  return posix_memalign(&result, alignment, size) == 0 ? recordAllocation(result) : nullptr;
}
extern "C" void heap_caps_free(void* ptr) {
#ifdef XP_FFI_HOST_FAULT_INJECTION
  if (ptr) --liveAllocations;
#endif
  std::free(ptr);
}
