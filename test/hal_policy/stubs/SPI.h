#pragma once
#include <Arduino.h>
struct FakeSPI {
  void begin(int a, int b, int c, int d) { printf("spi %d %d %d %d\n", a, b, c, d); }
};
inline FakeSPI SPI;
