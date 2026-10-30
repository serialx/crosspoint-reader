#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <xp_ui_reader.h>

#include <string_view>

#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"

namespace ReaderUtils {

static_assert(CrossPointSettings::OFF == 0 && CrossPointSettings::CHAPTER_SKIP == 1 &&
              CrossPointSettings::ORIENTATION_CHANGE == 2);
static_assert(CrossPointSettings::SHORT_PWRBTN::PAGE_TURN == 2);
static_assert(CrossPointSettings::TAP_AND_SWIPE == 0 && CrossPointSettings::TAP_ONLY == 1 &&
              CrossPointSettings::SWIPE_ONLY == 2 && CrossPointSettings::INVERTED_TAP == 3);
static_assert(CrossPointSettings::READER_MENU_OFF == 0 && CrossPointSettings::READER_MENU_TAP == 1 &&
              CrossPointSettings::READER_MENU_SWIPE_UP == 2);
static_assert(static_cast<uint8_t>(HomeButtonAction::NextPage) == 2);

// Cross-checked against Rust reader thresholds by native_back_threshold_matches_rust.
constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long GO_BACK_OR_HOME_MS = GO_HOME_MS;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;
constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

inline XpUiReaderInputConfig inputConfig() {
  return {SETTINGS.longPressButtonBehavior, SETTINGS.shortPwrBtn,           SETTINGS.tiltPageTurn,
          SETTINGS.touchReaderControls,     SETTINGS.pageTurnGesture,       SETTINGS.previousPageGesture,
          SETTINGS.showReaderMenu,          SETTINGS.backShortToFileBrowser};
}

inline XpUiReaderInputServices inputServices(const MappedInputManager& input) {
  return {const_cast<MappedInputManager*>(&input),
          [](void* context, uint32_t mask, uint8_t query, uint32_t threshold) -> uint8_t {
            const auto& frame = *static_cast<const MappedInputManager*>(context);
            MappedInputManager::Button button;
            switch (mask) {
              case XP_UI_BACK:
                button = MappedInputManager::Button::Back;
                break;
              case XP_UI_LEFT:
                button = MappedInputManager::Button::Left;
                break;
              case XP_UI_RIGHT:
                button = MappedInputManager::Button::Right;
                break;
              case XP_UI_POWER:
                button = MappedInputManager::Button::Power;
                break;
              case XP_UI_PAGE_BACK:
                button = MappedInputManager::Button::PageBack;
                break;
              case XP_UI_PAGE_FORWARD:
                button = MappedInputManager::Button::PageForward;
                break;
              default:
                return false;
            }
            switch (static_cast<XpUiReaderButtonQuery>(query)) {
              case XP_UI_READER_BUTTON_PRESS:
                return frame.wasPressed(button);
              case XP_UI_READER_BUTTON_RELEASE:
                return frame.wasReleased(button);
              case XP_UI_READER_BUTTON_LONG:
                return frame.wasLongPressed(button, threshold);
            }
            return false;
          },
          [](void* context, uint8_t query) -> uint32_t {
            const auto& frame = *static_cast<const MappedInputManager*>(context);
            switch (static_cast<XpUiReaderInputQuery>(query)) {
              case XP_UI_READER_INPUT_HAS_TOUCH:
                return frame.hasTouch();
              case XP_UI_READER_INPUT_SWIPE:
                switch (frame.wasSwipe()) {
                  case MappedInputManager::SwipeDir::None:
                    return XP_UI_SWIPE_NONE;
                  case MappedInputManager::SwipeDir::Left:
                    return XP_UI_SWIPE_LEFT;
                  case MappedInputManager::SwipeDir::Right:
                    return XP_UI_SWIPE_RIGHT;
                  case MappedInputManager::SwipeDir::Up:
                    return XP_UI_SWIPE_UP;
                  case MappedInputManager::SwipeDir::Down:
                    return XP_UI_SWIPE_DOWN;
                }
                return XP_UI_SWIPE_NONE;
              case XP_UI_READER_INPUT_HELD_MS:
                return frame.getHeldTime();
              case XP_UI_READER_INPUT_TOUCH_HELD_MS:
                return gpio.lastTouchHeldMs();
              case XP_UI_READER_INPUT_TILT_NEXT:
                return halTiltSensor.wasTiltedForward();
              case XP_UI_READER_INPUT_TILT_PREV:
                return halTiltSensor.wasTiltedBack();
              case XP_UI_READER_INPUT_NAV_SWAPPED:
                return frame.isNavDirectionSwapped();
              case XP_UI_READER_INPUT_HOME_ACTION:
                return static_cast<uint32_t>(frame.homeButtonAction());
              case XP_UI_READER_INPUT_BACK_GESTURE:
                return frame.wasBackGesture();
              case XP_UI_READER_INPUT_MENU_GESTURE:
                return frame.wasMenuGesture();
              case XP_UI_READER_INPUT_MENU_SWIPE_UP:
                return frame.wasReaderMenuSwipeUp();
            }
            return 0;
          },
          [](void* context, int32_t* x, int32_t* y) -> uint8_t {
            int tapX = 0, tapY = 0;
            if (!static_cast<const MappedInputManager*>(context)->wasScreenTapped(tapX, tapY)) return false;
            *x = tapX;
            *y = tapY;
            return true;
          }};
}

inline bool gestureAllowsSwipe(const uint8_t gesture) { return xp_ui_reader_gesture(gesture) & 2; }

inline bool gestureAllowsTap(const uint8_t gesture) { return xp_ui_reader_gesture(gesture) & 1; }

inline bool isRtlBookLanguage(std::string_view tag) {
  return xp_ui_reader_language_rtl(reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
}

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromTilt;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const auto services = inputServices(input);
  const auto config = inputConfig();
  const auto turn = xp_ui_reader_buttons(&services, &config);
  return {turn.prev != 0, turn.next != 0, turn.from_tilt != 0};
}

struct TouchPageTurn {
  bool prev;
  bool next;
  unsigned long heldMs;
};

inline TouchPageTurn detectTouchPageTurn(const GfxRenderer& renderer, const MappedInputManager& input,
                                         const bool rtlBook = false) {
  const auto services = inputServices(input);
  const auto config = inputConfig();
  const auto turn =
      xp_ui_reader_touch(&services, &config, renderer.getScreenWidth(), renderer.getScreenHeight(), rtlBook);
  return {turn.prev != 0, turn.next != 0, turn.held_ms};
}

// Reader menu opens on the menu edge-swipe or a center-third tap. Home-key
// actions are configured separately from screen gestures.
// Menu gestures honor showReaderMenu independently of touchReaderControls,
// which only gates page-turn touch zones in detectTouchPageTurn().
inline bool isTouchMenuGesture(const GfxRenderer& renderer, const MappedInputManager& input) {
  const auto services = inputServices(input);
  return xp_ui_reader_menu_gesture(&services, SETTINGS.showReaderMenu, renderer.getScreenWidth(),
                                   renderer.getScreenHeight());
}

// One helper, blocking or deferred: the async form starts the refresh and
// returns so the caller can overlap CPU work with the panel's refresh time.
// Async callers must not touch the framebuffer until
// renderer.waitRefreshComplete() and must rebuild the differential baseline
// before the next page turn (the tiled grayscale cleanup does).
inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh, bool async = false) {
  int32_t remaining = pagesUntilFullRefresh;
  const auto mode = xp_ui_reader_refresh(&remaining, 0) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  if (async) {
    renderer.displayBufferAsync(mode);
  } else {
    renderer.displayBuffer(mode);
  }
  remaining = pagesUntilFullRefresh;
  if (xp_ui_reader_refresh(&remaining, 0)) remaining = SETTINGS.getRefreshFrequency();
  pagesUntilFullRefresh = remaining;
}

