#include <FreeInkUICore.h>
namespace fui = freeink::ui;
struct CrossPointSettings {
  enum { PREV_NEXT, NEXT_PREV, PREV_PREV, NEXT_NEXT, SIDE_BUTTONS_DISABLED };
  int sideButtonLayout = PREV_NEXT, focusReadingEnabled = 0;
  uint8_t frontButtonBack = 0, frontButtonConfirm = 1, frontButtonLeft = 2, frontButtonRight = 3;
  bool frontButtonFollowOrientation = true;
} SETTINGS;
struct HalGPIO {
  // PHYSICAL_BUTTON_INDICES
  unsigned pressed = 0, pressEdges = 0, released = 0, held = 0;
  bool swipePresent = false;
  float sx = .5f, sy = .5f, ex = .5f, ey = .5f;
  bool wasPressed(uint8_t button) const { return pressEdges & (1u << button); }
  bool wasReleased(uint8_t button) const { return released & (1u << button); }
  bool isPressed(uint8_t button) const { return pressed & (1u << button); }
  bool hasTouch() const { return true; }
  bool wasSwipe(float& xs, float& ys, float& xe, float& ye) const {
    xs = sx;
    ys = sy;
    xe = ex;
    ye = ey;
    return swipePresent;
  }
};
struct GfxRenderer {
  // PHYSICAL_ORIENTATION
  Orientation orientation = Portrait;
  int panelWidth = 800, panelHeight = 600;
  Orientation getOrientation() const { return orientation; }
  void tapToLogical(float, float, int&, int&) const;
};
enum class HomeButtonAction { Ignore, Confirm };
struct MappedInputManager {
  // PHYSICAL_BUTTON_ENUM
  // PHYSICAL_SWIPE_ENUM
  enum class RemoteButton { None, Confirm, Back, Next, Previous };
  struct {
    RemoteButton button = RemoteButton::None;
  } remoteInput;
  HalGPIO gpio;
  GfxRenderer renderer;
  HomeButtonAction homeButtonAction() const { return HomeButtonAction::Ignore; }
  bool wasBackGesture() const { return false; }
  bool mapButton(Button, bool (HalGPIO::*)(uint8_t) const) const;
  Button mapScreenDirection(Button) const;
  bool isNavDirectionSwapped() const;
  bool isPressed(Button) const;
  bool wasPressed(Button) const;
  bool wasReleased(Button) const;
  bool wasLongPressed(Button, unsigned) const { std::abort(); }
  unsigned getHeldTime() const { return gpio.held; }
  bool decodeSwipe(int&, int&, int&, int&) const;
  SwipeDir wasSwipe(int* = nullptr, int* = nullptr) const;
};
// PHYSICAL_METHODS
