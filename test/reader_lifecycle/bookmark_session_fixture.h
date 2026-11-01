#pragma once
#include <xp_epub_session.h>

struct XpEpubTestBookmarkSeed {
  void* book;
  void* section;
  void* bookmarks;
  int32_t spine, page;
  uint32_t visible_offset;
  int32_t history[6];
  uint8_t has_visible_offset, removed, bookmarked, history_depth;
  void* context;
  void (*position)(void*, XpEpubPosition*);
  void (*sanitize)(void*, XpAppText);
  void (*estimate)(void*, int32_t);
};
struct XpEpubTestBookmarkObservation {
  int32_t history[6];
  uint8_t removed, bookmarked, history_depth, has_section;
};
struct XpEpubTestBookmarkRange {
  float start, end;
};
extern "C" {
void* xp_epub_test_bookmark_create(const XpEpubTestBookmarkSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                   const XpAppResources*);
void xp_epub_test_bookmark_destroy(void*);
void xp_epub_test_bookmark_observe(void*, XpEpubTestBookmarkObservation*);
void xp_epub_test_bookmark_flag(void*);
void xp_epub_test_bookmark_add(void*);
void xp_epub_test_bookmark_load(void*);
void xp_epub_test_bookmark_save_links(void*);
void xp_epub_test_bookmark_load_links(void*);
void xp_epub_test_bookmark_seed_history(void*, uint8_t, const int32_t*);
void xp_epub_test_bookmark_drop_section(void*);
XpEpubTestBookmarkRange xp_epub_test_bookmark_range(int32_t page, int32_t count);
uint8_t xp_epub_test_bookmark_matches(const XpEpubBookmark*, int32_t spine, int32_t page, int32_t count,
                                      XpEpubTestBookmarkRange);
}
