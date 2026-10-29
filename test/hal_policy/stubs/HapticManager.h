#pragma once
#include <Arduino.h>
inline bool motorBegin = true;
struct HapticManager {
  struct Step {
    uint32_t durationMs;
    uint8_t intensity;
  };
  static HapticManager& getInstance() {
    static HapticManager m;
    return m;
  }
  void setEnabled(bool value) { printf("motor enabled %u\n", value); }
  void setIntensity(uint8_t value) { printf("motor intensity %u\n", value); }
  bool begin() {
    puts("motor begin");
    return motorBegin;
  }
  void pulse() { puts("motor pulse"); }
  void play(const Step* p, size_t n) {
    for (size_t i = 0; i < n; ++i) printf("motor step %u %u\n", p[i].durationMs, p[i].intensity);
  }
};
