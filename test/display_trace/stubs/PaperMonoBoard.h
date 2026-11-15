#pragma once
#include "Trace.h"
namespace freeink::papermono {
inline void setEpdPower(bool enabled) { trace::boardHook("papermono.power", enabled); }
inline void setEpdReset(bool high) { trace::boardHook("papermono.reset", high); }
}  // namespace freeink::papermono
