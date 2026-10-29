#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#define PROGMEM
inline uint32_t millis() { return 0; }
inline uint32_t micros() { return 0; }
struct HostEsp {
  size_t freeHeap = 200 * 1024;
  size_t getFreeHeap() const { return freeHeap; }
  size_t getMaxAllocHeap() const { return 200 * 1024; }
  void restart() const { std::abort(); }
};
inline HostEsp ESP;
