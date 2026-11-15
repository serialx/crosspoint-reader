// Frozen input method from 386e3c45; deterministic platform leaves.

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <utility>
struct ActivityResult {
  bool isCancelled = false;
};
struct RenderLock {};
struct MappedInputManager {
  enum class Button { Back, Confirm, Power, ScreenLeft, ScreenRight, ScreenUp, ScreenDown };
  uint32_t pressed = 0, released = 0, held = 0, heldMs = 0;
  bool wasPressed(Button b) const { return pressed & (1u << int(b)); }
  bool wasReleased(Button b) const { return released & (1u << int(b)); }
  bool isPressed(Button b) const { return held & (1u << int(b)); }
  uint32_t getHeldTime() const { return heldMs; }
};
static uint32_t timeMs;
static unsigned long millis() { return timeMs; }
constexpr unsigned long WORD_REPEAT_START_MS = 500;
constexpr unsigned long WORD_REPEAT_INTERVAL_MS = 500;
class EpubReaderFootnoteSelectActivity {
 public:
  MappedInputManager mappedInput;
  int footnoteLinkCount = 3, selected = 0;
  unsigned long lastHorizontalMoveTime = 0;
  int effect = 0, repaint = 0;
  void loop();
  void requestUpdate() { ++repaint; }
  void setResult(ActivityResult r) { effect = r.isCancelled ? 1 : 2; }
  void finish() {}
  void performJump() { effect = 2; }
};

void EpubReaderFootnoteSelectActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  RenderLock lock;
  if ((mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
       mappedInput.wasReleased(MappedInputManager::Button::Power)) &&
      footnoteLinkCount != 0) {
    performJump();
    return;
  }

  if (footnoteLinkCount == 0) return;

  const bool hasNext = selected + 1 < static_cast<int>(footnoteLinkCount);
  const unsigned long now = millis();
  const bool repeat =
      mappedInput.getHeldTime() >= WORD_REPEAT_START_MS && now - lastHorizontalMoveTime >= WORD_REPEAT_INTERVAL_MS;
  const bool moveLeft = mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft) ||
                        (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenLeft));
  const bool moveRight = mappedInput.wasPressed(MappedInputManager::Button::ScreenRight) ||
                         (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenRight));
  if ((moveLeft || mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) && selected > 0) {
    selected--;
    if (moveLeft) lastHorizontalMoveTime = now;
    requestUpdate();
  } else if ((moveRight || mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) && hasNext) {
    selected++;
    if (moveRight) lastHorizontalMoveTime = now;
    requestUpdate();
  }
}

int main() {
  for (int count : {0, 1, 3, 16}) {
    for (int selected : {0, 1, 2, 15}) {
      if (selected >= count && count) continue;
      for (int key = 0; key < 7; key++) {
        for (int edge = 0; edge < 3; edge++) {
          for (unsigned held : {0u, 499u, 500u, 501u, 1000u}) {
            EpubReaderFootnoteSelectActivity a;
            a.footnoteLinkCount = count;
            a.selected = selected;
            a.lastHorizontalMoveTime = 500;
            timeMs = 500 + held;
            a.mappedInput.heldMs = held;
            if (edge == 0) a.mappedInput.pressed = 1u << key;
            if (edge == 1) a.mappedInput.released = 1u << key;
            if (edge == 2) a.mappedInput.held = 1u << key;
            a.loop();
            std::printf("%d %d %d %d %u: %d %d %d %lu\n", count, selected, key, edge, held, a.selected, a.effect,
                        a.repaint, a.lastHorizontalMoveTime);
          }
        }
      }
    }
  }
  // Highlight geometry from drawHighlightWithSnapshot at 386e3c45.
  for (const int x : {-10, 0, 1, 12}) {
    for (const int y : {-10, 0, 1, 12}) {
      for (const int size : {0, 1, 12}) {
        int hx = x - 2;
        int hy = y - 2;
        int hw = size + 4;
        int hh = size + 4;
        if (hx < 0) {
          hw += hx;
          hx = 0;
        }
        if (hy < 0) {
          hh += hy;
          hy = 0;
        }
        std::printf("highlight %d %d %d: %d %d %d %d %d %d\n", x, y, size, hx, hy, hw, hh, x, y - 1);
      }
    }
  }
}
