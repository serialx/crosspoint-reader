#pragma once
#include <cstdint>
namespace HalMemory {
struct Heap {
  uint32_t freeBytes;
  uint32_t largestBlockBytes = UINT32_MAX;
  uint32_t totalBytes = UINT32_MAX;
};
inline Heap getInternalHeap() { return {UINT32_MAX}; }
inline Heap getDefaultHeap() { return {UINT32_MAX}; }
inline Heap getPsramHeap() { return {0}; }
}  // namespace HalMemory
