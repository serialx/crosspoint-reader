#pragma once

#include <cstddef>

struct EspStub {
  std::size_t getMaxAllocHeap() const { return 1000000; }
};

inline EspStub ESP;
