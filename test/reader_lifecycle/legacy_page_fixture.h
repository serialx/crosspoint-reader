#pragma once
#include <xp_epub_session.h>

#include <cstdlib>

#include "legacy_render_wire.h"

// Frozen native-shaped inputs for named synthetic Page oracles only.
typedef struct {
  uint32_t visible_offset;
  uint16_t elements;
  uint16_t footnotes;
  uint16_t links;
  uint8_t images;
  uint8_t images_need_decode;
} XpEpubPageInfo;

typedef struct {
  int32_t x;
  int32_t y;
  uint16_t words;
  uint8_t tag;
  uint8_t valid;
} XpEpubElement;

typedef struct {
  XpAppText text;
  int32_t x;
  uint32_t source_start;
  uint32_t source_end;
  uint8_t style;
  int8_t character_spacing;
} XpEpubWord;

typedef struct {
  XpAppText text;
  XpAppText href;
} XpEpubFootnote;

typedef struct {
  XpAppText href;
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
} XpEpubLink;

typedef uint8_t (*XpEpubFootnoteSink)(void* context, const XpEpubFootnote* value);
typedef uint8_t (*XpEpubLinkSink)(void* context, const XpEpubLink* value);
typedef void (*XpEpubLegacyHighlight)(void* context, void* page, int32_t font, int32_t left, int32_t top);

struct XpEpubLegacyPageServices {
  void (*page_close)(void* page);
  void (*page_info)(void* page, XpEpubPageInfo* out);
  void (*page_element)(void* page, uint16_t index, XpEpubElement* out);
  void (*page_word)(void* page, uint16_t element, uint16_t word, XpEpubWord* out);
  uint8_t (*metadata_assign)(void* resources, void* page);
  void (*metadata_clear)(void* resources);
  uint16_t (*metadata_footnotes)(void* resources);
  uint16_t (*metadata_links)(void* resources);
  uint8_t (*metadata_footnote)(void* resources, uint16_t index, XpEpubFootnoteSink sink, void* context);
  uint8_t (*metadata_link)(void* resources, uint16_t index, XpEpubLinkSink sink, void* context);
  int32_t (*page_word_font)(void* page, uint16_t element, uint16_t word, int32_t reader_font);
  int32_t (*page_line_font)(void* page, uint16_t element, int32_t reader_font);
  int32_t (*page_word_y_offset)(void* page, uint16_t element, uint16_t word, int32_t reader_font);
  int32_t (*page_line_ruby_shift)(void* page, uint16_t element, int32_t line_font);
  void (*page_render)(void* page, int32_t font, int32_t left, int32_t top);
  void* (*page_load)(void* file, uint32_t bytes, uint32_t visible_offset);
  void (*render_bind_page)(void* resources, void* page, int32_t font, int32_t left, int32_t top, int32_t bottom,
                           XpEpubLegacyHighlight highlight, void* context);
  void* (*child_footnote)(void* page, int32_t left, int32_t top);
  void* (*child_dictionary)(void* page, int32_t left, int32_t top, XpAppText initial_text);
  void* (*child_selection)(void** pages, uint8_t count, const XpEpubSelectionArgs* args);
  void* (*resources_open)();
  void (*resources_close)(void*);
  const XpUiReaderRenderServices* (*render_services)(void*);
  void (*render_finish_page)(void*);
  uint8_t (*strip_release_idle)(void*);
  uint8_t (*fonts_prewarm_begin)(void*);
  void (*fonts_prewarm_finish_scan)(void*);
  void (*fonts_prewarm_drop)(void*);
};

template <class F>
struct LegacyPageUnexpected;
template <class R, class... A>
struct LegacyPageUnexpected<R (*)(A...)> {
  [[noreturn]] static R call(A...) { std::abort(); }
};

inline XpEpubLegacyPageServices unexpectedLegacyPageServices() {
  XpEpubLegacyPageServices value{};
  value.page_close = &LegacyPageUnexpected<decltype(value.page_close)>::call;
  value.page_info = &LegacyPageUnexpected<decltype(value.page_info)>::call;
  value.page_element = &LegacyPageUnexpected<decltype(value.page_element)>::call;
  value.page_word = &LegacyPageUnexpected<decltype(value.page_word)>::call;
  value.metadata_assign = &LegacyPageUnexpected<decltype(value.metadata_assign)>::call;
  value.metadata_clear = &LegacyPageUnexpected<decltype(value.metadata_clear)>::call;
  value.metadata_footnotes = &LegacyPageUnexpected<decltype(value.metadata_footnotes)>::call;
  value.metadata_links = &LegacyPageUnexpected<decltype(value.metadata_links)>::call;
  value.metadata_footnote = &LegacyPageUnexpected<decltype(value.metadata_footnote)>::call;
  value.metadata_link = &LegacyPageUnexpected<decltype(value.metadata_link)>::call;
  value.page_word_font = &LegacyPageUnexpected<decltype(value.page_word_font)>::call;
  value.page_line_font = &LegacyPageUnexpected<decltype(value.page_line_font)>::call;
  value.page_word_y_offset = &LegacyPageUnexpected<decltype(value.page_word_y_offset)>::call;
  value.page_line_ruby_shift = &LegacyPageUnexpected<decltype(value.page_line_ruby_shift)>::call;
  value.page_render = &LegacyPageUnexpected<decltype(value.page_render)>::call;
  value.page_load = &LegacyPageUnexpected<decltype(value.page_load)>::call;
  value.render_bind_page = &LegacyPageUnexpected<decltype(value.render_bind_page)>::call;
  value.child_footnote = &LegacyPageUnexpected<decltype(value.child_footnote)>::call;
  value.child_dictionary = &LegacyPageUnexpected<decltype(value.child_dictionary)>::call;
  value.child_selection = &LegacyPageUnexpected<decltype(value.child_selection)>::call;
  value.resources_open = &LegacyPageUnexpected<decltype(value.resources_open)>::call;
  value.resources_close = &LegacyPageUnexpected<decltype(value.resources_close)>::call;
  value.render_services = &LegacyPageUnexpected<decltype(value.render_services)>::call;
  value.render_finish_page = &LegacyPageUnexpected<decltype(value.render_finish_page)>::call;
  value.strip_release_idle = &LegacyPageUnexpected<decltype(value.strip_release_idle)>::call;
  value.fonts_prewarm_begin = &LegacyPageUnexpected<decltype(value.fonts_prewarm_begin)>::call;
  value.fonts_prewarm_finish_scan = &LegacyPageUnexpected<decltype(value.fonts_prewarm_finish_scan)>::call;
  value.fonts_prewarm_drop = &LegacyPageUnexpected<decltype(value.fonts_prewarm_drop)>::call;
  return value;
}
