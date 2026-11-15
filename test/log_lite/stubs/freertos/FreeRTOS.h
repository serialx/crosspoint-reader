#pragma once

#include <cstdint>

using UBaseType_t = uint32_t;
inline UBaseType_t interruptMask = 0;
inline unsigned criticalDepth = 0;
inline unsigned restoreCount = 0;
inline UBaseType_t setInterruptMask() {
  const auto previous = interruptMask;
  interruptMask = 1;
  return previous;
}
inline void restoreInterruptMask(UBaseType_t previous) {
  interruptMask = previous;
  ++restoreCount;
}
#define portSET_INTERRUPT_MASK_FROM_ISR() setInterruptMask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(previous) restoreInterruptMask(previous)
using portMUX_TYPE = unsigned;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) (++*(mux), ++criticalDepth)
#define portEXIT_CRITICAL(mux) (--*(mux), --criticalDepth)
