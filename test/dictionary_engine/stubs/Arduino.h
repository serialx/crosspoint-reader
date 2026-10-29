#pragma once
#include <cstdint>
#include <cstddef>
inline uint32_t millis() { return 0; }
inline size_t heapFree = 1000000, heapMax = 1000000;
struct EspStub {
  size_t getFreeHeap() const { return heapFree; }
  size_t getMaxAllocHeap() const { return heapMax; }
};
inline EspStub ESP;
