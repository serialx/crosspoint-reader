#pragma once
#include "overlay_actions_session_fixture.h"

struct XpEpubTestChildSeed {
  void* native;
  void* book;
  void* section;
  int32_t spine;
  int32_t page;
  int32_t next_page;
  int32_t cached_count;
  uint16_t width;
  uint16_t height;
  uint8_t history_depth;
  void* context;
  void (*bookmark)(void*);
  void (*menu)(void*);
  void (*progress)(void*, const XpAppProgressResult*);
  void (*flush)(void*);
  uint8_t (*save_progress)(void*, int32_t, int32_t, int32_t);
  void (*navigate)(void*, XpAppText, uint8_t);
  int32_t (*hit)(void*, void*, int32_t, int32_t);
  void (*replace)(void*, void*);
  void (*position)(void*, XpEpubPosition*);
  uint32_t (*signature)(void*, uint16_t, uint16_t);
  void (*estimate)(void*, int32_t);
  void (*discard)(void*);
  void (*save_links)(void*);
  void (*update)(void*);
  void (*push)(void*, void*);
  void (*finish)(void*);
};
struct XpEpubTestChildObservation {
  int32_t page;
  int32_t next_page;
  int32_t spine;
  uint32_t visible_offset;
  uint32_t dictionary_time;
  uint32_t bookmark_time;
  uint32_t clipping_time;
  uint8_t sync_error;
  uint8_t dictionary;
  uint8_t bookmark;
  uint8_t clipping;
  uint8_t clipping_message;
  uint8_t has_offset;
  uint8_t has_book;
  uint8_t has_section;
};
extern "C" {
void* xp_epub_test_child_create(const XpEpubTestChildSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                const XpAppResources*);
void xp_epub_test_child_destroy(void*);
void xp_epub_test_child_observe(void*, XpEpubTestChildObservation*);
void xp_epub_test_child_spine(void*, int32_t);
void xp_epub_test_child_dictionary(void*);
void xp_epub_test_child_selection(void*, int32_t, int32_t, uint8_t);
void xp_epub_test_child_footnotes(void*, uint8_t);
void xp_epub_test_child_clippings(void*);
uint8_t xp_epub_test_child_sync(void*);
void xp_epub_test_child_result(void*, const XpAppResultView*);
}
