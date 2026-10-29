#pragma once
#include <stdint.h>

#include "xp_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Borrowed, synchronous calls; no allocation, retained pointers or reentry.
 * State uses the existing activity's RenderLock. No shared runtime ABI changes. */
typedef struct {
  uint32_t anchor_ms;
  int64_t anchor_epoch, start_epoch;
  uint32_t duration;
  uint16_t anchor_progress, start_progress, end_progress;
  uint8_t has_anchor, pending_forward;
} XpUiReaderSession;
void xp_ui_reader_session_note(XpUiReaderSession*, uint8_t forward, uint8_t succeeded);
void xp_ui_reader_session_render(XpUiReaderSession*, uint32_t now_ms, int64_t epoch, int32_t progress);
uint8_t xp_ui_reader_session_worthy(const XpUiReaderSession*);

typedef struct {
  uint16_t spine, page, count;
  uint8_t has_count, has_offset;
  uint32_t offset;
} XpUiReaderProgress;
/* Returns 0 for invalid input, otherwise 6 or 10 bytes written to out[10]. */
uint32_t xp_ui_reader_progress_encode(int32_t spine, int32_t page, int32_t count, uint8_t has_offset, uint32_t offset,
                                      uint8_t* out);
/* 0 invalid, 1 loaded, 2 loaded with stale page sentinel reset; invalid leaves out unchanged. */
uint8_t xp_ui_reader_progress_decode(const uint8_t* data, uint32_t len, XpUiReaderProgress* out);
uint8_t xp_ui_reader_remote_allowed(uint8_t shown, uint8_t rendering, uint8_t input_over_page);
uint8_t xp_ui_reader_build_allowed(uint8_t running, uint8_t stopping, uint8_t starting);
typedef struct {
  void* context;
  uint8_t (*ready)(void*);
  uint32_t (*now)(void*);
  void (*sleep)(void*, uint32_t);
} XpUiReaderBuildWait;
uint8_t xp_ui_reader_wait_build(const XpUiReaderBuildWait*, uint32_t wait_ms, uint32_t step_ms);
/* Returns 1 for HALF, 0 for FAST; advances the existing counter. */
uint8_t xp_ui_reader_refresh(int32_t* remaining, int32_t frequency);
typedef struct {
  uint8_t drawn_in_title, drawn;
} XpUiReaderLinkNote;
uint8_t xp_ui_reader_link_draw(XpUiReaderLinkNote*, uint8_t shown, uint8_t note);
uint8_t xp_ui_reader_link_repaint(XpUiReaderLinkNote*, uint8_t note);
typedef enum {
  XP_UI_READER_MOVE_NONE = 0,
  XP_UI_READER_MOVE_PAGE = 1,
  XP_UI_READER_MOVE_CHAPTER = 2,
  XP_UI_READER_MOVE_END = 3
} XpUiReaderMoveKind;
typedef struct {
  int32_t value; /* page or spine, selected by kind */
  uint8_t kind, last_page;
  uint8_t reserved[2];
} XpUiReaderMove;
XpUiReaderMove xp_ui_reader_turn(int32_t spine, int32_t spine_count, int32_t page, uint16_t page_count,
                                 uint8_t building, uint8_t forward);
XpUiReaderMove xp_ui_reader_skip(int32_t spine, int32_t page, int32_t amount);
/* The C++ shell applies the selected resource operation at its original lock point,
 * and samples millis only after a successful turn. No callback or allocation here. */

typedef struct {
  void* context;
  uint8_t (*open_temp)(void*);
  uint32_t (*write_temp)(void*, const uint8_t*, uint32_t);
  void (*flush_temp)(void*);
  void (*close_temp)(void*);
  void (*remove_final)(void*);
  uint8_t (*rename_temp)(void*);
} XpUiReaderProgressIo;
typedef enum {
  XP_UI_READER_PROGRESS_OK = 0,
  XP_UI_READER_PROGRESS_OPEN_FAILED = 1,
  XP_UI_READER_PROGRESS_SHORT_WRITE = 2,
  XP_UI_READER_PROGRESS_RENAME_FAILED = 3,
  XP_UI_READER_PROGRESS_INVALID = 4
} XpUiReaderProgressStatus;
/* All callbacks required; bytes/services borrowed synchronously and never retained.
 * A failed open/write returns before close: caller logs then destroys HalFile,
 * preserving the original ordering. On success, flush/close precede remove/rename.
 * written receives the attempted write count (zero when no write was attempted). */
