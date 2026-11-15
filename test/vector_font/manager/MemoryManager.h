#pragma once
#include <cstddef>
namespace manager_test {
bool pressure(size_t);
}
namespace freeink {
class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager m;
    return m;
  }
  bool ensureFree(size_t bytes) { return manager_test::pressure(bytes); }
};
}  // namespace freeink
