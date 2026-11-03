#pragma once
#include <xp_epub_session.h>
#include <xp_ui_reader.h>

#include <cstdint>

#include "legacy_book_fixture.h"
struct XpEpubTestLifecycleSnapshot {
  int32_t spine, page, next_page, cached_spine, cached_count;
  uint32_t cached_offset, last_turn;
  int32_t failed_spine, history[6];
  uint16_t pending_page;
  uint8_t has_pending_page, has_cached_offset, history_depth, heap_paused, overlay, stale;
  int8_t manual_turn;
  uint8_t manual_touch, orientation, has_section, radio_released;
};
struct XpEpubTestLifecycleSeed {
  void *native, *book, *section;
  XpEpubTestLifecycleSnapshot state;
  void* context;
  void (*clear)(void*);
  void (*acknowledge)(void*);
  void (*fallback)(void*);
  void (*estimate)(void*, int32_t);
  void (*building)(void*, uint8_t);
  uint8_t (*reposition)(void*);
  void (*update)(void*);
  XpUiReaderTurnInput (*buttons)(void*);
  void (*note)(void*, uint8_t, uint8_t);
  uint8_t (*end_turn)(void*, uint8_t, uint8_t);
  void (*orientation)(void*, uint8_t);
  uint8_t input_composition;
};
extern "C" {
void* xp_epub_test_lifecycle_create_legacy_book(const XpEpubTestLifecycleSeed*, const XpEpubLeaves*,
                                                const XpAppRuntime*, const XpAppResources*,
                                                const XpEpubLegacyBookServices*);
void xp_epub_test_lifecycle_destroy(void*);
void xp_epub_test_lifecycle_observe(void*, XpEpubTestLifecycleSnapshot*);
void xp_epub_test_lifecycle_clear_pending_page(void*);
void xp_epub_test_lifecycle_progress(void*);
void xp_epub_test_lifecycle_save_links(void*);
void xp_epub_test_lifecycle_load_links(void*);
void xp_epub_test_lifecycle_input(void*, XpUiReaderTurnInput);
uint8_t xp_epub_test_projection_turn(void*, uint8_t);
uint8_t xp_epub_test_projection_skip(void*, int32_t);
uint8_t xp_epub_test_build_room(void*);
uint8_t xp_epub_test_build_heap(void*);
void xp_epub_test_build_background(void*);
uint8_t xp_epub_test_build_wanted(void*);
uint8_t xp_epub_test_build_delay(void*);
void xp_epub_test_navigation_restore(void*);
}
