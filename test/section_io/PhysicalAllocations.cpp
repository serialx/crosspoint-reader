#include <cassert>
#include <cstdlib>
#include <new>

namespace {
size_t live = 0;
void* allocate(size_t bytes) {
  void* pointer = std::malloc(bytes ? bytes : 1);
  if (pointer) ++live;
  return pointer;
}
void release(void* pointer) {
  if (pointer) {
    assert(live);
    --live;
  }
  std::free(pointer);
}
}  // namespace
size_t sectionPhysicalNativeAllocations() { return live; }
void* operator new(size_t bytes) {
  void* pointer = allocate(bytes);
  if (!pointer) std::abort();
  return pointer;
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void* operator new(size_t bytes, const std::nothrow_t&) noexcept { return allocate(bytes); }
void* operator new[](size_t bytes, const std::nothrow_t&) noexcept { return allocate(bytes); }
void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, size_t) noexcept { release(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
