#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
static uint32_t clockMs;
inline uint32_t millis() { return clockMs; }
class MappedInputManager {
 public:
  enum class Button {
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back,
    Power,
    PageBack,
    PageForward,
    NavNext,
    NavPrevious,
    ScreenLeft,
    ScreenRight,
    ScreenUp,
    ScreenDown
  };
  enum class SwipeDir { None, Left, Right, Up, Down };
  enum class RowTouch : uint8_t { None, Down, Tap };
  uint32_t pressed = 0, released = 0, held = 0, duration = 0;
  int touch = 0, x = -1, y = -1;
  SwipeDir swipe = SwipeDir::None;
  bool longConfirm = false;
  bool wasLongPressed(Button b, uint32_t) const { return b == Button::Confirm && longConfirm; }
  bool wasPressed(Button b) const { return pressed & (1u << unsigned(b)); }
  bool wasReleased(Button b) const { return released & (1u << unsigned(b)); }
  bool isPressed(Button b) const { return held & (1u << unsigned(b)); }
  uint32_t getHeldTime() const { return duration; }
  SwipeDir wasSwipe() const { return swipe; }
  bool wasScreenTouchDown(int& a, int& b) const {
    a = x;
    b = y;
    return touch == 1;
  }
  bool wasScreenTapped(int& a, int& b) const {
    a = x;
    b = y;
    return touch == 2;
  }
  RowTouch rowTouch(int&, int, int, int, int = 0, int = INT32_MAX, int = 0) const;
  RowTouch colTouch(int&, int, int, int, int, int, int = 0) const;
};
