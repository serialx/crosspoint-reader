#pragma once

#include <cstdint>

// Inert capability/state facade for emulator pixels; no bus or SDK calls.
class UiOracleFrontlight {
 public:
  void configure(bool colorTemperature) {
    detected = true;
    color = colorTemperature;
    level = 60;
    temperature = color ? 40 : 0;
    lit = true;
  }
  void begin(uint8_t, uint8_t, bool) {}
  bool present() const { return detected; }
  bool hasColorTemperature() const { return color; }
  uint8_t brightness() const { return level; }
  uint8_t warmth() const { return temperature; }
  bool isOn() const { return lit; }
  void setBrightness(uint8_t value) { level = value > 100 ? 100 : value; }
  void setWarmth(uint8_t value) { temperature = color ? (value > 100 ? 100 : value) : 0; }
  void setOn(bool value) { lit = value; }

 private:
  bool detected = false;
  bool color = false;
  uint8_t level = 60;
  uint8_t temperature = 0;
  bool lit = false;
};

extern UiOracleFrontlight uiOracleFrontlight;