// Display the B/W base of a page whose grayscale pass follows. Panels that
// combine the base (Paper Mono) defer the activation so base + gray planes go
// out as one waveform — displaying the base separately makes the gray pass
// re-drive the whole text body (a visible flash). Other panels display
// normally. Same refresh-cadence bookkeeping as displayWithRefreshCycle.
inline void displayBaseWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (renderer.grayscaleCapabilities().base != HalDisplay::GrayscaleBase::Combined) {
    displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
    return;
  }
  int32_t remaining = pagesUntilFullRefresh;
  const auto mode = xp_ui_reader_refresh(&remaining, 0) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  renderer.displayGrayscaleBase(mode);
  remaining = pagesUntilFullRefresh;
  if (xp_ui_reader_refresh(&remaining, 0)) remaining = SETTINGS.getRefreshFrequency();
  pagesUntilFullRefresh = remaining;
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    // A combined-base panel may still hold a deferred B/W activation; flush it
    // so the page reaches the panel even without its grays.
    if (renderer.grayscaleCapabilities().base == HalDisplay::GrayscaleBase::Combined)
      renderer.cleanupGrayscaleWithFrameBuffer();
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
}

struct BackNavCallback {
  void* ctx;
  void (*fn)(void*);
};

// Returns true if the back button was consumed (caller should return).
// Long press (>= GO_BACK_OR_HOME_MS):
// - default: go to file browser
// - with backShortToFileBrowser: go home
// Short press (< GO_BACK_OR_HOME_MS):
// - default: go home
// - with backShortToFileBrowser: go to file browser.
inline bool handleBackNavigation(const MappedInputManager& mappedInput, ActivityManager& activityManager,
                                 const char* filePath, BackNavCallback goHome) {
  const auto services = inputServices(mappedInput);
  switch (xp_ui_reader_back(&services, SETTINGS.backShortToFileBrowser)) {
    case 1:
      activityManager.goToFileBrowser(filePath);
      return true;
    case 2:
      goHome.fn(goHome.ctx);
      return true;
    default:
      return false;
  }
}

}  // namespace ReaderUtils