uint8_t xp_ui_reader_write_progress(const XpUiReaderProgressIo*, const uint8_t* data, uint32_t len, uint32_t* written);

typedef struct {
  int32_t spine, page;
} XpUiReaderLinkPosition;
/* out has 13 bytes, positions has three records; returns 0 for depth >3. */
uint32_t xp_ui_reader_links_encode(const XpUiReaderLinkPosition* positions, uint8_t depth, uint8_t* out);
/* Returns accepted depth or 0. For valid length/depth, writes every decoded record
 * even if one spine is invalid, matching the original abandoned-stack state.
 * For bad length/depth leaves positions untouched. */
uint8_t xp_ui_reader_links_decode(const uint8_t* data, uint32_t len, int32_t spine_count,
                                  XpUiReaderLinkPosition* positions);

/* Addition to xp_ui_reader.h. Include xp_ui.h for its existing button masks and ListState. */
typedef enum { XP_UI_READER_BUTTON_PRESS, XP_UI_READER_BUTTON_RELEASE, XP_UI_READER_BUTTON_LONG } XpUiReaderButtonQuery;
typedef enum {
  XP_UI_READER_INPUT_HAS_TOUCH,
  XP_UI_READER_INPUT_SWIPE,
  XP_UI_READER_INPUT_HELD_MS,
  XP_UI_READER_INPUT_TOUCH_HELD_MS,
  XP_UI_READER_INPUT_TILT_NEXT,
  XP_UI_READER_INPUT_TILT_PREV,
  XP_UI_READER_INPUT_NAV_SWAPPED,
  XP_UI_READER_INPUT_HOME_ACTION,
  XP_UI_READER_INPUT_BACK_GESTURE,
  XP_UI_READER_INPUT_MENU_GESTURE,
  XP_UI_READER_INPUT_MENU_SWIPE_UP
} XpUiReaderInputQuery;
/* Borrowed synchronous probes of the existing mapped frame, never hardware polling.
 * LONG and tap probes retain their original short-circuit evaluation order: LONG
 * suppresses a following release, and tap records a held-time override. */
typedef struct {
  void* context;
  uint8_t (*button)(void*, uint32_t button_mask, uint8_t query, uint32_t threshold_ms);
  uint32_t (*read)(void*, uint8_t query);
  uint8_t (*tap)(void*, int32_t* x, int32_t* y);
} XpUiReaderInputServices;
/* Persisted setting values, with C++ static_asserts against CrossPointSettings. */
typedef struct {
  uint8_t long_press, short_power, tilt, touch_controls;
  uint8_t next_gesture, prev_gesture, menu_gesture, back_short_browser;
} XpUiReaderInputConfig;
typedef struct {
  uint32_t held_ms;
  uint8_t prev, next, from_tilt, reserved;
} XpUiReaderTurnInput;
XpUiReaderTurnInput xp_ui_reader_buttons(const XpUiReaderInputServices*, const XpUiReaderInputConfig*);
XpUiReaderTurnInput xp_ui_reader_touch(const XpUiReaderInputServices*, const XpUiReaderInputConfig*, int32_t width,
                                       int32_t height, uint8_t rtl);
uint8_t xp_ui_reader_menu_gesture(const XpUiReaderInputServices*, uint8_t menu_gesture, int32_t width, int32_t height);
/* 0 unhandled, 1 file browser, 2 Home. C++ applies navigation after this returns. */
uint8_t xp_ui_reader_back(const XpUiReaderInputServices*, uint8_t back_short_browser);
/* Flags: bit 0 tap allowed, bit 1 swipe allowed. */
uint8_t xp_ui_reader_gesture(uint8_t persisted_gesture);
uint8_t xp_ui_reader_language_rtl(const uint8_t* tag, uint32_t length);

typedef struct {
  int32_t display_page;
  float fraction;
  uint8_t has_total, reserved[3];
} XpUiReaderChapterDisplay;
XpUiReaderChapterDisplay xp_ui_reader_chapter_display(int32_t page, int32_t total);
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

/* Reader page-render sequencing. Rust issues one synchronous operation at a
 * time; the callback executes an existing renderer/storage/allocator leaf.
 * No new page renderer, task, scheduler, retained callback or allocation. */
