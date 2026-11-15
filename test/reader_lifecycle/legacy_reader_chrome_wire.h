// Historical native Menu/Toolbar wire ABI for host oracles only.
#pragma once
#include <xp_ui_reader.h>
#include <xp_ui_reader_chrome.h>
#ifdef __cplusplus
extern "C" {
#endif
void xp_ui_reader_list_select(XpUiReaderListState*, int32_t selected, uint16_t count);
XpUiInterest xp_ui_reader_list_interest(const XpUiReaderListState*, uint8_t kind, uint32_t now_ms);
// Extra native classifier facts preserve the existing long-press and swipe
// ownership without adding another hardware poll or a second classifier.
XpUiEffect xp_ui_reader_list_update(XpUiReaderListState*, const XpUiInput*, uint16_t count, uint8_t present,
                                    uint8_t kind, uint8_t long_press, int32_t swipe_row);

/* ReaderToolbarUi replaces ListNav with the existing XpUiListState. These keep
 * the old immediate viewport scroll/clamp and pageRows fallback exactly. */
void xp_ui_reader_toolbar_nav_reset(XpUiListState*);
int32_t xp_ui_reader_toolbar_page_rows(const XpUiListState*);
uint8_t xp_ui_reader_toolbar_scroll(XpUiListState*, int32_t delta, int32_t count);
// POD-only, synchronous, O(1), allocation-free and without logging. Null pointers are ignored.
// The parent holds its short model lock. Snapshot before layout; publish after successful paint.
// Both states must be disjoint. A reset/scroll increments the 32-bit navigation_revision modulo
// 2^32; publication accepts selected/top and row counts only if the revision still matches.
// Exactly 2^32 changes during one layout can produce a false match; this wrap case is accepted.
void xp_ui_reader_toolbar_nav_publish(XpUiListState* state, const XpUiListState* painted);

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
#ifdef __cplusplus
}
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
