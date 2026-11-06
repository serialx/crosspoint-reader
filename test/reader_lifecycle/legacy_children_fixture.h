#pragma once
#include <xp_epub_session.h>

#include <cstdlib>

// Frozen native child wire values, used only by historical host fixtures.
typedef struct {
  XpAppText title;
  int32_t display_page;
  int32_t pages;
  int32_t percent;
  uint8_t orientation;
  uint8_t footnotes;
  uint8_t bookmarks;
  uint8_t clippings;
} XpEpubMenuArgs;

typedef struct {
  XpAppText chapter;
  XpAppText page_info;
  XpAppText panel_title;
  int32_t progress_permille;
  int32_t item_count;
  int32_t selected;
  int32_t active_tool;
  uint8_t panel;
  uint8_t dense;
} XpEpubToolbarModel;

typedef struct {
  XpAppText label;
  XpAppText value;
  uint32_t label_spaces;
  uint8_t toggle;
  uint8_t checked;
} XpEpubToolbarRow;

typedef struct {
  int32_t value;
  int32_t permille;
  int32_t x;
  int32_t y;
  uint8_t event;
  uint8_t routed;
} XpEpubRouted;

typedef uint8_t (*XpEpubToolbarRowSink)(void* context, const XpEpubToolbarRow* value);

typedef uint8_t (*XpEpubRowSource)(void* context, int32_t index, XpEpubToolbarRowSink sink, void* sink_context);

struct XpEpubLegacyChildren {
  void* (*child_menu)(const XpEpubMenuArgs* args);
  void* (*child_chapter)(void* book, int32_t toc);
  void* (*child_percent)(int32_t percent);
  void* (*child_qr)(XpAppText text);
  void* (*toolbar_open)(void);
  void (*toolbar_close)(void* toolbar);
  void (*toolbar_begin)(void* toolbar);
  uint8_t (*toolbar_paint)(void* toolbar, const XpEpubToolbarModel* model, XpEpubRowSource rows, void* context);
  void (*toolbar_route)(void* toolbar, XpEpubRouted* out);
  uint8_t (*toolbar_scroll)(void* toolbar, int32_t delta, int32_t count);
  int32_t (*toolbar_visible_rows)(void* toolbar);
  void (*toolbar_reset_navigation)(void* toolbar, int32_t selected, int32_t top);
};

template <typename F>
struct LegacyChildUnexpected;
template <typename R, typename... A>
struct LegacyChildUnexpected<R (*)(A...)> {
  [[noreturn]] static R call(A...) { std::abort(); }
};
inline XpEpubLegacyChildren unexpectedLegacyChildren() {
  XpEpubLegacyChildren value{};
  value.child_menu = &LegacyChildUnexpected<decltype(value.child_menu)>::call;
  value.child_chapter = &LegacyChildUnexpected<decltype(value.child_chapter)>::call;
  value.child_percent = &LegacyChildUnexpected<decltype(value.child_percent)>::call;
  value.child_qr = &LegacyChildUnexpected<decltype(value.child_qr)>::call;
  value.toolbar_open = &LegacyChildUnexpected<decltype(value.toolbar_open)>::call;
  value.toolbar_close = &LegacyChildUnexpected<decltype(value.toolbar_close)>::call;
  value.toolbar_begin = &LegacyChildUnexpected<decltype(value.toolbar_begin)>::call;
  value.toolbar_paint = &LegacyChildUnexpected<decltype(value.toolbar_paint)>::call;
  value.toolbar_route = &LegacyChildUnexpected<decltype(value.toolbar_route)>::call;
  value.toolbar_scroll = &LegacyChildUnexpected<decltype(value.toolbar_scroll)>::call;
  value.toolbar_visible_rows = &LegacyChildUnexpected<decltype(value.toolbar_visible_rows)>::call;
  value.toolbar_reset_navigation = &LegacyChildUnexpected<decltype(value.toolbar_reset_navigation)>::call;
  return value;
}
extern "C" void xp_epub_test_legacy_children(void*, const XpEpubLegacyChildren*);

static_assert(sizeof(XpEpubLegacyChildren) == 12 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, child_menu) == 0 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, child_chapter) == 1 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, child_percent) == 2 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, child_qr) == 3 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, toolbar_open) == 4 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, toolbar_close) == 5 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, toolbar_begin) == 6 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, toolbar_paint) == 7 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, toolbar_route) == 8 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, toolbar_scroll) == 9 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, toolbar_visible_rows) == 10 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyChildren, toolbar_reset_navigation) == 11 * sizeof(void (*)()));
