#pragma once
#include <cstdlib>

#include "Trace.h"
inline bool displayTraceAllocations = false;
inline int displayFailAllocation = 0;
inline int displayAllocationCount = 0;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
inline void* heap_caps_malloc(size_t size, int caps) {
  const bool failed = ++displayAllocationCount == displayFailAllocation || displayFailAllocation < 0;
  if (displayTraceAllocations)
    trace::event("alloc size=" + std::to_string(size) + " caps=" + std::to_string(caps) +
                 " failed=" + std::to_string(failed));
  return failed ? nullptr : trace::allocateCaps(size, caps);
}
