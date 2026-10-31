#include "Allocations.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <new>
namespace linealloc {
namespace {
struct Entry {
  void* pointer;
  size_t size;
};
Entry entries[32768]{};
size_t used = 0;
bool enabled = false;
Stats stats{};
void remove(void* p) {
  for (size_t i = 0; i < used; ++i)
    if (entries[i].pointer == p) {
      stats.live -= entries[i].size;
      entries[i] = entries[--used];
      return;
    }
}
void* record(void* p, size_t n) {
  if (enabled && p) {
    if (used == std::size(entries)) std::abort();
    entries[used++] = {p, n};
    ++stats.requests;
    stats.live += n;
    stats.peak = std::max(stats.peak, stats.live);
    stats.largest = std::max(stats.largest, n);
  }
  return p;
}
}  // namespace
void begin() {
  used = 0;
  stats = {};
  enabled = true;
}
Stats end() {
  enabled = false;
  return stats;
}
void* allocate(size_t n) { return record(std::malloc(n ? n : 1), n); }
void release(void* p) {
  if (!p) return;
  remove(p);
  std::free(p);
}
void* resize(void* p, size_t n) {
  void* result = std::realloc(p, n);
  if (result) {
    remove(p);
    record(result, n);
  }
  return result;
}
int aligned(void** p, size_t alignment, size_t n) {
  int result = posix_memalign(p, alignment, n);
  if (!result) record(*p, n);
  return result;
}
}  // namespace linealloc
void* operator new(size_t n) {
  void* p = linealloc::allocate(n);
  if (!p) std::abort();
  return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return linealloc::allocate(n); }
void* operator new[](size_t n, const std::nothrow_t& tag) noexcept { return ::operator new(n, tag); }
void operator delete(void* p) noexcept { linealloc::release(p); }
void operator delete[](void* p) noexcept { linealloc::release(p); }
void operator delete(void* p, size_t) noexcept { linealloc::release(p); }
void operator delete[](void* p, size_t) noexcept { linealloc::release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { linealloc::release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { linealloc::release(p); }
