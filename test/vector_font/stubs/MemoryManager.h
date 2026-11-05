#pragma once
#include <esp_heap_caps.h>

#include <cstdio>
inline void (*vectorEvict)() = nullptr;
inline bool vectorRecover = false;
namespace freeink {
class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager m;
    return m;
  }
  bool ensureFree(size_t bytes) {
    std::printf("evict %zu\n", bytes);
    if (vectorEvict) vectorEvict();
    if (vectorRecover) vectorLargest = 200 * 1024;
    return vectorRecover;
  }
};
}  // namespace freeink
