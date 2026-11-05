#pragma once

#include <cstddef>

namespace allocation_fault {
inline bool armed = false;
inline size_t attempts = 0;
struct PlatformScope {
  bool saved = armed;
  PlatformScope() { armed = false; }
  ~PlatformScope() { armed = saved; }
};
inline bool fail() {
  if (!armed) return false;
  ++attempts;
  return true;
}
}  // namespace allocation_fault