typedef enum {
  XP_UI_READER_RENDER_NOW,
  XP_UI_READER_RENDER_HAS_IMAGES,
  XP_UI_READER_RENDER_IMAGES_NEED_DECODE,
  XP_UI_READER_RENDER_TAKE_FORCED,
  XP_UI_READER_RENDER_COUNTER,
  XP_UI_READER_RENDER_FREQUENCY,
  XP_UI_READER_RENDER_TEXT_AA,
  XP_UI_READER_RENDER_IS_A4,
  XP_UI_READER_RENDER_IS_X3,
  XP_UI_READER_RENDER_IS_UC8279,
  XP_UI_READER_RENDER_ABSOLUTE_SUPPORTED,
  XP_UI_READER_RENDER_OVERLAY_CAPS,
  XP_UI_READER_RENDER_ABSOLUTE_CAPS,
  XP_UI_READER_RENDER_STRIP_BOARD,
  XP_UI_READER_RENDER_ELEMENTS,
  XP_UI_READER_RENDER_WIDTH_BYTES,
  XP_UI_READER_RENDER_HEIGHT,
  XP_UI_READER_RENDER_FREE_HEAP,
  XP_UI_READER_RENDER_MAX_BLOCK
} XpUiReaderRenderQuery;
/* Capability bits returned by *_CAPS: STRIP=1, COMBINED=2, ASYNC_BASE=4. */
typedef enum {
  XP_UI_READER_RENDER_IMAGE_RESET,
  XP_UI_READER_RENDER_PREWARM_BEGIN,
  XP_UI_READER_RENDER_PAGE,
  XP_UI_READER_RENDER_STATUS,
  XP_UI_READER_RENDER_PREWARM_END,
  XP_UI_READER_RENDER_HIGHLIGHTS,
  XP_UI_READER_RENDER_IMAGES,
  XP_UI_READER_RENDER_PLACEHOLDERS,
  XP_UI_READER_RENDER_CLEAR,
  XP_UI_READER_RENDER_DISPLAY,
  XP_UI_READER_RENDER_DISPLAY_ASYNC,
  XP_UI_READER_RENDER_GRAY_BASE,
  XP_UI_READER_RENDER_ABSOLUTE_BASE,
  XP_UI_READER_RENDER_PRECONDITION,
  XP_UI_READER_RENDER_SET_COUNTER,
  XP_UI_READER_RENDER_PREPARE_STRIPS,
  XP_UI_READER_RENDER_IMAGE_WINDOW,
  XP_UI_READER_RENDER_RECORD_BEGIN,
  XP_UI_READER_RENDER_RECORD_END,
  XP_UI_READER_RENDER_SET_SOURCE,
  XP_UI_READER_RENDER_CLEAR_PLAN,
  XP_UI_READER_RENDER_CLEAR_WINDOW,
  XP_UI_READER_RENDER_RELEASE_STRIPS,
  XP_UI_READER_RENDER_ALLOCATE,
  XP_UI_READER_RENDER_BORROW_SCRATCH,
  XP_UI_READER_RENDER_MODE,
  XP_UI_READER_RENDER_STRIP_BEGIN,
  XP_UI_READER_RENDER_STRIP_END,
  XP_UI_READER_RENDER_WAIT,
  XP_UI_READER_RENDER_WRITE_PLANE,
  XP_UI_READER_RENDER_GRAY_DISPLAY,
  XP_UI_READER_RENDER_CLEANUP,
  XP_UI_READER_RENDER_STORE_BW,
  XP_UI_READER_RENDER_RESTORE_BW,
  XP_UI_READER_RENDER_COPY_LSB,
  XP_UI_READER_RENDER_COPY_MSB,
  XP_UI_READER_RENDER_LOG,
  XP_UI_READER_RENDER_MARK
} XpUiReaderRenderOperation;
typedef enum { XP_UI_READER_BUFFER_LSB, XP_UI_READER_BUFFER_MSB, XP_UI_READER_BUFFER_SCRATCH } XpUiReaderRenderBuffer;
typedef enum {
  XP_UI_READER_RENDER_STRIP_OOM,
  XP_UI_READER_RENDER_ABSOLUTE_FAILED,
  XP_UI_READER_RENDER_ABSOLUTE_QUALITY,
  XP_UI_READER_RENDER_SCRATCH_OOM,
  XP_UI_READER_RENDER_STORE_FAILED,
  XP_UI_READER_RENDER_TIMING_ASYNC,
  XP_UI_READER_RENDER_TIMING_TILED,
  XP_UI_READER_RENDER_TIMING_GRAY,
  XP_UI_READER_RENDER_TIMING_BW
} XpUiReaderRenderLog;
typedef struct {
  uint8_t kind, buffer, mode, reserved;
  uint16_t y, rows;
  uint32_t offset, value;
} XpUiReaderRenderOp;
/* mode uses XpUiRefresh for display operations, 0 BW/1 LSB/2 MSB for MODE.
 * value carries allocation bytes, counter, log/mark ID or clear fill.
 * MARK stores a timestamp in the C++ diagnostic context; LOG only formats the
 * original statement. Rust decides all conditions and operation ordering.
 * Results: allocation/store/base/prepare/source operations return success;
 * RECORD_END returns page-plan validity; other operations return zero. */
