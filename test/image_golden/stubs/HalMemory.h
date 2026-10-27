#pragma once
#include <cstdint>
namespace HalMemory {
struct Heap {
  uint32_t freeBytes;
};
inline Heap getInternalHeap() { return {UINT32_MAX}; }
inline Heap getDefaultHeap() { return {UINT32_MAX}; }
inline Heap getPsramHeap() { return {0}; }
}  // namespace HalMemory
