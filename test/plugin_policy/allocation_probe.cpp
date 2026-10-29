#include <algorithm>
#include <cstdlib>
#include <new>

#include "stubs/AllocationProbe.h"
namespace {
struct alignas(std::max_align_t) Header {
  size_t size;
};
}  // namespace
void* operator new(size_t size) {
  auto* h = static_cast<Header*>(std::malloc(sizeof(Header) + size));
  if (!h) throw std::bad_alloc();
  h->size = size;
  if (size >= 8174) ++allocation_probe::large;
  allocation_probe::live += size;
  if (allocation_probe::measuring) allocation_probe::peak = std::max(allocation_probe::peak, allocation_probe::live);
  return h + 1;
}
void operator delete(void* p) noexcept {
  if (!p) return;
  auto* h = static_cast<Header*>(p) - 1;
  allocation_probe::live -= h->size;
  if (h->size >= 8174) --allocation_probe::large;
  std::free(h);
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
