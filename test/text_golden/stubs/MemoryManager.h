#pragma once
#include <cstddef>
#include <functional>
namespace freeink {
class MemoryManager {
 public:
  struct Sink {
    const char* name;
    int priority;
    std::function<size_t(size_t)> release;
  };
  static MemoryManager& instance() {
    static MemoryManager m;
    return m;
  }
  void registerSink(Sink) {}
  size_t freeBytes() const { return 200 * 1024; }
  bool ensureFree(size_t) { return true; }
};
}  // namespace freeink
