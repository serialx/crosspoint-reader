#pragma once
#include <xp_epub_session.h>

#include <cstdlib>

#include "legacy_page_fixture.h"

// Only historical synthetic session constructors accept these inputs.
struct XpEpubLegacySectionServices {
  XpEpubLegacyPageServices page = unexpectedLegacyPageServices();
  void* (*section_open)(void* book, int32_t spine);
  void (*section_close)(void* section);
  uint16_t (*section_page_count)(void* section);
  int32_t (*section_estimated_pages)(void* section);
  uint8_t (*section_building)(void* section);
  uint8_t (*section_complete)(void* section);
  uint8_t (*section_partial)(void* section);
  uint8_t (*section_load_cache)(void* section, const XpEpubRenderSpec* spec);
  uint8_t (*section_create_cache)(void* section, const XpEpubRenderSpec* spec, XpEpubBuildPopup popup, void* context);
  uint8_t (*section_start_build)(void* section, const XpEpubRenderSpec* spec, XpEpubBuildPopup popup, void* context);
  uint8_t (*section_build_more)(void* section, int32_t max_pages, uint8_t yield_on_input);
  void (*section_abandon_build)(void* section);
  uint8_t (*section_clear_cache)(void* section);
  uint8_t (*section_html_cached)(void* section);
  uint8_t (*section_find_anchor)(void* section, XpAppText anchor, uint16_t* page);
  uint8_t (*section_cached_anchor)(void* section, XpAppText anchor, uint16_t* page);
  // Caller chooses null for a temporary cached Section; book stays borrowed for this call.
  int32_t (*section_toc_for_page)(void* section_or_null, void* book, int32_t spine, int32_t page);
  uint8_t (*section_page_for_offset)(void* section, uint32_t offset, uint8_t prefer_first, uint16_t* page);
  uint8_t (*section_offset_for_page)(void* section, uint16_t page, uint32_t* offset);
  uint8_t (*section_paragraph_for_page)(void* section, uint16_t page, uint16_t* paragraph);
  uint8_t (*section_reached_offset)(void* section, uint32_t offset);
  void* (*section_load_page)(void* section, int32_t page);
  uint8_t (*section_page_text)(void* section, int32_t page, XpAppTextSink sink, void* context);
};

inline XpEpubLegacySectionServices unexpectedLegacySectionServices();

struct XpEpubLegacyBookServices {
  XpEpubLegacySectionServices section = unexpectedLegacySectionServices();
  void* (*book_open)(XpAppText path);
  uint8_t (*book_load)(void* book, uint8_t build_missing, uint8_t skip_css);
  void (*book_close)(void* book);
  uint32_t (*book_size)(void* book);
  int32_t (*book_spine_count)(void* book);
  int32_t (*book_toc_count)(void* book);
  uint8_t (*book_path)(void* book, XpAppTextSink sink, void* context);
  uint8_t (*book_cache_path)(void* book, XpAppTextSink sink, void* context);
  uint8_t (*book_title)(void* book, XpAppTextSink sink, void* context);
  uint8_t (*book_author)(void* book, XpAppTextSink sink, void* context);
  uint8_t (*book_language)(void* book, XpAppTextSink sink, void* context);
  uint8_t (*book_spine)(void* book, int32_t index, XpEpubSpineSink sink, void* context);
  uint8_t (*book_toc)(void* book, int32_t index, XpEpubTocSink sink, void* context);
  int32_t (*book_toc_for_spine)(void* book, int32_t index);
  uint32_t (*book_cumulative_size)(void* book, int32_t spine);
  int32_t (*book_spine_for_size)(void* book, uint32_t bytes);
  int32_t (*book_text_reference)(void* book);
  float (*book_progress)(void* book, int32_t spine, float fraction);
  int32_t (*book_resolve_href)(void* book, XpAppText href);
  void (*book_setup_cache)(void* book);
  void (*book_clear_cache)(void* book);
  void (*book_install_extractor)(void* book);
  void (*book_clear_extractor)(void);
  uint8_t (*book_protection_error)(void* book, XpAppTextSink sink, void* context);
  uint8_t (*bookmarks_load)(void* bookmarks, void* book);
  uint8_t (*bookmarks_save)(void* bookmarks, void* book);
  uint8_t (*clippings_load)(void* book);
  void (*recents_add)(void* book);
};
struct LegacyEpubLeaves {
  XpEpubLeaves physical;
  XpEpubLegacyBookServices book;
};