typedef struct {
  void* context;
  uint32_t (*read)(void*, uint8_t query);
  uint32_t (*execute)(void*, const XpUiReaderRenderOp*);
} XpUiReaderRenderServices;
/* Called with the original RenderLock held. Resource context owns the original
 * scoped buffers/guards, whose cleanup runs after this synchronous plan returns.
 * Query/operation output pointers must not be retained. The operation borrows
 * existing Rust page/strip execution through the C++ resource binding. */
void xp_ui_reader_render_page(const XpUiReaderRenderServices*, uint8_t strip_enabled);

#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
#include <cstddef>
static_assert(sizeof(XpUiReaderSession) == 40 && alignof(XpUiReaderSession) == 8);
static_assert(offsetof(XpUiReaderSession, anchor_ms) == 0);
static_assert(offsetof(XpUiReaderSession, anchor_epoch) == 8);
static_assert(offsetof(XpUiReaderSession, start_epoch) == 16);
static_assert(offsetof(XpUiReaderSession, duration) == 24);
static_assert(offsetof(XpUiReaderSession, anchor_progress) == 28);
static_assert(offsetof(XpUiReaderSession, start_progress) == 30);
static_assert(offsetof(XpUiReaderSession, end_progress) == 32);
static_assert(offsetof(XpUiReaderSession, has_anchor) == 34);
static_assert(offsetof(XpUiReaderSession, pending_forward) == 35);
static_assert(sizeof(XpUiReaderProgress) == 12 && alignof(XpUiReaderProgress) == 4);
static_assert(offsetof(XpUiReaderProgress, spine) == 0);
static_assert(offsetof(XpUiReaderProgress, page) == 2);
static_assert(offsetof(XpUiReaderProgress, count) == 4);
static_assert(offsetof(XpUiReaderProgress, has_count) == 6);
static_assert(offsetof(XpUiReaderProgress, has_offset) == 7);
static_assert(offsetof(XpUiReaderProgress, offset) == 8);
static_assert(sizeof(XpUiReaderLinkNote) == 2 && alignof(XpUiReaderLinkNote) == 1);
static_assert(offsetof(XpUiReaderLinkNote, drawn_in_title) == 0);
static_assert(offsetof(XpUiReaderLinkNote, drawn) == 1);
static_assert(sizeof(XpUiReaderBuildWait) == 4 * sizeof(void*));
static_assert(alignof(XpUiReaderBuildWait) == alignof(void*));
static_assert(offsetof(XpUiReaderBuildWait, ready) == sizeof(void*));
static_assert(offsetof(XpUiReaderBuildWait, now) == 2 * sizeof(void*));
static_assert(offsetof(XpUiReaderBuildWait, sleep) == 3 * sizeof(void*));
#endif

