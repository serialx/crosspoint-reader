#include <xp_ui.h>

#include <cassert>
#include <string>
#include <vector>

static std::vector<std::string> events;
static uint32_t now;
static uint32_t millis() {
  events.push_back("clock");
  return now;
}
class MappedInputManager {
 public:
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
  enum class SwipeDir { None, Up, Down, Left, Right };
  uint32_t pressed{}, released{}, held{}, heldMs{};
  uint8_t touchFlags{};
  SwipeDir swipe{};
  bool wasPressed(Button button) const {
    events.push_back("press:" + std::to_string(static_cast<unsigned>(button)));
    return pressed & (1u << static_cast<unsigned>(button));
  }
  bool wasReleased(Button button) const {
    events.push_back("release:" + std::to_string(static_cast<unsigned>(button)));
    return released & (1u << static_cast<unsigned>(button));
  }
  bool isPressed(Button button) const {
    events.push_back("held:" + std::to_string(static_cast<unsigned>(button)));
    return held & (1u << static_cast<unsigned>(button));
  }
  uint32_t getHeldTime() const {
    events.push_back("duration");
    return heldMs;
  }
  SwipeDir wasSwipe() const {
    events.push_back("swipe");
    return swipe;
  }
};
struct TouchSnapshot {
  int16_t touchX, touchY;
  bool touchPressed, touchReleased, touchHeld;
};
static TouchSnapshot touchSnapshotFrom(const MappedInputManager& input) {
  events.push_back("touch");
  return {-17, 311, bool(input.touchFlags & 1), bool(input.touchFlags & 2), bool(input.touchFlags & 4)};
}
#include "components/RustInputSnapshot.h"

int main() {
  std::vector<std::string> expected;
  for (unsigned i = 0; i < 15; ++i) {
    for (const char* kind : {"press:", "release:", "held:"}) expected.push_back(kind + std::to_string(i));
  }
  expected.insert(expected.end(), {"clock", "duration", "touch", "swipe"});
  for (unsigned bit = 0; bit < 15; ++bit) {
    for (uint8_t flags = 0; flags < 8; ++flags) {
      for (auto swipe :
           {MappedInputManager::SwipeDir::None, MappedInputManager::SwipeDir::Up, MappedInputManager::SwipeDir::Down,
            MappedInputManager::SwipeDir::Left, MappedInputManager::SwipeDir::Right}) {
        MappedInputManager input;
        input.pressed = 1u << bit;
        input.released = 1u << ((bit + 5) % 15);
        input.held = 1u << ((bit + 10) % 15);
        input.heldMs = 1511;
        input.touchFlags = flags;
        input.swipe = swipe;
        now = 0xfffffff1;
        events.clear();
        const auto snapshot = rustInputSnapshot(input);
        assert(events == expected && "physical snapshot probe order");
        assert(snapshot.pressed == input.pressed && snapshot.released == input.released && snapshot.held == input.held);
        assert(snapshot.now_ms == now && snapshot.held_ms == input.heldMs);
        assert(snapshot.touch_x == -17 && snapshot.touch_y == 311 && snapshot.touch_flags == flags);
        assert(snapshot.swipe == static_cast<int8_t>(swipe));
        assert(!snapshot.action && !snapshot.value && !snapshot.reserved);
      }
    }
  }
}
