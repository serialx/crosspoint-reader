#pragma once

#include <xp_epub_session.h>

struct XpEpubTestLifetimeSeed {
  void* native;
  void* book;
  void* section;
  XpAppText path;
  int32_t spine, next_page, cached_spine, cached_count;
  uint32_t cached_offset;
  uint8_t has_cached_offset, radio_released, history_depth, move_to_read;
  void* context;
  void (*settle_overlay)(void*);
  void (*discard_overlay)(void*);
  void (*save_links)(void*);
  void (*load_links)(void*);
  void (*load_bookmarks)(void*);
  void (*disable_fast)(void*);
  uint8_t (*destination)(void*, XpAppText, XpAppTextSink, void*);
  void (*move_book)(void*, XpAppText, XpAppText, XpAppText);
};

struct XpEpubTestLifetimeObservation {
  int32_t spine, next_page, cached_spine, cached_count;
  uint32_t cached_offset;
  uint8_t has_book, has_cached_offset, failure;
};

extern "C" {
void* xp_epub_test_lifetime_create(const XpEpubTestLifetimeSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                   const XpAppResources*);
void xp_epub_test_lifetime_destroy(void*);
uint8_t xp_epub_test_lifetime_load(void*);
void xp_epub_test_lifetime_observe(void*, XpEpubTestLifetimeObservation*);
}
