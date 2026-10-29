#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

#include "xp_ui_settings.h"
#define LOG_DBG(...)
struct HalGPIO {
  bool wasReleased(uint8_t) const { return false; }
  bool isCapacitivePagePressed(uint8_t) const { return false; }
};
struct CrossPointSettings {
  enum VIBRATION_TOUCH { VIBRATION_TOUCH_PAGE };
};
struct {
  int vibration = 0, hapticIntensity = 0;
} SETTINGS;
namespace HalHaptics {
void longPress(bool, int) {}
}  // namespace HalHaptics
namespace haptic_feedback {
void touchAction(bool) {}
}  // namespace haptic_feedback
struct MappedInputManager {
  enum class Button {
    Back,
    Confirm,
    NavNext,
    NavPrevious,
    Left,
    Right,
    Up,
    Down,
    Power,
    PageBack,
    PageForward,
    ScreenLeft,
    ScreenRight,
    ScreenUp,
    ScreenDown
  };
  mutable uint16_t longPressFiredButtons = 0, suppressedReleaseButtons = 0;
  uint8_t confirmPin = 1, pin = 1;
  bool held = false, released = false, longTouch = false;
  unsigned long duration = 0;
  bool isPressed(Button b) const { return b == Button::Confirm && pin == confirmPin && held; }
  bool wasPressed(Button b) const { return isPressed(b) && duration == 0; }
  enum class SwipeDir { None, Up, Down, Left, Right };
  SwipeDir wasSwipe() const { return SwipeDir::None; }
  bool wasReleased(Button b) const { return b == Button::Confirm && pin == confirmPin && released; }
  unsigned long getHeldTime() const { return duration; }
  bool mapButton(Button b, bool (HalGPIO::*fn)(uint8_t) const) const {
    return fn == &HalGPIO::wasReleased && wasReleased(b);
  }
  bool wasLongPressed(Button, unsigned long) const;
  void suppressNextRelease(Button) const;
  bool consumeSuppressedRelease() const;
  bool wasScreenLongPress(int& x, int& y) {
    x = 10;
    y = 30;
    return longTouch;
  }
};
struct Nav {
  int selected = 0;
};
namespace fui {
struct ActionEvent {
  int value;
  bool longPress;
};
}  // namespace fui
struct Common {
  MappedInputManager mappedInput;
  int activated = 0, cleared = 0, learned = -1, clearedIndex = -1;
  bool learning = false, ready = true;
  int currentView = 0;
  void activateIndex(int i) {
    if (learning) return;
    ++activated;
    if (currentView == 3) {
      learned = i;
      learning = true;
    }
  }
  void clearMapping(int i) {
    if (!learning) {
      ++cleared;
      clearedIndex = i;
    }
  }
  void onRowLongPress(int i) {
    if (currentView == 3) clearMapping(i);
  }
  void onBackButton() {}
  int listCount() const { return 8; }
};
#if BASELINE
struct UiListActivity : Common {
  Nav nav;
  Nav& activeNav() { return nav; }
  bool handleButtons();
  void onRowAction(const fui::ActionEvent&);
  void loop() {
    if (handleButtons()) return;
    if (mappedInput.longTouch && ready && !learning) onRowAction({1, true});
  }
};
struct BleSettingsActivity : UiListActivity {
  enum class View { Menu, Scan, Paired, Mapping };
  View view = View::Menu;
  void loop();
  void setView(int v) {
    currentView = v;
    view = static_cast<View>(v);
  }
  int selection() const { return nav.selected; }
  void down() { ++nav.selected; }
};
constexpr unsigned long CLEAR_HOLD_MS = 700;
#else
struct Publication {
  bool ready = true;
  XpUiScene scene{};
};
struct ModelLock {
  template <class T>
  explicit ModelLock(T&) {}
};
uint32_t millis() { return 100; }
struct Touch {
  int16_t touchX = 10, touchY = 30;
  bool touchPressed = false, touchReleased = false, touchHeld = false;
};
Touch touchSnapshotFrom(const MappedInputManager& input) {
  Touch t;
  t.touchHeld = input.longTouch;
  return t;
}
struct RustActivityAdapter : Common {
  bool sceneFailed = false;
  bool consumeRejectedInput() { return false; }
  bool rustSceneRejected() const { return false; }
  XpUiInput sampleRustInput() const;
  virtual XpUiEffect updateRust(const XpUiInput&) = 0;
  virtual void applyRustEffect(const XpUiEffect&) = 0;
  virtual XpUiInterest rustInterest(uint32_t) const = 0;
  virtual Publication* rustPublication() = 0;
  void requestUpdate() {}
  void loop();
};
struct SettingsUiActivity : RustActivityAdapter {
  XpUiSettingsKind kind = XP_UI_SETTINGS_BLE;
  XpUiListState state{};
  XpUiSettingsHits publishedHits{}, nextHits{};
  Publication publication;
  bool longPress = false, longTouch = false, touchActivated = false;
  int touchX = -1, touchY = -1;
  std::string name;
  SettingsUiActivity() {
    xp_ui_settings_list_init(&state, 0);
    publishedHits.first_row = {0, 0, 100, 20};
    publishedHits.count = 8;
    publishedHits.uniform = 1;
    nextHits = publishedHits;
    nextHits.first_index = 4;
  }
  bool handleCustomInput() { return false; }
  bool routingReady() const { return publication.ready; }
  int selectedIndex() const { return state.selected; }
  void loop();
  Publication* rustPublication() override { return &publication; }
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
};
struct BleSettingsActivity : SettingsUiActivity {
  XpUiSettingsBle state{};
  void loop();
  void setView(int v) {
    currentView = v;
    state.view = v;
  }
  int selection() const { return selectedIndex(); }
  void down() {
    XpUiInput input{};
    input.pressed = XP_UI_NAV_NEXT;
    updateRust(input);
  }
};
#endif
// METHODS
int main() {
  for (int pin : {1, 3})
    for (int view = 0; view < 4; ++view) {
      BleSettingsActivity s;
      s.setView(view);
      s.mappedInput.pin = s.mappedInput.confirmPin = pin;
      s.mappedInput.held = true;
      for (auto ms : {0, 699, 700, 900}) {
        s.mappedInput.duration = ms;
        s.loop();
      }
      s.mappedInput.held = false;
      s.mappedInput.released = true;
      if (!s.mappedInput.consumeSuppressedRelease()) s.loop();
      std::printf("hold %d %d %d %d %d\n", pin, view, s.activated, s.cleared, s.learned);
    }
  for (bool disabled : {false, true}) {
    BleSettingsActivity s;
    s.setView(3);
    s.learning = disabled;
#if !BASELINE
    if (disabled) s.publishedHits.disabled = 2;
#endif
    s.mappedInput.longTouch = true;
    s.loop();
    s.mappedInput.longTouch = false;
    // The long gesture has no tap release; the next physical Confirm is separate.
    s.loop();
    int selected = s.selection();
    s.mappedInput.released = true;
    s.loop();
    s.mappedInput.released = false;
    s.down();
    std::printf("touch %d %d %d %d %d %d\n", disabled, s.cleared, s.clearedIndex, selected, s.learned, s.selection());
  }
}
