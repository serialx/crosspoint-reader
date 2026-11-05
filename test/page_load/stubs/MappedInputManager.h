#pragma once
class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Power, ScreenLeft, ScreenRight, ScreenUp, ScreenDown, None };
  Button pressed = Button::None, released = Button::None;
  bool tapped = false;
  int touchX = 0, touchY = 0;
  bool hasTouch() const { return true; }
  bool wasScreenTapped(int& x, int& y) const {
    x = touchX;
    y = touchY;
    return tapped;
  }
  bool wasScreenLongPress(int&, int&) const { return false; }
  bool wasPressed(Button b) const { return pressed == b; }
  bool wasReleased(Button b) const { return released == b; }
  bool isPressed(Button) const { return false; }
  unsigned long getHeldTime() const { return 0; }
  struct Labels {
    const char *btn1, *btn2, *btn3, *btn4;
  };
  Labels mapDirectionalLabels(const char* a, const char* b, const char* c, const char* d, const char*,
                              const char*) const {
    return {a, b, c, d};
  }
};
