// Only button and LTR touch calls enter the owner; other helpers remain shared coverage.
#include "overlay_actions_session_fixture.h"

extern "C" {
void* xp_epub_test_input_create(const XpEpubLeaves*, const XpAppRuntime*, const XpAppResources*);
void xp_epub_test_input_destroy(void*);
void xp_epub_test_input_buttons(void*, XpUiReaderTurnInput*);
void xp_epub_test_input_touch(void*, XpUiReaderTurnInput*);
}

static void* inputOwner = nullptr;
static const MappedInputManager* currentInput = nullptr;
static int inputModelDepth = 0;
static unsigned ownerButtonCalls = 0, ownerTouchCalls = 0;

static void createInputOwner() {
  static auto leaves = overlayUnexpectedLeaves();
  static auto runtime = overlayUnexpectedRuntime();
  static auto resources = overlayUnexpectedResources();
  runtime.model_lock = [] { assert(inputModelDepth == 0); ++inputModelDepth; };
  runtime.model_unlock = [] { assert(inputModelDepth == 1); --inputModelDepth; };
  leaves.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
    assert(inputModelDepth == 0 && resolveFont == 0 && currentInput);
    *out = {};
    out->long_button = SETTINGS.longPressButtonBehavior;
    out->short_power = SETTINGS.shortPwrBtn;
    out->tilt = SETTINGS.tiltPageTurn;
    out->touch_controls = SETTINGS.touchReaderControls;
    out->page_gesture = SETTINGS.pageTurnGesture;
    out->previous_gesture = SETTINGS.previousPageGesture;
    out->show_menu = SETTINGS.showReaderMenu;
    out->back_to_browser = SETTINGS.backShortToFileBrowser;
  };
  leaves.render_geometry = [](XpEpubGeometry* out) {
    assert(inputModelDepth == 0 && currentInput);
    *out = {};
    out->width = 90;
    out->height = 120;
  };
  leaves.input_services = []() -> const XpUiReaderInputServices* {
    assert(inputModelDepth == 0 && currentInput);
    static XpUiReaderInputServices physical;
    physical = ReaderUtils::inputServices(*currentInput);
    // Wrap only to assert the lock contract; all probes use the extracted bridge.
    static const XpUiReaderInputServices checked{
        &physical,
        [](void* raw, uint32_t mask, uint8_t query, uint32_t threshold) -> uint8_t {
          assert(inputModelDepth == 0 && currentInput);
          auto& service = *static_cast<XpUiReaderInputServices*>(raw);
          return service.button(service.context, mask, query, threshold);
        },
        [](void* raw, uint8_t query) -> uint32_t {
          assert(inputModelDepth == 0 && currentInput);
          auto& service = *static_cast<XpUiReaderInputServices*>(raw);
          return service.read(service.context, query);
        },
        [](void* raw, int32_t* x, int32_t* y) -> uint8_t {
          assert(inputModelDepth == 0 && currentInput);
          auto& service = *static_cast<XpUiReaderInputServices*>(raw);
          return service.tap(service.context, x, y);
        }};
    return &checked;
  };
  inputOwner = xp_epub_test_input_create(&leaves, &runtime, &resources);
  assert(inputOwner && inputModelDepth == 0);
}

static ReaderUtils::PageTurnResult ownerButtons(const MappedInputManager& input) {
  assert(inputOwner && !currentInput && inputModelDepth == 0);
  currentInput = &input;
  XpUiReaderTurnInput result{};
  xp_epub_test_input_buttons(inputOwner, &result);
  currentInput = nullptr;
  ++ownerButtonCalls;
  assert(inputModelDepth == 0 && result.reserved == 0);
  return {result.prev != 0, result.next != 0, result.from_tilt != 0};
}

static ReaderUtils::TouchPageTurn ownerTouch(const MappedInputManager& input) {
  assert(inputOwner && !currentInput && inputModelDepth == 0);
  currentInput = &input;
  XpUiReaderTurnInput result{};
  xp_epub_test_input_touch(inputOwner, &result);
  currentInput = nullptr;
  ++ownerTouchCalls;
  assert(inputModelDepth == 0 && result.reserved == 0);
  return {result.prev != 0, result.next != 0, result.held_ms};
}

static void destroyInputOwner() {
  assert(inputOwner && !currentInput && inputModelDepth == 0);
  assert(ownerButtonCalls == 126720 && ownerTouchCalls == 6750);
  xp_epub_test_input_destroy(inputOwner);
  inputOwner = nullptr;
  assert(inputModelDepth == 0);
}
