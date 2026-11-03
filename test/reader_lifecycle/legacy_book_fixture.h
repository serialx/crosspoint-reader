#pragma once
#include <xp_epub_session.h>

#include <cstdlib>

// Only historical synthetic session constructors accept these inputs.
struct XpEpubLegacyBookServices {
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
