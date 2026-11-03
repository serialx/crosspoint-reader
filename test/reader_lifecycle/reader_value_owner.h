#pragma once
#include <cstring>

#include "../ble_link_note_title/owner_tables.h"
#include "projection_session_fixture.h"

extern "C" {
void* xp_epub_test_input_create_legacy_book(const XpEpubLeaves*, const XpAppRuntime*, const XpAppResources*,
                                            const XpEpubLegacyBookServices*);
void xp_epub_test_input_destroy(void*);
void xp_epub_test_input_book_touch(void*, void*, XpUiReaderTurnInput*);
}

namespace reader_value_fixture {
inline const char* language;
inline unsigned reads, probes;
inline uint8_t book;
inline unsigned languageCases, chapterCases;
inline void physical() { assert(!retirement_fixture::modelDepth); }
}  // namespace reader_value_fixture

// Classify a physical right swipe through the ordinary book-touch owner method.
// Extra silent probes are checked independently of the immutable language lines.
inline bool readerValueLanguage(const char* tag) {
  using namespace reader_value_fixture;
  using namespace retirement_fixture;
  assert(!language && !modelDepth);
  language = tag;
  reads = probes = 0;
  auto leaves = legacyLeavesDefaults();
  auto runtime = modelRuntime();
  auto resources = resourcesDefaults();
  leaves.book.book_language = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
    physical();
    assert(raw == &book && language);
    ++reads;
    return sink(context, {reinterpret_cast<const uint8_t*>(language), std::strlen(language)});
  };
  leaves.physical.render_geometry = [](XpEpubGeometry* out) {
    physical();
    *out = {};
    out->width = 90;
    out->height = 120;
  };
  leaves.physical.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    physical();
    assert(!resolve);
    *out = {};
    out->touch_controls = 1;
    out->page_gesture = 2;
    out->previous_gesture = 2;
  };
  leaves.physical.input_services = []() -> const XpUiReaderInputServices* {
    physical();
    static const XpUiReaderInputServices input{nullptr,
                                               &Unexpected<uint8_t (*)(void*, uint32_t, uint8_t, uint32_t)>::call,
                                               [](void*, uint8_t query) -> uint32_t {
                                                 physical();
                                                 if (probes++ == 0) {
                                                   assert(query == XP_UI_READER_INPUT_HAS_TOUCH);
                                                   return 1;
                                                 }
                                                 assert(probes == 2 && query == XP_UI_READER_INPUT_SWIPE);
                                                 return XP_UI_SWIPE_RIGHT;
                                               },
                                               &Unexpected<uint8_t (*)(void*, int32_t*, int32_t*)>::call};
    return &input;
  };
  void* owner = xp_epub_test_input_create_legacy_book(&leaves.physical, &runtime, &resources, &leaves.book);
  assert(owner);
  XpUiReaderTurnInput turn{};
  xp_epub_test_input_book_touch(owner, &book, &turn);
  assert(reads == 1 && probes == 2 && !modelDepth);
  assert(turn.prev <= 1 && turn.next <= 1 && turn.prev != turn.next);
  assert(!turn.from_tilt && !turn.held_ms && !turn.reserved);
  xp_epub_test_input_destroy(owner);
  language = nullptr;
  ++languageCases;
  return turn.next != 0;
}

inline XpEpubTestChapterDisplay readerValueChapter(int page, int total) {
  using namespace retirement_fixture;
  assert(!modelDepth);
  auto leaves = legacyLeavesDefaults();
  auto runtime = modelRuntime();
  auto resources = resourcesDefaults();
  uint8_t native;
  XpEpubTestProjectionSeed seed{};
  seed.native = &native;
  seed.next_page = page;
  seed.cached_count = total;
#define UNUSED_VALUE_HOOK(field) seed.field = &Unexpected<decltype(seed.field)>::call
  UNUSED_VALUE_HOOK(toolbar_menu);
  UNUSED_VALUE_HOOK(base_input);
  UNUSED_VALUE_HOOK(acknowledge);
  UNUSED_VALUE_HOOK(clear);
  UNUSED_VALUE_HOOK(fallback);
  UNUSED_VALUE_HOOK(estimate);
#undef UNUSED_VALUE_HOOK
  void* owner = xp_epub_test_projection_create_legacy_book(&seed, &leaves.physical, &runtime, &resources, &leaves.book);
  assert(owner);
  XpEpubTestChapterPosition position{};
  xp_epub_test_projection_chapter(owner, &position);
  assert(position.page == page && position.count == total);
  XpEpubTestChapterDisplay display{};
  xp_epub_test_projection_display(owner, &display);
  assert(display.has_total <= 1 && !display.reserved[0] && !display.reserved[1] && !display.reserved[2]);
  xp_epub_test_projection_destroy(owner);
  assert(!modelDepth);
  ++reader_value_fixture::chapterCases;
  return display;
}
