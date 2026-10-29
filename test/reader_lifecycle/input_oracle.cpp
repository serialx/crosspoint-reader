
#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "ChapterPosition.h"

enum class HomeButtonAction : uint8_t { Home, Ignore, NextPage };
struct CrossPointSettings {
  enum { OFF = 0, CHAPTER_SKIP = 1, ORIENTATION_CHANGE = 2 };
  enum SHORT_PWRBTN { PAGE_TURN = 2 };
  enum { TAP_AND_SWIPE = 0, TAP_ONLY = 1, SWIPE_ONLY = 2, INVERTED_TAP = 3 };
  enum { READER_MENU_OFF = 0, READER_MENU_TAP = 1, READER_MENU_SWIPE_UP = 2 };
  uint8_t longPressButtonBehavior = 0, shortPwrBtn = 0, pageTurnGesture = 0, previousPageGesture = 0,
          showReaderMenu = 0;
  bool tiltPageTurn = false, touchReaderControls = false, backShortToFileBrowser = false;
} SETTINGS;
struct {
  bool next = false, prev = false;
  bool wasTiltedForward() const {
    std::printf("tilt-next %d\n", next);
    return next;
  }
  bool wasTiltedBack() const {
    std::printf("tilt-prev %d\n", prev);
    return prev;
  }
} halTiltSensor;
struct {
  unsigned long lastTouchHeldMs() const {
    std::puts("touch-held");
    return 731;
  }
} gpio;
struct GfxRenderer {
  int getScreenWidth() const { return 90; }
  int getScreenHeight() const { return 120; }
};
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
  enum class SwipeDir { None, Up, Down, Left, Right };
  uint32_t pressed = 0, released = 0, held = 0;
  mutable uint32_t fired = 0, suppressed = 0;
  mutable bool touchOverride = false;
  unsigned long heldMs = 0;
  bool touch = false, swapped = false, tapped = false, backGesture = false, menuGesture = false, menuSwipeUp = false;
  int x = 0, y = 0;
  HomeButtonAction home = HomeButtonAction::Ignore;
  SwipeDir swipe = SwipeDir::None;
  bool wasPressed(Button b) const {
    bool v = pressed & (1u << int(b));
    std::printf("press %d %d\n", int(b), v);
    return v;
  }
  bool wasReleased(Button b) const {
    bool v = (released & ~suppressed) & (1u << int(b));
    std::printf("release %d %d\n", int(b), v);
    return v;
  }
  bool wasLongPressed(Button b, unsigned long threshold) const {
    uint32_t bit = 1u << int(b);
    bool v = (held & bit) && !(fired & bit) && getHeldTime() >= threshold;
    if (v) {
      fired |= bit;
      suppressed |= bit;
    }
    std::printf("long %d %lu %d\n", int(b), threshold, v);
    return v;
  }
  unsigned long getHeldTime() const {
    std::printf("held %lu\n", touchOverride ? 731 : heldMs);
    return touchOverride ? 731 : heldMs;
  }
  bool isNavDirectionSwapped() const {
    std::printf("swap %d\n", swapped);
    return swapped;
  }
  HomeButtonAction homeButtonAction() const {
    std::printf("home-action %d\n", int(home));
    return home;
  }
  bool hasTouch() const {
    std::printf("touch %d\n", touch);
    return touch;
  }
  SwipeDir wasSwipe() const {
    std::printf("swipe %d\n", int(swipe));
    return swipe;
  }
  bool wasScreenTapped(int& ox, int& oy) const {
    std::printf("tap %d\n", tapped);
    if (tapped) {
      ox = x;
      oy = y;
      touchOverride = true;
    }
    return tapped;
  }
  bool wasBackGesture() const {
    std::printf("back-gesture %d\n", backGesture);
    if (backGesture) touchOverride = true;
    return backGesture;
  }
  bool wasMenuGesture() const {
    std::printf("menu-gesture %d\n", menuGesture);
    return menuGesture;
  }
  bool wasReaderMenuSwipeUp() const {
    std::printf("menu-up %d\n", menuSwipeUp);
    return menuSwipeUp;
  }
};
struct ActivityManager {
  void goToFileBrowser(const char* p) { std::printf("browser %s\n", p); }
} manager;
namespace ReaderUtils {
[[maybe_unused]] constexpr unsigned long SKIP_HOLD_MS = 700, GO_BACK_OR_HOME_MS = 1000;
struct PageTurnResult {
  bool prev, next, fromTilt;
};
struct TouchPageTurn {
  bool prev, next;
  unsigned long heldMs;
};
struct BackNavCallback {
  void* ctx;
  void (*fn)(void*);
};
}  // namespace ReaderUtils
#include "input_methods.inc"
static void buttons() {
  using B = MappedInputManager::Button;
  constexpr B buttons[] = {B::Left, B::Right, B::PageBack, B::PageForward, B::Power};
  for (int behavior = 0; behavior < 3; ++behavior)
    for (int mask = 0; mask < 32; ++mask)
      for (int swap = 0; swap < 2; ++swap)
        for (int tilt = 0; tilt < 5; ++tilt)
          for (int home = 0; home < 2; ++home)
            for (int power = 0; power < 11; ++power)
              for (int time : {699, 700, 701}) {
                SETTINGS.longPressButtonBehavior = behavior;
                SETTINGS.shortPwrBtn = power;
                SETTINGS.tiltPageTurn = tilt != 4;
                halTiltSensor.prev = tilt & 1;
                halTiltSensor.next = tilt & 2;
                MappedInputManager input;
                input.swapped = swap;
                input.home = home ? HomeButtonAction::NextPage : HomeButtonAction::Ignore;
                input.heldMs = time;
                for (int b = 0; b < 5; ++b)
                  if (mask & (1 << b)) input.pressed |= 1u << int(buttons[b]);
                input.held = input.released = input.pressed;
                std::printf("buttons %d %d %d %d %d %d %d\n", behavior, mask, swap, tilt, home, power, time);
                for (int call = 0; call < 2; ++call) {
                  auto r = ReaderUtils::detectPageTurn(input);
                  std::printf("turn %d %d %d %u %u\n", r.prev, r.next, r.fromTilt, input.fired, input.suppressed);
                }
              }
}
static void touch() {
  GfxRenderer renderer;
  for (int enabled = 0; enabled < 3; ++enabled)
    for (int next = 0; next < 5; ++next)
      for (int prev = 0; prev < 5; ++prev)
        for (int swipe = 0; swipe < 5; ++swipe)
          for (int rtl = 0; rtl < 2; ++rtl)
            for (int menu = 0; menu < 3; ++menu)
              for (int point = 0; point < 6; ++point) {
                SETTINGS.touchReaderControls = enabled != 0;
                SETTINGS.pageTurnGesture = next;
                SETTINGS.previousPageGesture = prev;
                SETTINGS.showReaderMenu = menu;
                MappedInputManager input;
                input.touch = enabled == 2;
                input.swipe = static_cast<MappedInputManager::SwipeDir>(swipe);
                input.tapped = point != 0;
                input.x = point == 1 ? 29 : point == 2 ? 30 : point == 3 ? 59 : point == 4 ? 60 : 89;
                input.y = point == 1 ? 39 : point == 5 ? 80 : 40;
                input.menuGesture = point == 1;
                input.menuSwipeUp = point == 2;
                std::printf("touch-case %d %d %d %d %d %d %d\n", enabled, next, prev, swipe, rtl, menu, point);
                auto r = ReaderUtils::detectTouchPageTurn(renderer, input, rtl);
                std::printf("touch-turn %d %d %lu %d\n", r.prev, r.next, r.heldMs, input.touchOverride);
                const bool open = ReaderUtils::isTouchMenuGesture(renderer, input);
                std::printf("menu-result %d %d\n", open, input.touchOverride);
              }
}
static void back() {
  using B = MappedInputManager::Button;
  for (bool gesture : {false, true})
    for (bool setting : {false, true})
      for (int mode = 0; mode < 4; ++mode)
        for (int held : {999, 1000, 1001}) {
          SETTINGS.backShortToFileBrowser = setting;
          MappedInputManager input;
          input.backGesture = gesture;
          input.heldMs = held;
          input.held = mode & 1 ? 1u << int(B::Back) : 0;
          input.released = mode & 2 ? 1u << int(B::Back) : 0;
          std::printf("back-case %d %d %d %d\n", gesture, setting, mode, held);
          const bool handled = ReaderUtils::handleBackNavigation(input, manager, "/book.epub",
                                                                 {nullptr, [](void*) { std::puts("go-home"); }});
          std::printf("back-result %d %u %u %d\n", handled, input.fired, input.suppressed, input.touchOverride);
        }
}
int main() {
  buttons();
  touch();
  back();
  for (const char* tag : {"he", "iw", "ar", "fa", "ur", "HE", "iW", "Ar", "fA", "He-iL", "IW_IL", "AR-sa", "fa_IR",
                          "heb", "hebrew", "en", "h", ""})
    std::printf("language %s %d\n", tag, ReaderUtils::isRtlBookLanguage(tag));
  for (int value = 0; value < 256; ++value)
    std::printf("gesture-setting %d %d %d\n", value, ReaderUtils::gestureAllowsTap(value),
                ReaderUtils::gestureAllowsSwipe(value));
  for (int page : {-1, 0, 10, 48})
    for (int total : {-1, 0, 48}) {
      ChapterPosition p{page, total};
      std::printf("chapter %d %d %d %d %08x\n", page, total, p.displayPage(), p.hasTotal(),
                  std::bit_cast<uint32_t>(p.chapterFraction()));
    }
}
