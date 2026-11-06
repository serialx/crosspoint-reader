#pragma once
#include "xp_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// Private reader family ABI. All descriptors and row strings are borrowed for
// the call; scene resources remain immutable through painting/publication.
typedef enum {
  XP_UI_READER_CHAPTERS,
  XP_UI_READER_MENU,
  XP_UI_READER_BOOKMARKS,
  XP_UI_READER_CLIPPINGS,
  XP_UI_READER_END,
  XP_UI_READER_PANEL
} XpUiReaderListKind;

typedef struct {
  XpUiListState list;
  int32_t revealed;
  uint8_t long_press_consumed, reserved[3];
} XpUiReaderListState;

typedef struct {
  XpUiRect screen, safe;
  int32_t body_font, small_font, title_font, fixed_title_font, help_font;
  int16_t row_min, row_gap, row_inset, row_padding, scroll_width;
  int16_t top_padding, header_height, spacing, tab_height, header_padding;
  int16_t space_sm, space_md, space_lg;
  uint8_t row_radius, selection, scroll_left, title_bold;
  uint8_t control_radius, sheet_radius, touch, orientation;
} XpUiReaderTheme;

typedef struct {
  uint32_t label, subtitle, value, icon;
  uint8_t toggle, checked, reserved[2];
} XpUiReaderRow;

typedef struct {
  void* context;
  // Prepare the existing bounded row window. No pointer may outlive its owner.
  uint8_t (*window)(void*, uint16_t first, uint16_t requested);
  uint8_t (*row)(void*, uint16_t index, XpUiReaderRow*);
  int32_t (*text_width)(void*, uint32_t handle, int32_t font, uint8_t bold);
  int32_t (*text_height)(void*, uint32_t handle, int32_t font, uint8_t bold, uint8_t lines, int16_t width);
  int32_t (*line_height)(void*, int32_t font);
  uint16_t count, window_capacity;
  uint32_t generation;
} XpUiReaderRows;

typedef struct {
  XpUiRect rect;
  int32_t value;
  uint8_t action, reserved[3];
} XpUiReaderControl;  // 16 B, align 4

typedef struct {
  XpUiRect first_row;
  int32_t first_index;
  int16_t gap;
  uint8_t row_count, row_action;
  int16_t heights[24];
  XpUiReaderControl controls[7];
  uint32_t generation;
  uint8_t control_count, uniform, reserved[2];
} XpUiReaderHitLayout;  // 184 B, align 4; offsets 0,8,12,14,15,16,64,176,180,181,182

enum { XP_UI_READER_VIEW_INVALID = 0, XP_UI_READER_VIEW_OK = 1, XP_UI_READER_VIEW_CAPACITY = 2 };

// Handles outside the row namespace are immutable chrome resource bindings.
#define XP_UI_READER_HEADER 0x80000001u
#define XP_UI_READER_FOOTER 0x80000002u
#define XP_UI_READER_EMPTY 0x80000003u
#define XP_UI_READER_HELP 0x80000004u
#define XP_UI_READER_PROGRESS 0x80000005u
#define XP_UI_READER_TITLE 0x80000006u
#define XP_UI_READER_SUBTITLE 0x80000007u
#define XP_UI_READER_DELETE_ICON 0x80000008u
#define XP_UI_READER_TOOL_CONTENTS 0x80000009u
#define XP_UI_READER_TOOL_TEXT 0x8000000au
#define XP_UI_READER_TOOL_MORE 0x8000000bu
#define XP_UI_READER_TOOL_PREV 0x8000000cu
#define XP_UI_READER_TOOL_NEXT 0x8000000du
#define XP_UI_READER_PANEL_PAGE 0x8000000eu

void xp_ui_reader_list_init(XpUiReaderListState*, int32_t selected);
// The common effect IDs retain their meanings. These family effects request
// the existing bookmark/clipping action and deletion resource bindings.
enum { XP_UI_READER_ACTIONS = 4, XP_UI_READER_DELETE = 5 };
void xp_ui_reader_list_select(XpUiReaderListState*, int32_t selected, uint16_t count);
XpUiInterest xp_ui_reader_list_interest(const XpUiReaderListState*, uint8_t kind, uint32_t now_ms);
// Extra native classifier facts preserve the existing long-press and swipe
// ownership without adding another hardware poll or a second classifier.
XpUiEffect xp_ui_reader_list_update(XpUiReaderListState*, const XpUiInput*, uint16_t count, uint8_t present,
                                    uint8_t kind, uint8_t long_press, int32_t swipe_row);
