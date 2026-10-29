#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
namespace json_fault {
inline size_t failAt = 0, attempts = 0, live = 0, peak = 0;
struct alignas(std::max_align_t) Header {
  size_t size;
};
inline void* allocate(size_t size) {
  if (++attempts == failAt) return nullptr;
  auto* p = static_cast<Header*>(std::malloc(sizeof(Header) + size));
  if (!p) return nullptr;
  p->size = size;
  live += size;
  peak = std::max(peak, live);
  return p + 1;
}
inline void deallocate(void* p) {
  if (!p) return;
  auto* h = static_cast<Header*>(p) - 1;
  live -= h->size;
  std::free(h);
}
inline void* reallocate(void* p, size_t size) {
  if (!p) return allocate(size);
  auto* h = static_cast<Header*>(p) - 1;
  auto old = h->size;
  // ArduinoJson's StringBuilder requires shrinking reallocations to succeed.
  if (size > old && ++attempts == failAt) return nullptr;
  auto* q = static_cast<Header*>(std::realloc(h, sizeof(Header) + size));
  if (!q) return nullptr;
  q->size = size;
  live = live - old + size;
  peak = std::max(peak, live);
  return q + 1;
}
}  // namespace json_fault
