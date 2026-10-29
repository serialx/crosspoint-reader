#pragma once
#include <Arduino.h>
inline bool lightPresent = true;
struct FrontlightManager {
 uint8_t warmth = 0;
 void begin() { puts("light begin"); }
 bool present() const { puts("light present"); return lightPresent; }
 bool hasColorTemperature() const { return true; }
 void setColorTemperature(uint8_t v) { printf("warmth %u\n", v); warmth = v; }
 uint8_t colorTemperature() const { puts("warmth get"); return warmth; }
 void setBrightness(uint8_t v) { printf("brightness %u\n", v); }
};