void xp_ui_reader_list_publish(XpUiReaderListState*, const XpUiReaderListState*, int32_t consumed_scroll);
// View outputs remain unchanged on INVALID/CAPACITY; scratch commands may change.
// Reader scenes have no hit array: buffer.hits may be null with capacity zero.
uint8_t xp_ui_reader_list_view(const XpUiReaderListState*, uint8_t kind, const XpUiReaderTheme*, const XpUiReaderRows*,
                               const XpUiBuffer*, XpUiReaderListState*, XpUiScene*, XpUiReaderHitLayout*);

// Existing public MenuAction values used by the parent-facing facade.
enum {
  XP_UI_READER_MENU_NIGHT = 3,
  XP_UI_READER_MENU_FRONTLIGHT = 4,
  XP_UI_READER_MENU_BLUETOOTH = 5,
  XP_UI_READER_MENU_AUTO_TURN = 7,
  XP_UI_READER_MENU_ORIENTATION = 8,
  XP_UI_READER_MENU_LAST_ACTION = 18
};
typedef struct {
  uint8_t orientation, page_turn, reserved[2];
} XpUiReaderMenuState;
typedef struct {
  int32_t action;
  uint8_t orientation, page_turn, cancelled, effect;
} XpUiReaderMenuResult;
typedef enum {
  XP_UI_READER_MENU_NONE,
  XP_UI_READER_MENU_SHOW_ORIENTATION,
  XP_UI_READER_MENU_SHOW_AUTO_TURN,
  XP_UI_READER_MENU_TOGGLE_NIGHT,
  XP_UI_READER_MENU_TOGGLE_BLUETOOTH,
  XP_UI_READER_MENU_TOGGLE_FRONTLIGHT,
  XP_UI_READER_MENU_FINISH,
  XP_UI_READER_MENU_APPLY_ORIENTATION,
  XP_UI_READER_MENU_REDRAW
} XpUiReaderMenuEffect;
enum { XP_UI_READER_MENU_CHOOSE_ORIENTATION, XP_UI_READER_MENU_CHOOSE_AUTO_TURN };
void xp_ui_reader_menu_init(XpUiReaderMenuState*, uint8_t orientation);
XpUiReaderMenuResult xp_ui_reader_menu_activate(const XpUiReaderMenuState*, int32_t action);
XpUiReaderMenuResult xp_ui_reader_menu_choose(XpUiReaderMenuState*, uint8_t option, int32_t selection);
XpUiReaderMenuResult xp_ui_reader_menu_cancel(const XpUiReaderMenuState*);

typedef struct {
  int32_t selected;
  uint32_t last_move_ms;
} XpUiReaderFootnoteState;
XpUiInterest xp_ui_reader_footnote_interest(const XpUiReaderFootnoteState*, uint32_t now_ms);
XpUiEffect xp_ui_reader_footnote_update(XpUiReaderFootnoteState*, const XpUiInput*, uint16_t count);
// Geometry is logical, including the original top/left clipping before the
// framebuffer snapshot service. That service retains its existing 4096 B cap.
uint8_t xp_ui_reader_footnote_view(XpUiRect screen, XpUiRect link, int16_t margin_left, int16_t margin_top,
                                   int32_t font, uint32_t number_handle, const XpUiBuffer*, XpUiRect* snapshot,
                                   XpUiScene*);

typedef struct {
  uint8_t panel, dense_rows, reserved[2];
  int32_t active_tool, progress_permille;
  uint32_t chapter_title, page_info, panel_title;
} XpUiReaderToolbarModel;
uint8_t xp_ui_reader_toolbar_view(const XpUiListState*, const XpUiReaderToolbarModel*, const XpUiReaderTheme*,
                                  const XpUiReaderRows*, const XpUiBuffer*, XpUiListState*, XpUiScene*,
                                  XpUiReaderHitLayout*);
typedef struct {
  int16_t active, last_drag_x;
  uint8_t contact_held, reserved[3];
} XpUiReaderToolbarInput;
// Init with active=last_drag_x=-1, contact_held=0. Effect.kind is the unchanged
// ReaderToolbarUi Event ID; invalidate reports routed, and scrub is release-only.
XpUiEffect xp_ui_reader_toolbar_route(XpUiReaderToolbarInput*, const XpUiReaderHitLayout*, const XpUiInput*,
                                      int32_t* permille);
