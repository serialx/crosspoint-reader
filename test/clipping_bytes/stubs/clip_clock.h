#pragma once
// Forced include for ClippingStore.cpp only. The store reads the wall clock in
// addClipping() through time(); the fixture substitutes that clock so the
// persisted timestamp is reproducible. Standard headers are pulled in before
// the rename so only the store's own call is affected.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <string>

namespace clip_test_clock {
extern time_t now;
}

inline time_t clipTestWallClock(time_t*) { return clip_test_clock::now; }
#define time clipTestWallClock
