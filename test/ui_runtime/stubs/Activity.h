#pragma once
#include <xp_ui.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#define LOG_ERR(...) ((void)0)
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED \
  {                                  \
  }
#define taskENTER_CRITICAL(m) (m)->lock()
#define taskEXIT_CRITICAL(m) (m)->unlock()
inline uint32_t testNow = 0;
inline uint32_t millis() { return testNow; }
inline std::mutex renderMutex;
class Activity;
class RenderLock {
 public:
  RenderLock() { renderMutex.lock(); }
  explicit RenderLock(Activity&) : RenderLock() {}
  ~RenderLock() { renderMutex.unlock(); }
};
class MappedInputManager {
 public:
  enum class Button {
    Back,
    Confirm,
    Left,
    Right,
    Up,
    Down,
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
  uint32_t pressed = 0, released = 0, held = 0, heldTime = 0;
  uint8_t touchFlags = 0;
  int rawPressed = -1;
  bool longConfirm = false;
  int getPressedFrontButton() const { return rawPressed; }
  bool wasLongPressed(Button, uint32_t) const { return longConfirm; }
  SwipeDir swipe = SwipeDir::None;
  bool wasPressed(Button b) const { return pressed & (1u << static_cast<unsigned>(b)); }
  bool wasReleased(Button b) const { return released & (1u << static_cast<unsigned>(b)); }
  bool isPressed(Button b) const { return held & (1u << static_cast<unsigned>(b)); }
  uint32_t getHeldTime() const { return heldTime; }
  SwipeDir wasSwipe() const { return swipe; }
};
class HalDisplay {
 public:
  enum RefreshMode { FAST_REFRESH, HALF_REFRESH, FULL_REFRESH };
};
class GfxRenderer {
 public:
  XpNativeProvider textProvider() const { return {}; }
  int getLineHeight(int32_t) const { return 20; }
  bool paintOk = true;
  bool drawUiScene(const XpUiScene&, const XpUiPaint*) const { return paintOk; }
  std::atomic<bool> refreshing{false}, finishRefresh{false};
  void displayBuffer(HalDisplay::RefreshMode) {
    refreshing = true;
    while (!finishRefresh) std::this_thread::yield();
  }
};
class Activity {
 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

 public:
  Activity(const char* n, GfxRenderer& r, MappedInputManager& i) : name(n), renderer(r), mappedInput(i) {}
  virtual ~Activity() = default;
  virtual void loop() {}
  virtual void render(RenderLock&&) {}
  void requestUpdate() {}
};
