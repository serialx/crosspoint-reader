#pragma once
#include <xp_epub_session.h>

#include "legacy_book_fixture.h"

struct XpEpubTestBookValues {
  int32_t spine;
  int32_t page;
  int32_t next_page;
  int32_t cached_spine;
  int32_t cached_count;
  int32_t last_spine;
  int32_t last_page;
  int32_t last_count;
  int32_t pending_page;
  int32_t has_pending_page;
  uint32_t pending_offset;
  int32_t has_pending_offset;
  uint32_t cached_offset;
  int32_t has_cached_offset;
  uint32_t visible_offset;
  int32_t has_visible_offset;
  int32_t partial_failed;
  int32_t percent;
  int32_t popup;
  int32_t stale;
  int32_t automatic;
  int32_t sync_error;
  int32_t screenshot;
  int32_t bookmark_notice;
  int32_t bookmark_removed;
  int32_t dictionary_notice;
  int32_t font_notice;
  int32_t clipping_notice;
  int32_t stored;
  int32_t overlay;
  int32_t retry;
  int32_t refresh;
  int32_t width;
  int32_t height;
  int32_t left;
  int32_t top;
  uint32_t last_render;
  uint32_t font_since;
  int32_t rendered;
  float spine_progress;
};
struct XpEpubTestBookHooks {
  void* context;
  void (*settle)(void*);
  uint8_t (*room)(void*);
  uint8_t (*deferred)(void*);
  void (*status)(void*);
  void (*bookmark)(void*);
  void (*contents)(void*, void*, const int32_t*);
  uint8_t (*save)(void*, int32_t, int32_t, int32_t);
  uint8_t (*toolbar)(void*);
  void (*overlay)(void*);
  void (*push)(void*);
  void (*clear)(void*);
  void (*discard)(void*);
  void (*mark)(void*);
};
struct XpEpubTestBookSeed {
  void* native;
  void* book;
  void* section;
  void* popup;
  XpAppText anchor;
  XpEpubTestBookValues values;
  XpEpubTestBookHooks hooks;
  void (*update)(void*);
};
struct XpEpubTestBookObservation {
  XpEpubTestBookValues values;
  void* section;
  XpAppText anchor;
  int32_t published_pages;
  int32_t published_estimate;
  uint8_t published_building, published_complete, published_partial;
};
extern "C" {
void* xp_epub_test_book_create_legacy_book(const XpEpubTestBookSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                           const XpAppResources*, const XpEpubLegacyBookServices*);
void xp_epub_test_book_render(void*);
void xp_epub_test_book_observe(void*, XpEpubTestBookObservation*);
void xp_epub_test_book_destroy(void*);
}
