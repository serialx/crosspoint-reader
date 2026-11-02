#pragma once
#include "owner_tables.h"

// Existing app-test layouts; no shipping facade or policy-hook constructor.
struct XpEpubTestPositionSeed {
  void* native;
  void* book;
  void* section;
  int32_t page;
  int32_t spine;
  void* estimate_context;
  void (*estimate_read)(void*, int32_t);
};
struct XpEpubTestChapterPosition {
  int32_t page, count;
};
struct XpEpubTestChapterDisplay {
  int32_t display_page;
  float fraction;
  uint8_t has_total;
  uint8_t reserved[3];
};
extern "C" {
void* xp_epub_test_position_create(const XpEpubTestPositionSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                   const XpAppResources*);
void xp_epub_test_position_destroy(void*);
void xp_epub_test_helpers_apply_text_live(void*);
void xp_epub_test_projection_chapter(void*, XpEpubTestChapterPosition*);
void xp_epub_test_projection_display(void*, XpEpubTestChapterDisplay*);
}

namespace chapter_position_fixture {
struct Section {
  int32_t count;
};
struct Context {
  Section* section = nullptr;
  unsigned closed = 0, saved = 0, fonts = 0, updates = 0, renderDepth = 0;
};
inline Context* active;
inline void physical() { assert(active && retirement_fixture::modelDepth == 0); }
inline uint16_t pages(void* raw) {
  physical();
  assert(raw == active->section);
  return uint16_t(active->section->count);
}
}  // namespace chapter_position_fixture

// Preserve the original tests' value syntax; every result comes from the owner.
struct ChapterPosition {
  int pageIndex = 0;
  int totalPages = 0;
  int displayPage() const { return observe().display_page; }
  bool hasTotal() const { return observe().has_total != 0; }
  float chapterFraction() const { return observe().fraction; }

 private:
  XpEpubTestChapterDisplay observe() const {
    using namespace chapter_position_fixture;
    using namespace retirement_fixture;
    assert(!active && !modelDepth && totalPages >= 0 && totalPages <= UINT16_MAX);
    Context context;
    active = &context;
    context.section = new Section{totalPages};
    auto leaves = leavesDefaults();
    auto runtime = modelRuntime();
    auto resources = resourcesDefaults();
    runtime.render_lock = []() -> void* {
      physical();
      assert(!active->renderDepth);
      active->renderDepth = 1;
      return active;
    };
    runtime.render_unlock = [](void* raw) {
      physical();
      assert(raw == active && active->renderDepth == 1);
      active->renderDepth = 0;
    };
    runtime.notify_render = [] {
      physical();
      assert(!active->renderDepth);
      ++active->updates;
    };
    leaves.section_page_count = pages;
    leaves.section_estimated_pages = [](void* raw) -> int32_t {
      physical();
      assert(raw == active->section);
      return active->section->count;
    };
    leaves.section_building = [](void* raw) -> uint8_t {
      physical();
      assert(raw == active->section);
      return 0;
    };
    leaves.section_complete = [](void* raw) -> uint8_t {
      physical();
      assert(raw == active->section);
      return 1;
    };
    leaves.section_partial = [](void* raw) -> uint8_t {
      physical();
      assert(raw == active->section);
      return 0;
    };
    leaves.section_offset_for_page = [](void* raw, uint16_t page, uint32_t* out) -> uint8_t {
      physical();
      assert(active->renderDepth == 1 && raw == active->section && page < pages(raw));
      *out = page;
      return 1;
    };
    leaves.section_close = [](void* raw) {
      physical();
      assert(active->renderDepth == 1 && raw == active->section);
      delete active->section;
      active->section = nullptr;
      ++active->closed;
    };
    leaves.settings_save = [] {
      physical();
      assert(!active->renderDepth);
      ++active->saved;
    };
    leaves.fonts_ensure_loaded = [] {
      physical();
      assert(active->renderDepth == 1);
      ++active->fonts;
    };
    XpEpubTestPositionSeed seed{
        &context, nullptr, context.section, pageIndex, 0, nullptr, &Unexpected<void (*)(void*, int32_t)>::call};
    void* owner = xp_epub_test_position_create(&seed, &leaves, &runtime, &resources);
    assert(owner);
    // Ordinary text reflow caches page/count before releasing the real Section handle.
    xp_epub_test_helpers_apply_text_live(owner);
    assert(!context.section && context.closed == 1 && context.saved == 1 && context.fonts == 1 && context.updates == 1);
    XpEpubTestChapterPosition position{};
    xp_epub_test_projection_chapter(owner, &position);
    assert(position.page == pageIndex && position.count == totalPages);
    XpEpubTestChapterDisplay display{};
    xp_epub_test_projection_display(owner, &display);
    assert(display.has_total <= 1 && !display.reserved[0] && !display.reserved[1] && !display.reserved[2]);
    xp_epub_test_position_destroy(owner);
    assert(!modelDepth && !context.renderDepth && context.closed == 1);
    active = nullptr;
    return display;
  }
};
