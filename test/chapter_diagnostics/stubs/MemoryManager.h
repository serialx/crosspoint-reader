#pragma once

// Host reclaim hook can release a forced array failure for the retry test.

#include <cstddef>

extern "C" void xp_test_fail_array_bytes(size_t);
namespace ChapterAllocation {
inline bool recover = false;
}
namespace freeink {

enum class MemPool : unsigned char { Internal, Psram, Default };

class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager inst;
    return inst;
  }
  bool ensureFree(size_t, MemPool = MemPool::Default) {
    if (ChapterAllocation::recover) xp_test_fail_array_bytes(0);
    return true;
  }

 private:
  MemoryManager() = default;
};

}  // namespace freeink
