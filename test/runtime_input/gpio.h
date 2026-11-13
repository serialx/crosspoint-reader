#pragma once
#include <xp_hal.h>

#include <cstdint>
#include <string>
#include <vector>
extern std::vector<std::string> trace;
struct HalGPIO {
  struct Sdk {
    void update() { trace.push_back("sdk"); }
    uint8_t capacitivePageButtonMask() {
      trace.push_back("pages");
      return 2;
    }
  } inputMgr;
  XpHalInput state{};
  bool pressed = false, released = false, held = false, home = false;
  uint8_t button = 0;
  void update();
  XpHalInputPoll pollRaw();
  bool isUsbConnected() {
    trace.push_back("usb");
    return true;
  }
  bool wasAnyPressed() const { return pressed; }
  bool wasAnyReleased() const { return released; }
  bool wasPressed(uint8_t b) const { return b == button && pressed; }
  bool wasReleased(uint8_t b) const { return b == button && released; }
  bool isPressed(uint8_t b) const { return b == button && held; }
  bool isCapacitivePagePressed(uint8_t) const { return false; }
  unsigned long getHeldTime() const { return 900; }
  bool wasCapacitivePagePressed() const { return state.page_pressed; }
  uint32_t lastTouchHeldMs() const { return 42; }
  bool hasHomeKey() const { return home; }
};
