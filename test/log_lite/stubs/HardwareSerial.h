#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

#include <cstdlib>
#include <string>

inline std::string serialBytes;
inline void (*onWrite)() = nullptr;
class HardwareSerial : public Print {
 public:
  void begin(unsigned long) {}
  operator bool() const { return true; }
  void print(const char* line) {
    if (interruptMask || criticalDepth) std::abort();
    serialBytes += line;
    if (onWrite) onWrite();
  }
};
inline HardwareSerial Serial;
