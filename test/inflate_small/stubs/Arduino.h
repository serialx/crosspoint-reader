#pragma once
#include <cstddef>
struct EspStub {
  size_t getMaxAllocHeap() const { return 1000000; }
};
inline EspStub ESP;
