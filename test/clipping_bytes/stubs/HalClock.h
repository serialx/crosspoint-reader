#pragma once
#include <ctime>

// Substituted clock for ClippingsManager's "Added on" line. The oracle sets the
// fields the production formatter reads; availability models a missing RTC.
class HalClock {
 public:
  bool available = true;
  struct tm local = {};
  bool localTime(struct tm& out) const;
};

inline bool HalClock::localTime(struct tm& out) const {
  if (!available) return false;
  out = local;
  return true;
}

inline HalClock halClock;
