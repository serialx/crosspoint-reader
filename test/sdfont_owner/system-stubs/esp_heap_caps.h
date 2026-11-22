#pragma once
#include <cstddef>
namespace systemtest {
size_t largest();
}
struct HeapFixture {
  size_t getMaxAllocHeap() const { return systemtest::largest(); }
  size_t getFreeHeap() const { return 200 * 1024; }
};
inline HeapFixture ESP;
