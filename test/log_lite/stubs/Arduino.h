#pragma once

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define RTC_NOINIT_ATTR
inline unsigned long clockMs = 42;
inline unsigned long millis() { return clockMs; }
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) { return 1; }
  virtual size_t write(const uint8_t*, size_t size) { return size; }
  virtual void flush() {}
};
inline size_t loopStackBytes = 16384;
inline size_t getArduinoLoopTaskStackSize() { return loopStackBytes; }
