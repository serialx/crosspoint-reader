#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {
thread_local size_t failAfter = SIZE_MAX;
thread_local size_t attempts = 0;
bool fail() {
  const bool result = attempts == failAfter;
  ++attempts;
  return result;
}
}  // namespace

extern "C" void cssTestFailAllocation(size_t index) { failAfter = index; attempts = 0; }
extern "C" size_t cssTestAllocationAttempts() { return attempts; }
extern "C" void* heap_caps_malloc(size_t size, uint32_t) { return fail() ? nullptr : std::malloc(size); }
extern "C" void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
  return fail() ? nullptr : std::aligned_alloc(alignment, size);
}
extern "C" void heap_caps_free(void* pointer) { std::free(pointer); }
