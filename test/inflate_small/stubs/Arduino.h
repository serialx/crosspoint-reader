#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
inline uint32_t millis() { return 0; }
inline uint32_t micros() { return 0; }
struct EspStub {
  size_t getMaxAllocHeap() const { return 1000000; }
};
inline EspStub ESP;
