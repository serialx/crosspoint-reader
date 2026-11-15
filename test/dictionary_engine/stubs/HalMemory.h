#pragma once
#include <Arduino.h>
struct HalMemory {
  struct Heap {
    size_t freeBytes, largestBlockBytes;
  };
  static Heap getDefaultHeap() { return {heapFree, heapMax}; }
};
