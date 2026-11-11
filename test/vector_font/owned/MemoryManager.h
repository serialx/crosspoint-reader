#pragma once
#include <cstddef>
#include <cstdint>
extern "C" size_t vector_owner_pressure(uint8_t);
namespace freeink {
class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager manager;
    return manager;
  }
  bool ensureFree(size_t bytes) {
    const size_t have = vector_owner_pressure(0);
    if (have >= bytes) return true;
    size_t freed = 0;
    for (uint8_t sink = 1; sink <= 3 && freed < bytes - have; ++sink) freed += vector_owner_pressure(sink);
    return vector_owner_pressure(0) >= bytes;
  }
};
}  // namespace freeink
