#pragma once
#include <xp_epub_session.h>

#include "legacy_book_fixture.h"

struct XpEpubTestViewSeed {
  void* native;
  void* book;
  void* section;
  int32_t spine;
  int32_t page;
  uint32_t duration;
  uint8_t automatic;
  uint8_t bookmarked;
  void* context;
  uint16_t (*more_label)(void*, int32_t);
  void (*link_note_drawn)(void*, uint8_t, uint8_t);
};
struct XpEpubTestViewObservation {
  uint8_t note_shown;
  uint8_t note;
};
extern "C" {
void* xp_epub_test_view_create_legacy_book(const XpEpubTestViewSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                           const XpAppResources*, const XpEpubLegacyBookServices*);
void xp_epub_test_view_destroy(void*);
void xp_epub_test_view_seed_rows(void*, const uint8_t*, uint8_t, uint8_t);
void xp_epub_test_view_real_labels(void*);
void xp_epub_test_view_observe(void*, XpEpubTestViewObservation*);
void xp_epub_test_view_status(void*);
uint8_t xp_epub_test_view_toolbar(void*);
uint8_t xp_epub_test_view_text(void*, uint8_t, int32_t, XpAppTextSink, void*);
uint16_t xp_epub_test_view_action_label(uint8_t);
}
