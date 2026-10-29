#pragma once
#include <xp_ui.h>

#include "Activity.h"

// The production adapter's lock/dispatch protocol is tested in ui_runtime_adapter.
// This harness retains real Rust models while stubbing only platform sampling.
class RustActivityAdapter : public Activity {
 protected:
  class ModelLock {
    inline static bool held = false;

   public:
    explicit ModelLock(RustActivityAdapter&) {
      RenderLock::require(!held, "recursive model lock");
      held = true;
    }
    ~ModelLock() { held = false; }
    static void requireHeld() { RenderLock::require(held, "selection without model lock"); }
  };
  int& checkedModelSelection(int& value) {
    ModelLock::requireHeld();
    return value;
  }
  virtual XpUiInterest rustInterest(uint32_t) const { return {}; }
  virtual XpUiEffect updateRust(const XpUiInput&) { return {}; }
  virtual void applyRustEffect(const XpUiEffect&) {}

 public:
  using Activity::Activity;
  void loop() override {
    XpUiInput input{};
    const MappedInputManager::Button buttons[] = {
        MappedInputManager::Button::Back,        MappedInputManager::Button::Confirm,
        MappedInputManager::Button::Power,       MappedInputManager::Button::ScreenLeft,
        MappedInputManager::Button::ScreenRight, MappedInputManager::Button::ScreenUp,
        MappedInputManager::Button::ScreenDown};
    const uint32_t bits[] = {1, 2, 1 << 8, 1 << 11, 1 << 12, 1 << 13, 1 << 14};
    for (unsigned i = 0; i < 7; ++i) {
      if (mappedInput.wasPressed(buttons[i])) input.pressed |= bits[i];
      if (mappedInput.wasReleased(buttons[i])) input.released |= bits[i];
    }
    input.held_ms = mappedInput.getHeldTime();
    input.now_ms = millis();
    XpUiEffect effect{};
    {
      ModelLock lock(*this);
      if (!xp_ui_input_due(&rustInterestFor(input.now_ms), &input)) return;
      effect = updateRust(input);
    }
    if (effect.invalidate) requestUpdate();
    applyRustEffect(effect);
  }

 private:
  XpUiInterest interest{};
  const XpUiInterest& rustInterestFor(uint32_t now) {
    interest = rustInterest(now);
    return interest;
  }
};
