#pragma once
#include <cstdlib>

#include "Trace.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
inline void* heap_caps_malloc(size_t size, int caps) { return trace::allocateCaps(size, caps); }
