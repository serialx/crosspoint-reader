#pragma once

#include <freertos/FreeRTOS.h>

namespace loglite {
class CriticalSection {
 public:
  CriticalSection() {
#if configNUM_CORES == 1
    // Preserve an outer mask without the task critical-nesting assertion on C3.
    previousMask = portSET_INTERRUPT_MASK_FROM_ISR();
#else
    portENTER_CRITICAL(&mux);
#endif
  }
  ~CriticalSection() {
#if configNUM_CORES == 1
    portCLEAR_INTERRUPT_MASK_FROM_ISR(previousMask);
#else
    portEXIT_CRITICAL(&mux);
#endif
  }
  CriticalSection(const CriticalSection&) = delete;
  CriticalSection& operator=(const CriticalSection&) = delete;

 private:
#if configNUM_CORES == 1
  UBaseType_t previousMask;
#else
  inline static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#endif
};
}  // namespace loglite
