#pragma once
#include "Trace.h"
namespace freeink::metalio {
inline void ensureBooted() { trace::event("metalio.ensureBooted"); }
}  // namespace freeink::metalio
