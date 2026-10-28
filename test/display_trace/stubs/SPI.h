#pragma once
#include <Arduino.h>
#define MSBFIRST 1
#define SPI_MODE0 0
struct SPISettings {
  uint32_t hz = 0;
  int order = 0, mode = 0;
  SPISettings() = default;
  SPISettings(uint32_t hz, int order, int mode) : hz(hz), order(order), mode(mode) {}
};
struct FakeSPI {
  void begin(int sclk, int miso, int mosi, int cs);
  void end();
  void beginTransaction(SPISettings settings);
  void endTransaction();
  uint8_t transfer(uint8_t value);
  void writeBytes(const uint8_t* bytes, size_t size);
  bool bus() const { return true; }
  int pinSS() const { return -1; }
};
inline FakeSPI SPI;
