// Base mode retains button/LTR coverage; EPUB mode adds real book touch and menu input.
#include "overlay_actions_session_fixture.h"

extern "C" {
void* xp_epub_test_input_create_legacy_book(const XpEpubLeaves*, const XpAppRuntime*, const XpAppResources*, const XpEpubLegacyBookServices*);
void xp_epub_test_input_destroy(void*);
void xp_epub_test_input_buttons(void*, XpUiReaderTurnInput*);
void xp_epub_test_input_touch(void*, XpUiReaderTurnInput*);
void xp_epub_test_input_book_touch(void*, void*, XpUiReaderTurnInput*);
uint8_t xp_epub_test_input_menu(void*);
}

static void* inputOwner = nullptr;
static const MappedInputManager* currentInput = nullptr;
static int inputModelDepth = 0;
static unsigned ownerButtonCalls = 0, ownerTouchCalls = 0;
static unsigned ownerBookTouchCalls = 0, ownerMenuCalls = 0, languageCalls = 0;
static unsigned char markerBook = 0;
static bool currentRtl = false;

static void createInputOwner() {
  static auto leaves = overlayLegacyLeaves();
  static auto runtime = overlayUnexpectedRuntime();
  static auto resources = overlayUnexpectedResources();
  runtime.model_lock = [] { assert(inputModelDepth == 0); ++inputModelDepth; };
  runtime.model_unlock = [] { assert(inputModelDepth == 1); --inputModelDepth; };
  leaves.physical.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
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
  leaves.book.book_language = [](void* book, XpAppTextSink sink, void* context) -> uint8_t {
    assert(book == &markerBook && inputModelDepth == 0 && currentInput && sink);
    const char* language = currentRtl ? "ar" : "en";
    assert(language[2] == '\0');
    const uint8_t consumed = sink(context, {reinterpret_cast<const uint8_t*>(language), 2});
    assert(consumed == 1 && inputModelDepth == 0);
    ++languageCalls;
    return consumed;
  };
  leaves.physical.render_geometry = [](XpEpubGeometry* out) {
    assert(inputModelDepth == 0 && currentInput);
    *out = {};
    out->width = 90;
    out->height = 120;
  };
  leaves.physical.input_services = []() -> const XpUiReaderInputServices* {
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
  inputOwner = xp_epub_test_input_create_legacy_book(&leaves.physical, &runtime, &resources, &leaves.book);
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

static ReaderUtils::TouchPageTurn ownerBookTouch(const MappedInputManager& input, bool rtl) {
  assert(inputOwner && !currentInput && inputModelDepth == 0);
  currentInput = &input;
  currentRtl = rtl;
  const unsigned previousLanguageCalls = languageCalls;
  XpUiReaderTurnInput result{};
  xp_epub_test_input_book_touch(inputOwner, &markerBook, &result);
  assert(languageCalls == previousLanguageCalls + 1);
  currentInput = nullptr;
  currentRtl = false;
  ++ownerBookTouchCalls;
  assert(inputModelDepth == 0 && result.reserved == 0);
  return {result.prev != 0, result.next != 0, result.held_ms};
}

static bool ownerMenu(const MappedInputManager& input) {
  assert(inputOwner && !currentInput && inputModelDepth == 0);
  currentInput = &input;
  const uint8_t result = xp_epub_test_input_menu(inputOwner);
  currentInput = nullptr;
  ++ownerMenuCalls;
  assert(inputModelDepth == 0 && result <= 1);
  return result != 0;
}

static void destroyInputOwner() {
  assert(inputOwner && !currentInput && inputModelDepth == 0);
  assert(ownerButtonCalls == 126720);
#ifdef XP_INPUT_EPUB
  assert(ownerTouchCalls == 0 && ownerBookTouchCalls == 13500 && ownerMenuCalls == 13500);
  assert(languageCalls == 13500);
#else
  assert(ownerTouchCalls == 6750 && ownerBookTouchCalls == 0 && ownerMenuCalls == 0);
  assert(languageCalls == 0);
#endif
  xp_epub_test_input_destroy(inputOwner);
  inputOwner = nullptr;
  assert(inputModelDepth == 0);
}
