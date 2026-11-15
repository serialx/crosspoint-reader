#pragma once
#include <cstddef>
struct HalMemory {
  struct HeapStats {
    size_t freeBytes, largestBlockBytes, totalBytes;
  };
  static HeapStats getDefaultHeap() { return {200000, 100000, 300000}; }
  static HeapStats getPsramHeap() { return {0, 0, 0}; }
};
