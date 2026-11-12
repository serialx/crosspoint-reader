#pragma once
#include <cstddef>
#include <cstdint>
namespace protection_test {
inline size_t largest = 100000;
}
struct EspTest {
  uint32_t getFreeHeap() const { return 200000; }
  uint32_t getMaxAllocHeap() const { return 100000; }
};
inline EspTest ESP;
