#include <cstdlib>
#include <new>

// The library nothrow wrappers catch throwing new; avoid its unwinder on OOM.
void* operator new(const std::size_t size, const std::nothrow_t&) noexcept { return std::malloc(size == 0 ? 1 : size); }

void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
  return std::malloc(size == 0 ? 1 : size);
}
