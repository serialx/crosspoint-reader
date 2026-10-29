#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <new>

// Host-only requested sizes. Signed events preserve live multisets and peaks.
namespace allocations {
inline bool enabled = false;
inline std::array<int64_t, 4096> events{};
inline std::array<void*, 256> pointers{};
inline std::array<size_t, 256> sizes{};
inline size_t count = 0, live = 0, peak = 0;
inline void begin() {
  assert(live == 0);
  count = peak = 0;
  enabled = true;
}
inline void allocated(void* pointer, size_t bytes) {
  if (!enabled) return;
  for (size_t i = 0; i < pointers.size(); ++i) {
    if (pointers[i]) continue;
    pointers[i] = pointer;
    sizes[i] = bytes;
    assert(count < events.size());
    events[count++] = static_cast<int64_t>(bytes);
    live += bytes;
    if (live > peak) peak = live;
    return;
  }
  std::abort();
}
inline void released(void* pointer) {
  if (!enabled || !pointer) return;
  for (size_t i = 0; i < pointers.size(); ++i) {
    if (pointers[i] != pointer) continue;
    assert(count < events.size());
    events[count++] = -static_cast<int64_t>(sizes[i]);
    live -= sizes[i];
    pointers[i] = nullptr;
    return;
  }
}
struct Pause {
  bool previous = enabled;
  Pause() { enabled = false; }
  ~Pause() { enabled = previous; }
};
}  // namespace allocations
void* operator new(size_t n) {
  void* pointer = std::malloc(n ? n : 1);
  if (!pointer) std::abort();
  allocations::allocated(pointer, n);
  return pointer;
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* pointer) noexcept {
  allocations::released(pointer);
  std::free(pointer);
}
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, size_t) noexcept { ::operator delete(pointer); }
