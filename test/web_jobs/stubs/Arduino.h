#pragma once
#include "Oracle.h"
class Stream { public: virtual ~Stream() = default; virtual size_t write(const uint8_t*, size_t n) { return n; } };
inline void delay(unsigned n) { oracle::record("delay %u", n); }
inline void yield() { oracle::record("yield"); }
struct Esp { unsigned getFreeHeap() { return 123456; } unsigned getMaxAllocHeap() { return 98765; } };
inline Esp ESP;
