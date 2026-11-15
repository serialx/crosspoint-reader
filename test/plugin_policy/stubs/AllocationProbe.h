#pragma once
#include <cstddef>
namespace allocation_probe {
inline size_t live = 0, peak = 0, toastLive = 0, large = 0, toastLarge = 0;
inline bool measuring = false;
inline void beginToast() {
  peak = live;
  measuring = true;
}
inline void endToast() {
  toastLive = live;
  toastLarge = large;
  measuring = false;
}
}  // namespace allocation_probe
