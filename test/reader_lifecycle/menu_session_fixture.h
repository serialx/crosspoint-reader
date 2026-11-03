#pragma once
#include "legacy_book_fixture.h"
#include "overlay_actions_session_fixture.h"

struct XpEpubTestMenuHooks {
  void* context;
  void (*chapter)(void*, int32_t*);
  int32_t (*percent)(void*, int32_t, int32_t);
  void (*orientation)(void*, uint8_t);
  void (*automatic)(void*, uint8_t);
  void (*progress)(void*, const XpAppProgressResult*);
  void (*clear)(void*);
  void (*footnotes)(void*, uint8_t);
  void (*jump)(void*, int32_t);
  void (*dictionary)(void*);
  void (*home)(void*);
  uint8_t (*save)(void*, int32_t, int32_t, int32_t);
  void (*sync)(void*);
  void (*selection)(void*, int32_t, int32_t, uint8_t);
  void (*clippings)(void*);
  void (*bookmark)(void*);
  void (*bluetooth)(void*);
  uint8_t (*build_items)(void*, uint8_t, uint8_t, uint8_t, uint8_t*);
  void (*remember)(void*);
  void (*popup)(void*);
};
struct XpEpubTestMenuSeed {
  void* native;
  void* book;
  void* section;
  void* toolbar;
  void* popup;
  void* bookmarks;
  int32_t spine;
  int32_t page;
  int32_t cached_spine;
  int32_t cached_count;
  int32_t next_page;
  int32_t focus;
  int8_t manual_turn;
  uint8_t automatic;
  uint8_t overlay;
  uint8_t stored;
  uint8_t hold;
  uint8_t cursor;
  uint8_t more_actions[19];
  uint8_t more_count;
  XpEpubTestMenuHooks hooks;
  uint8_t (*toolbar_menu)(void*);
  int32_t (*toc)(void*);
  void (*discard)(void*);
  void (*settle)(void*);
  void (*load_bookmarks)(void*);
  void (*render)(void*);
  void (*push_overlay)(void*);
  void (*update)(void*);
  void (*push_child)(void*, void*);
};
struct XpEpubTestMenuObservation {
  int32_t cached_spine;
  int32_t cached_count;
  int32_t next_page;
  int32_t spine;
  int32_t focus;
  uint32_t bookmark_time;
  int8_t manual_turn;
  uint8_t automatic;
  uint8_t overlay;
  uint8_t stored;
  uint8_t hold;
  uint8_t cursor;
  uint8_t bookmarked;
  uint8_t screenshot;
  uint8_t has_section;
  uint8_t has_toolbar;
  uint8_t more_actions[19];
  uint8_t more_count;
};
extern "C" {
void* xp_epub_test_menu_create_legacy_book(const XpEpubTestMenuSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                           const XpAppResources*, const XpEpubLegacyBookServices*);
void xp_epub_test_menu_destroy(void*);
void xp_epub_test_menu_observe(void*, XpEpubTestMenuObservation*);
void xp_epub_test_menu_anchor(void*, XpAppTextSink, void*);
void xp_epub_test_menu_open(void*);
void xp_epub_test_menu_confirm(void*, int32_t);
void xp_epub_test_menu_build_more(void*);
void xp_epub_test_menu_activate(void*, int32_t);
void xp_epub_test_menu_result(void*, const XpAppResultView*);
}