#ifdef __cplusplus
static_assert(sizeof(XpUiReaderMove) == 8 && alignof(XpUiReaderMove) == 4);
static_assert(offsetof(XpUiReaderMove, value) == 0 && offsetof(XpUiReaderMove, kind) == 4);
static_assert(offsetof(XpUiReaderMove, last_page) == 5 && offsetof(XpUiReaderMove, reserved) == 6);
static_assert(sizeof(XpUiReaderLinkPosition) == 8 && alignof(XpUiReaderLinkPosition) == 4);
static_assert(offsetof(XpUiReaderLinkPosition, spine) == 0 && offsetof(XpUiReaderLinkPosition, page) == 4);
static_assert(sizeof(XpUiReaderProgressIo) == 7 * sizeof(void*));
static_assert(alignof(XpUiReaderProgressIo) == alignof(void*));
static_assert(offsetof(XpUiReaderProgressIo, context) == 0);
static_assert(offsetof(XpUiReaderProgressIo, open_temp) == sizeof(void*));
static_assert(offsetof(XpUiReaderProgressIo, write_temp) == 2 * sizeof(void*));
static_assert(offsetof(XpUiReaderProgressIo, flush_temp) == 3 * sizeof(void*));
static_assert(offsetof(XpUiReaderProgressIo, close_temp) == 4 * sizeof(void*));
static_assert(offsetof(XpUiReaderProgressIo, remove_final) == 5 * sizeof(void*));
static_assert(offsetof(XpUiReaderProgressIo, rename_temp) == 6 * sizeof(void*));
#endif

#ifdef __cplusplus
static_assert(sizeof(XpUiReaderInputServices) == 4 * sizeof(void*));
static_assert(alignof(XpUiReaderInputServices) == alignof(void*));
static_assert(offsetof(XpUiReaderInputServices, context) == 0);
static_assert(offsetof(XpUiReaderInputServices, button) == sizeof(void*));
static_assert(offsetof(XpUiReaderInputServices, read) == 2 * sizeof(void*));
static_assert(offsetof(XpUiReaderInputServices, tap) == 3 * sizeof(void*));
static_assert(sizeof(XpUiReaderInputConfig) == 8 && alignof(XpUiReaderInputConfig) == 1);
static_assert(offsetof(XpUiReaderInputConfig, long_press) == 0);
static_assert(offsetof(XpUiReaderInputConfig, short_power) == 1);
static_assert(offsetof(XpUiReaderInputConfig, tilt) == 2);
static_assert(offsetof(XpUiReaderInputConfig, touch_controls) == 3);
static_assert(offsetof(XpUiReaderInputConfig, next_gesture) == 4);
static_assert(offsetof(XpUiReaderInputConfig, prev_gesture) == 5);
static_assert(offsetof(XpUiReaderInputConfig, menu_gesture) == 6);
static_assert(offsetof(XpUiReaderInputConfig, back_short_browser) == 7);
static_assert(sizeof(XpUiReaderTurnInput) == 8 && alignof(XpUiReaderTurnInput) == 4);
static_assert(offsetof(XpUiReaderTurnInput, held_ms) == 0);
static_assert(offsetof(XpUiReaderTurnInput, prev) == 4 && offsetof(XpUiReaderTurnInput, next) == 5);
static_assert(offsetof(XpUiReaderTurnInput, from_tilt) == 6 && offsetof(XpUiReaderTurnInput, reserved) == 7);
static_assert(sizeof(XpUiReaderChapterDisplay) == 12 && alignof(XpUiReaderChapterDisplay) == 4);
static_assert(offsetof(XpUiReaderChapterDisplay, display_page) == 0);
static_assert(offsetof(XpUiReaderChapterDisplay, fraction) == 4);
static_assert(offsetof(XpUiReaderChapterDisplay, has_total) == 8 && offsetof(XpUiReaderChapterDisplay, reserved) == 9);
#endif

#ifdef __cplusplus
static_assert(sizeof(XpUiReaderRenderOp) == 16 && alignof(XpUiReaderRenderOp) == 4);
static_assert(offsetof(XpUiReaderRenderOp, kind) == 0);
static_assert(offsetof(XpUiReaderRenderOp, buffer) == 1);
static_assert(offsetof(XpUiReaderRenderOp, mode) == 2);
static_assert(offsetof(XpUiReaderRenderOp, reserved) == 3);
static_assert(offsetof(XpUiReaderRenderOp, y) == 4);
static_assert(offsetof(XpUiReaderRenderOp, rows) == 6);
static_assert(offsetof(XpUiReaderRenderOp, offset) == 8);
static_assert(offsetof(XpUiReaderRenderOp, value) == 12);
static_assert(sizeof(XpUiReaderRenderServices) == 3 * sizeof(void*));
static_assert(alignof(XpUiReaderRenderServices) == alignof(void*));
static_assert(offsetof(XpUiReaderRenderServices, context) == 0);
static_assert(offsetof(XpUiReaderRenderServices, read) == sizeof(void*));
static_assert(offsetof(XpUiReaderRenderServices, execute) == 2 * sizeof(void*));
#endif