uint8_t xp_ui_reader_hit(const XpUiReaderHitLayout*, int16_t x, int16_t y, XpUiInteraction*);
// Effect.kind is the existing EndOfBookOptions::Action ID (0..4).
XpUiEffect xp_ui_reader_end_update(XpUiListState*, const XpUiInput*, uint16_t count, uint8_t use_press);

#ifdef __cplusplus
}
#include <stddef.h>
static_assert(sizeof(XpUiReaderControl) == 16);
static_assert(alignof(XpUiReaderControl) == 4);
static_assert(offsetof(XpUiReaderControl, rect) == 0);
static_assert(offsetof(XpUiReaderControl, value) == 8);
static_assert(offsetof(XpUiReaderControl, action) == 12);
static_assert(offsetof(XpUiReaderControl, reserved) == 13);
static_assert(sizeof(XpUiReaderHitLayout) == 184);
static_assert(alignof(XpUiReaderHitLayout) == 4);
static_assert(offsetof(XpUiReaderHitLayout, first_row) == 0);
static_assert(offsetof(XpUiReaderHitLayout, first_index) == 8);
static_assert(offsetof(XpUiReaderHitLayout, gap) == 12);
static_assert(offsetof(XpUiReaderHitLayout, row_count) == 14);
static_assert(offsetof(XpUiReaderHitLayout, row_action) == 15);
static_assert(offsetof(XpUiReaderHitLayout, heights) == 16);
static_assert(offsetof(XpUiReaderHitLayout, controls) == 64);
static_assert(offsetof(XpUiReaderHitLayout, generation) == 176);
static_assert(offsetof(XpUiReaderHitLayout, control_count) == 180);
static_assert(offsetof(XpUiReaderHitLayout, uniform) == 181);
static_assert(offsetof(XpUiReaderHitLayout, reserved) == 182);
// XP_UI_SCROLL must remain the last shared effect ID; keep these family IDs above it.
static_assert(int(XP_UI_READER_ACTIONS) > int(XP_UI_SCROLL));
static_assert(int(XP_UI_READER_DELETE) > int(XP_UI_READER_ACTIONS));
static_assert(sizeof(XpUiReaderMenuState) == 4);
static_assert(alignof(XpUiReaderMenuState) == 1);
static_assert(offsetof(XpUiReaderMenuState, orientation) == 0);
static_assert(offsetof(XpUiReaderMenuState, page_turn) == 1);
static_assert(offsetof(XpUiReaderMenuState, reserved) == 2);
static_assert(sizeof(XpUiReaderMenuResult) == 8);
static_assert(alignof(XpUiReaderMenuResult) == 4);
static_assert(offsetof(XpUiReaderMenuResult, action) == 0);
static_assert(offsetof(XpUiReaderMenuResult, orientation) == 4);
static_assert(offsetof(XpUiReaderMenuResult, page_turn) == 5);
static_assert(offsetof(XpUiReaderMenuResult, cancelled) == 6);
static_assert(offsetof(XpUiReaderMenuResult, effect) == 7);
static_assert(sizeof(XpUiReaderListState) == 48);
static_assert(alignof(XpUiReaderListState) == 4);
static_assert(offsetof(XpUiReaderListState, list) == 0);
static_assert(offsetof(XpUiReaderListState, revealed) == 40);
static_assert(offsetof(XpUiReaderListState, long_press_consumed) == 44);
static_assert(offsetof(XpUiReaderListState, reserved) == 45);
static_assert(sizeof(XpUiReaderTheme) == 72);
static_assert(alignof(XpUiReaderTheme) == 4);
static_assert(offsetof(XpUiReaderTheme, screen) == 0);
static_assert(offsetof(XpUiReaderTheme, safe) == 8);
static_assert(offsetof(XpUiReaderTheme, body_font) == 16);
static_assert(offsetof(XpUiReaderTheme, small_font) == 20);
static_assert(offsetof(XpUiReaderTheme, title_font) == 24);
static_assert(offsetof(XpUiReaderTheme, fixed_title_font) == 28);
static_assert(offsetof(XpUiReaderTheme, help_font) == 32);
static_assert(offsetof(XpUiReaderTheme, row_min) == 36);
static_assert(offsetof(XpUiReaderTheme, row_gap) == 38);
static_assert(offsetof(XpUiReaderTheme, row_inset) == 40);
static_assert(offsetof(XpUiReaderTheme, row_padding) == 42);
static_assert(offsetof(XpUiReaderTheme, scroll_width) == 44);
static_assert(offsetof(XpUiReaderTheme, top_padding) == 46);
static_assert(offsetof(XpUiReaderTheme, header_height) == 48);
static_assert(offsetof(XpUiReaderTheme, spacing) == 50);
static_assert(offsetof(XpUiReaderTheme, tab_height) == 52);
static_assert(offsetof(XpUiReaderTheme, header_padding) == 54);
static_assert(offsetof(XpUiReaderTheme, space_sm) == 56);
static_assert(offsetof(XpUiReaderTheme, space_md) == 58);
static_assert(offsetof(XpUiReaderTheme, space_lg) == 60);
static_assert(offsetof(XpUiReaderTheme, row_radius) == 62);
static_assert(offsetof(XpUiReaderTheme, selection) == 63);
static_assert(offsetof(XpUiReaderTheme, scroll_left) == 64);
static_assert(offsetof(XpUiReaderTheme, title_bold) == 65);
static_assert(offsetof(XpUiReaderTheme, control_radius) == 66);
static_assert(offsetof(XpUiReaderTheme, sheet_radius) == 67);
static_assert(offsetof(XpUiReaderTheme, touch) == 68);
static_assert(offsetof(XpUiReaderTheme, orientation) == 69);
static_assert(sizeof(XpUiReaderRow) == 20);
static_assert(alignof(XpUiReaderRow) == 4);
static_assert(offsetof(XpUiReaderRow, label) == 0);
static_assert(offsetof(XpUiReaderRow, subtitle) == 4);
static_assert(offsetof(XpUiReaderRow, value) == 8);
static_assert(offsetof(XpUiReaderRow, icon) == 12);
static_assert(offsetof(XpUiReaderRow, toggle) == 16);
static_assert(offsetof(XpUiReaderRow, checked) == 17);
static_assert(offsetof(XpUiReaderRow, reserved) == 18);
static_assert(sizeof(XpUiReaderFootnoteState) == 8);
static_assert(alignof(XpUiReaderFootnoteState) == 4);
static_assert(offsetof(XpUiReaderFootnoteState, selected) == 0);
static_assert(offsetof(XpUiReaderFootnoteState, last_move_ms) == 4);
static_assert(sizeof(XpUiReaderRows) == sizeof(void*) * 6 + 8);
static_assert(alignof(XpUiReaderRows) == alignof(void*));
static_assert(offsetof(XpUiReaderRows, context) == sizeof(void*) * 0);
static_assert(offsetof(XpUiReaderRows, window) == sizeof(void*) * 1);
static_assert(offsetof(XpUiReaderRows, row) == sizeof(void*) * 2);
static_assert(offsetof(XpUiReaderRows, text_width) == sizeof(void*) * 3);
static_assert(offsetof(XpUiReaderRows, text_height) == sizeof(void*) * 4);
static_assert(offsetof(XpUiReaderRows, line_height) == sizeof(void*) * 5);
static_assert(offsetof(XpUiReaderRows, count) == sizeof(void*) * 6 + 0);
static_assert(offsetof(XpUiReaderRows, window_capacity) == sizeof(void*) * 6 + 2);
static_assert(offsetof(XpUiReaderRows, generation) == sizeof(void*) * 6 + 4);
static_assert(sizeof(XpUiReaderToolbarInput) == 8);
static_assert(alignof(XpUiReaderToolbarInput) == 2);
static_assert(offsetof(XpUiReaderToolbarInput, active) == 0);
static_assert(offsetof(XpUiReaderToolbarInput, last_drag_x) == 2);
static_assert(offsetof(XpUiReaderToolbarInput, contact_held) == 4);
static_assert(offsetof(XpUiReaderToolbarInput, reserved) == 5);
static_assert(sizeof(XpUiReaderToolbarModel) == 24);
static_assert(alignof(XpUiReaderToolbarModel) == 4);
static_assert(offsetof(XpUiReaderToolbarModel, panel) == 0);
static_assert(offsetof(XpUiReaderToolbarModel, dense_rows) == 1);
static_assert(offsetof(XpUiReaderToolbarModel, reserved) == 2);
static_assert(offsetof(XpUiReaderToolbarModel, active_tool) == 4);
static_assert(offsetof(XpUiReaderToolbarModel, progress_permille) == 8);
static_assert(offsetof(XpUiReaderToolbarModel, chapter_title) == 12);
static_assert(offsetof(XpUiReaderToolbarModel, page_info) == 16);
static_assert(offsetof(XpUiReaderToolbarModel, panel_title) == 20);
#endif
