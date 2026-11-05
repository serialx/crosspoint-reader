#pragma once
#include <xp_epub_session.h>

#include "legacy_book_fixture.h"

struct XpEpubTestOverlayViewSeed {
  void* native;
  void* book;
  void* section;
  void* toolbar;
  void* context;
  int32_t spine;
  int32_t page;
  int32_t focus;
  int32_t index;
  uint8_t overlay;
  uint8_t cursor;
  uint8_t more_count;
  uint8_t real_values;
  uint8_t more_actions[19];
  uint8_t (*chapter)(void*, XpAppTextSink, void*);
  uint8_t (*text_name)(void*, int32_t, XpAppTextSink, void*);
  uint8_t (*text_value)(void*, int32_t, XpAppTextSink, void*);
  uint8_t (*more_name)(void*, int32_t, XpAppTextSink, void*);
  uint8_t (*more_value)(void*, int32_t, XpAppTextSink, void*);
};
extern "C" {
void* xp_epub_test_overlay_view_create_legacy_book(const XpEpubTestOverlayViewSeed*, const XpEpubLeaves*,
                                                   const XpAppRuntime*, const XpAppResources*,
                                                   const XpEpubLegacyBookServices*);
void xp_epub_test_overlay_view_destroy(void*);
void xp_epub_test_overlay_view_render(void*);
}
