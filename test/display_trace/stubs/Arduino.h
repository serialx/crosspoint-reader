#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define CHANGE 3
#define PROGMEM
#define IRAM_ATTR
#define DRAM_ATTR
#define pgm_read_byte(p) (*(p))
unsigned long millis();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void pinMode(int pin, int mode);
void digitalWrite(int pin, int level);
int digitalRead(int pin);
inline int digitalPinToInterrupt(int pin) { return pin; }
void attachInterrupt(int pin, void (*callback)(), int mode);
void detachInterrupt(int pin);
#include <cstdio>

#include "Trace.h"
inline bool displayTraceDiagnostics = false;
struct FakeSerial {
  explicit operator bool() const { return false; }
  template <class... T>
  void printf(const char* format, T... values) const {
    if (!displayTraceDiagnostics) return;
    char line[512];
    std::snprintf(line, sizeof(line), format, values...);
    std::string text(line);
    if (!text.empty() && text.back() == '\n') text.pop_back();
    trace::event("diagnostic " + text);
  }
};
inline FakeSerial Serial;
using HardwareSerial = FakeSerial;
inline HardwareSerial Serial0;
