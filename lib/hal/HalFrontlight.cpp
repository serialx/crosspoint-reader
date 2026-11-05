#include "HalFrontlight.h"

#include <Logging.h>

HalFrontlight HalFrontlight::instance;

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
  // begin() runs the hardware probe (EEGO A4: an I2C ACK from the LM3630A at
  // 0x36) that decides present(), so it must come FIRST. Guarding on present()
  // before begin() would skip the probe forever and hide the light on units
  // that actually have one. begin() is inert on boards without a frontlight.
  manager.begin();
  if (!manager.present()) return;

  xp_hal_light_brightness(&state, brightness);
  manager.setColorTemperature(xp_hal_percent(warmth));
  xp_hal_light_on(&state, on);
  manager.setBrightness(xp_hal_light_output(&state));
  LOG_INF("LIGHT", "Frontlight up: %u%% warm=%u%% %s", state.brightness, manager.colorTemperature(),
          state.lit ? "on" : "off");
}

void HalFrontlight::setBrightness(const uint8_t percent) {
  if (xp_hal_light_brightness(&state, percent)) manager.setBrightness(state.brightness);
}

void HalFrontlight::setWarmth(const uint8_t warmPercent) { manager.setColorTemperature(xp_hal_percent(warmPercent)); }

void HalFrontlight::setOn(const bool on) {
  if (xp_hal_light_on(&state, on)) manager.setBrightness(xp_hal_light_output(&state));
}
