#pragma once
#include <xp_epub_session.h>

#include "legacy_book_fixture.h"

struct XpEpubTestNavigationSeed {
  void* book;
  void* section;
  int32_t spine, active_page, next_page, cached_spine, cached_count;
  uint32_t cached_offset, visible_offset;
  int32_t history[6];
  uint8_t history_depth, has_cached_offset, has_visible_offset;
  XpAppText anchor;
  void* estimate_context;
  void (*estimate_read)(void*, int32_t);
};

struct XpEpubTestNavigationObservation {
  int32_t spine, next_page, active_page, cached_count;
  uint32_t cached_offset, pending_offset;
  float spine_progress;
  XpAppText anchor;
  int32_t history[6];
  uint8_t history_depth, has_section, has_cached_offset, has_pending_offset, pending_percent;
};

struct XpEpubTestBuildSeed {
  void* native;
  void* section;
  int32_t spine, active_page, next_page, failed_spine, refresh;
  uint8_t radio_released, heap_paused, popup, overlay;
  int8_t pending_turn;
  uint8_t stale;
  void* context;
  void (*building_read)(void*, uint8_t);
  uint8_t (*reposition)(void*);
};

struct XpEpubTestBuildObservation {
  int32_t next_page, failed_spine, refresh;
  uint8_t radio_released, heap_paused, popup, stale;
};

extern "C" {
void* xp_epub_test_navigation_create_legacy_book(const XpEpubTestNavigationSeed*, const XpEpubLeaves*,
                                                 const XpAppRuntime*, const XpAppResources*,
                                                 const XpEpubLegacyBookServices*);
void xp_epub_test_navigation_destroy(void*);
void xp_epub_test_navigation_observe(void*, XpEpubTestNavigationObservation*);
void xp_epub_test_navigation_percent(void*, int32_t);
void xp_epub_test_navigation_progress(void*, const XpAppProgressResult*);
uint8_t xp_epub_test_navigation_deferred(void*);
void xp_epub_test_navigation_clear(void*);
uint8_t xp_epub_test_navigation_save(void*, int32_t, int32_t, int32_t);
void xp_epub_test_navigation_remember(void*);
void xp_epub_test_navigation_href(void*, XpAppText, uint8_t);
void xp_epub_test_navigation_restore(void*);
void* xp_epub_test_build_create(const XpEpubTestBuildSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                const XpAppResources*);
void xp_epub_test_build_destroy(void*);
void xp_epub_test_build_observe(void*, XpEpubTestBuildObservation*);
uint8_t xp_epub_test_build_room(void*);
uint8_t xp_epub_test_build_heap(void*);
void xp_epub_test_build_popup(void*);
uint8_t xp_epub_test_build_buttons(void*);
void xp_epub_test_build_background(void*);
uint8_t xp_epub_test_build_wanted(void*);
uint8_t xp_epub_test_build_delay(void*);
}
