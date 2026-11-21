#include <xp_ui_reader.h>

#include <cassert>

#include "MappedInputManager.h"
std::vector<std::string> trace;
class GfxRenderer {};
unsigned homeReads = 0;
HomeButtonAction action = HomeButtonAction::Ignore;
struct {
  HomeButtonAction homeButtonAction() {
    ++homeReads;
    return action;
  }
} activityManager;
struct CrossPointSettings {
  enum { VIBRATION_OFF, VIBRATION_TOUCH_PAGE };
  int vibration = VIBRATION_TOUCH_PAGE, hapticIntensity = 1;
} SETTINGS;
struct HalHaptics {
  static void feedback(bool enabled, bool pressed, int intensity) {
    assert(enabled && pressed && intensity == 1);
    trace.push_back("haptic");
  }
  static void longPress(bool, int) { trace.push_back("long-haptic"); }
};
constexpr unsigned long TOUCH_HELD_OVERRIDE_WINDOW_MS = 250;
unsigned long millis() { return 1000; }
enum { XP_APP_INPUT_POLLED };
struct XpAppEvent {
  int kind;
  struct {
    bool defer_home = false;
  } value{};
};
void xp_app_event(const XpAppEvent* event) { trace.push_back(event->value.defer_home ? "defer" : "event"); }
extern "C" void xp_hal_input_update(XpHalInput* state, uint8_t buttons, bool usb) {
  trace.push_back("edges");
  state->page_pressed = buttons & ~state->page_buttons;
  state->page_buttons = buttons;
  state->usb_changed = usb != state->usb_connected;
  state->usb_connected = usb;
}
bool MappedInputManager::mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  return (gpio.*fn)(static_cast<uint8_t>(button));
}
bool MappedInputManager::wasBackGesture() const { return false; }
bool MappedInputManager::wasBottomEdgeUpSwipe() const { return false; }
bool MappedInputManager::hasTouch() const { return true; }
bool MappedInputManager::isNavDirectionSwapped() const { return false; }
bool MappedInputManager::wasMenuGesture() const { return false; }
bool MappedInputManager::wasReaderMenuSwipeUp() const { return false; }
bool MappedInputManager::wasScreenTapped(int& x, int& y) const {
  x = 11;
  y = 22;
  return true;
}
MappedInputManager::SwipeDir MappedInputManager::wasSwipe(int*, int*) const { return SwipeDir::None; }
HalGPIO gpio;
struct {
  XpHalTilt state{};
  XpHalTiltContext context() { return {&state, {}}; }
  bool wasTiltedForward() {
    assert(false);
    return false;
  }
  bool wasTiltedBack() {
    assert(false);
    return false;
  }
} halTiltSensor;
// READER SOURCES
// PRODUCTION
int main() {
  GfxRenderer renderer;
  MappedInputManager mapped(gpio, renderer);
  using B = MappedInputManager::Button;
  using H = HomeButtonAction;
  auto sample = mapped.pollRaw();
  assert(sample.state == &gpio.state && sample.buttons == 2 && sample.usb);
  assert((trace == std::vector<std::string>{"sdk", "pages", "usb"}));
  assert(!gpio.state.page_pressed);  // Raw polling has not executed Rust edge policy.
  xp_hal_input_update(sample.state, sample.buttons, sample.usb);
  mapped.pollFeedback();
  trace.push_back("rust-input-polled");
  mapped.pollFinish();
  assert(gpio.heldReads == 0);  // No fired long press needs a held-state query.
  assert((trace == std::vector<std::string>{"sdk", "pages", "usb", "edges", "haptic", "rust-input-polled"}));
  trace.clear();
  gpio.state = {};
  mapped.update(true);
  assert((trace == std::vector<std::string>{"sdk", "pages", "usb", "edges", "haptic", "defer"}));
  assert(mapped.wasPressed(B::Confirm, H::Confirm));
  assert(mapped.wasReleased(B::Confirm, H::Confirm));
  assert(!mapped.wasPressed(B::Confirm, H::Ignore));
  assert(mapped.getHeldTime(H::Ignore) == 900 && mapped.getHeldTime(H::Confirm) == 0);
  gpio.home = true;
  assert(mapped.wasHomeGesture(H::Home) && !mapped.wasHomeGesture(H::Ignore));
  const auto sources = ReaderUtils::inputSources(mapped);
  XpEpubInputQuery query{sources.context, nullptr, static_cast<uint8_t>(H::Confirm)};
  assert(sources.button(&query, XP_UI_CONFIRM, XP_UI_READER_BUTTON_PRESS, 0));
  assert(sources.read(&query, XP_UI_READER_INPUT_HELD_MS) == 0);
  assert(sources.read(&query, XP_UI_READER_INPUT_TILT_NEXT) == 0 && query.tilt == &halTiltSensor.state);
  int32_t x = 0, y = 0;
  assert(sources.tap(&query, &x, &y) && x == 11 && y == 22);
  assert(homeReads == 0);  // Every explicit query is independent of the Rust compatibility getter.
  action = H::Confirm;
  assert(mapped.wasPressed(B::Confirm));
  assert(homeReads == 1);
  assert(mapped.injectRemoteClick(RemoteButton::Confirm));
  assert(mapped.wasReleased(B::Confirm));
  assert(mapped.getHeldTime() == 0);
  assert(homeReads == 1);  // Remote short circuit still wins before Home lookup.
  mapped.pollRaw();
  gpio.button = static_cast<uint8_t>(B::Confirm);
  gpio.held = true;
  assert(!mapped.wasLongPressed(B::Confirm, 901, H::Ignore));
  assert(!mapped.wasLongPressed(B::Confirm, 800, H::Confirm));
  assert(mapped.wasLongPressed(B::Confirm, 900, H::Ignore));
  assert(!mapped.wasLongPressed(B::Confirm, 900, H::Ignore));
  gpio.held = false;
  gpio.released = true;
  mapped.pollFinish();
  assert(mapped.consumeSuppressedRelease());
  assert(!mapped.consumeSuppressedRelease());
  gpio.released = false;
  gpio.held = true;
  assert(mapped.wasLongPressed(B::Confirm, 900, H::Ignore));
  gpio.heldReads = 0;
  mapped.pollFinish();
  assert(gpio.heldReads == 1);
  assert(!mapped.wasLongPressed(B::Confirm, 900, H::Ignore));
  gpio.held = false;
  mapped.pollFinish();
  gpio.heldReads = 0;
  mapped.pollFinish();
  assert(gpio.heldReads == 0);
  // Multiple fired bits are independent, including the final logical button.
  for (uint8_t value = 0; value <= static_cast<uint8_t>(B::ScreenDown); ++value) {
    gpio.button = value;
    gpio.held = true;
    assert(mapped.wasLongPressed(static_cast<B>(value), 900, H::Ignore));
  }
  gpio.heldReads = 0;
  mapped.pollFinish();
  assert(gpio.heldReads == static_cast<unsigned>(B::ScreenDown) + 1);
  gpio.heldReads = 0;
  mapped.pollFinish();
  assert(gpio.heldReads == 1);
  assert(!mapped.wasLongPressed(B::ScreenDown, 900, H::Ignore));
  gpio.held = false;
  gpio.released = true;
  mapped.pollFinish();
  assert(mapped.consumeSuppressedRelease());
  gpio.released = false;
  gpio.held = true;
  assert(mapped.wasLongPressed(B::ScreenDown, 900, H::Ignore));
}
