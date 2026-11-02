// Appended to the copied existing input adapter; no native classification math.
#include "projection_session_fixture.h"

// Main-approved test-only observation; main adds the matching Rust entry/header.
// Remove this local declaration when the repository header supplies it.
struct XpEpubTestChapterDisplay {
  int32_t display_page;
  float fraction;
  uint8_t has_total;
  uint8_t reserved[3];
};
extern "C" void xp_epub_test_projection_display(void*, XpEpubTestChapterDisplay*);
static unsigned ownerLanguageCases = 0, ownerChapterCases = 0;

static bool ownerLanguageTag(const char* language) {
  assert(inputOwner && !currentInput && !currentLanguage && !inputProbeOnly && !inputModelDepth);
  const auto savedSettings = SETTINGS;
  SETTINGS.touchReaderControls = true;
  SETTINGS.pageTurnGesture = CrossPointSettings::SWIPE_ONLY;
  SETTINGS.previousPageGesture = CrossPointSettings::SWIPE_ONLY;
  MappedInputManager stimulus;
  stimulus.touch = true;
  stimulus.swipe = MappedInputManager::SwipeDir::Right;
  currentInput = &stimulus;
  currentLanguage = language;
  inputProbeLines.clear();
  inputProbeOnly = true;
  const auto before = languageCalls;
  XpUiReaderTurnInput result{};
  xp_epub_test_input_book_touch(inputOwner, &markerBook, &result);
  inputProbeOnly = false;
  currentInput = nullptr;
  currentLanguage = nullptr;
  SETTINGS = savedSettings;
  assert(languageCalls == before + 1 && !inputModelDepth);
  // Additional physical probes have no historical language-line counterpart.
  // Check them separately; the entire original output is still compared unchanged.
  assert((inputProbeLines == std::vector<std::string>{"touch 1\n", "swipe 4\n"}));
  assert(result.prev <= 1 && result.next <= 1 && result.prev != result.next);
  assert(!result.from_tilt && !result.held_ms && !result.reserved && !stimulus.touchOverride);
  ++ownerLanguageCases;
  return result.next != 0;
}

static XpEpubTestChapterDisplay ownerChapterDisplay(int page, int total) {
  assert(inputOwner && !currentInput && !inputModelDepth);
  auto leaves = overlayUnexpectedLeaves();
  auto runtime = overlayUnexpectedRuntime();
  auto resources = overlayUnexpectedResources();
  runtime.model_lock = [] { assert(!inputModelDepth); ++inputModelDepth; };
  runtime.model_unlock = [] { assert(inputModelDepth == 1); --inputModelDepth; };
  unsigned char native;
  XpEpubTestProjectionSeed seed{};
  seed.native = &native;
  seed.next_page = page;
  seed.cached_count = total;
#define UNUSED_CHAPTER_HOOK(field) seed.field = &OverlayUnexpected<decltype(seed.field)>::call
  UNUSED_CHAPTER_HOOK(toolbar_menu);
  UNUSED_CHAPTER_HOOK(base_input);
  UNUSED_CHAPTER_HOOK(acknowledge);
  UNUSED_CHAPTER_HOOK(clear);
  UNUSED_CHAPTER_HOOK(fallback);
  UNUSED_CHAPTER_HOOK(estimate);
#undef UNUSED_CHAPTER_HOOK
  void* owner = xp_epub_test_projection_create(&seed, &leaves, &runtime, &resources);
  assert(owner && !inputModelDepth);
  XpEpubTestChapterPosition actual{};
  xp_epub_test_projection_chapter(owner, &actual);
  assert(actual.page == page && actual.count == total);
  XpEpubTestChapterDisplay display{};
  xp_epub_test_projection_display(owner, &display);
  assert(display.has_total <= 1 && !display.reserved[0] && !display.reserved[1] && !display.reserved[2]);
  xp_epub_test_projection_destroy(owner);
  assert(!inputModelDepth);
  ++ownerChapterCases;
  return display;
}
