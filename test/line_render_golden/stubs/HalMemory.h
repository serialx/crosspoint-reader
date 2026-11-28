#pragma once
#include <cstddef>
struct HalMemory {
  struct Heap {
    size_t freeBytes = 1 << 28, largestBlockBytes = 1 << 28;
  };
  static Heap getDefaultHeap() { return {}; }
  static Heap getInternalHeap() { return {}; }
};