template <typename F>
struct LegacyBookUnexpected;
template <typename R, typename... A>
struct LegacyBookUnexpected<R (*)(A...)> {
  [[noreturn]] static R call(A...) { std::abort(); }
};
inline XpEpubLegacySectionServices unexpectedLegacySectionServices() {
  XpEpubLegacySectionServices value{};
  value.section_open = &LegacyBookUnexpected<decltype(value.section_open)>::call;
  value.section_close = &LegacyBookUnexpected<decltype(value.section_close)>::call;
  value.section_page_count = &LegacyBookUnexpected<decltype(value.section_page_count)>::call;
  value.section_estimated_pages = &LegacyBookUnexpected<decltype(value.section_estimated_pages)>::call;
  value.section_building = &LegacyBookUnexpected<decltype(value.section_building)>::call;
  value.section_complete = &LegacyBookUnexpected<decltype(value.section_complete)>::call;
  value.section_partial = &LegacyBookUnexpected<decltype(value.section_partial)>::call;
  value.section_load_cache = &LegacyBookUnexpected<decltype(value.section_load_cache)>::call;
  value.section_create_cache = &LegacyBookUnexpected<decltype(value.section_create_cache)>::call;
  value.section_start_build = &LegacyBookUnexpected<decltype(value.section_start_build)>::call;
  value.section_build_more = &LegacyBookUnexpected<decltype(value.section_build_more)>::call;
  value.section_abandon_build = &LegacyBookUnexpected<decltype(value.section_abandon_build)>::call;
  value.section_clear_cache = &LegacyBookUnexpected<decltype(value.section_clear_cache)>::call;
  value.section_html_cached = &LegacyBookUnexpected<decltype(value.section_html_cached)>::call;
  value.section_find_anchor = &LegacyBookUnexpected<decltype(value.section_find_anchor)>::call;
  value.section_cached_anchor = &LegacyBookUnexpected<decltype(value.section_cached_anchor)>::call;
  value.section_toc_for_page = &LegacyBookUnexpected<decltype(value.section_toc_for_page)>::call;
  value.section_page_for_offset = &LegacyBookUnexpected<decltype(value.section_page_for_offset)>::call;
  value.section_offset_for_page = &LegacyBookUnexpected<decltype(value.section_offset_for_page)>::call;
  value.section_paragraph_for_page = &LegacyBookUnexpected<decltype(value.section_paragraph_for_page)>::call;
  value.section_reached_offset = &LegacyBookUnexpected<decltype(value.section_reached_offset)>::call;
  value.section_load_page = &LegacyBookUnexpected<decltype(value.section_load_page)>::call;
  value.section_page_text = &LegacyBookUnexpected<decltype(value.section_page_text)>::call;
  return value;
}
inline XpEpubLegacyBookServices unexpectedLegacyBookServices() {
  XpEpubLegacyBookServices value{};
  value.book_author = &LegacyBookUnexpected<decltype(value.book_author)>::call;
  value.book_cache_path = &LegacyBookUnexpected<decltype(value.book_cache_path)>::call;
  value.book_clear_cache = &LegacyBookUnexpected<decltype(value.book_clear_cache)>::call;
  value.book_clear_extractor = &LegacyBookUnexpected<decltype(value.book_clear_extractor)>::call;
  value.book_close = &LegacyBookUnexpected<decltype(value.book_close)>::call;
  value.book_cumulative_size = &LegacyBookUnexpected<decltype(value.book_cumulative_size)>::call;
  value.book_install_extractor = &LegacyBookUnexpected<decltype(value.book_install_extractor)>::call;
  value.book_language = &LegacyBookUnexpected<decltype(value.book_language)>::call;
  value.book_load = &LegacyBookUnexpected<decltype(value.book_load)>::call;
  value.book_open = &LegacyBookUnexpected<decltype(value.book_open)>::call;
  value.book_path = &LegacyBookUnexpected<decltype(value.book_path)>::call;
  value.book_progress = &LegacyBookUnexpected<decltype(value.book_progress)>::call;
  value.book_protection_error = &LegacyBookUnexpected<decltype(value.book_protection_error)>::call;
  value.book_resolve_href = &LegacyBookUnexpected<decltype(value.book_resolve_href)>::call;
  value.book_setup_cache = &LegacyBookUnexpected<decltype(value.book_setup_cache)>::call;
  value.book_size = &LegacyBookUnexpected<decltype(value.book_size)>::call;
  value.book_spine = &LegacyBookUnexpected<decltype(value.book_spine)>::call;
  value.book_spine_count = &LegacyBookUnexpected<decltype(value.book_spine_count)>::call;
  value.book_spine_for_size = &LegacyBookUnexpected<decltype(value.book_spine_for_size)>::call;
  value.book_text_reference = &LegacyBookUnexpected<decltype(value.book_text_reference)>::call;
  value.book_title = &LegacyBookUnexpected<decltype(value.book_title)>::call;
  value.book_toc = &LegacyBookUnexpected<decltype(value.book_toc)>::call;
  value.book_toc_count = &LegacyBookUnexpected<decltype(value.book_toc_count)>::call;
  value.book_toc_for_spine = &LegacyBookUnexpected<decltype(value.book_toc_for_spine)>::call;
  value.bookmarks_load = &LegacyBookUnexpected<decltype(value.bookmarks_load)>::call;
  value.bookmarks_save = &LegacyBookUnexpected<decltype(value.bookmarks_save)>::call;
  value.clippings_load = &LegacyBookUnexpected<decltype(value.clippings_load)>::call;
  value.recents_add = &LegacyBookUnexpected<decltype(value.recents_add)>::call;
  return value;
}

extern "C" XpAppEventResult xp_app_test_event_legacy_book(const XpAppEvent*, const XpEpubLegacyBookServices*);
