#pragma once
#include <stdint.h>

#include "xp_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Borrowed, synchronous calls with caller-owned state and no Rust heap owner.
 * Scalar and fixed-size POD operations are O(1), with no callbacks, allocation
 * or I/O, and are safe under ModelLock. The caller synchronizes mutable state and supplies
 * valid, aligned, non-overlapping buffers for every non-null pointer argument.
 * Callback groups run on the calling activity task and may allocate, free, block
 * or perform storage I/O. Never call a callback group under ModelLock, and never
 * reenter an operation while that operation borrows the same mutable context.
 * No pointer is retained. Each group specifies its null/missing-service result. */
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
/* Callback group: radio/clock/delay leaves, outside ModelLock. All slots required;
 * null services or a missing slot returns false without invoking any callback. */
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

/* Callback group: storage leaves, outside ModelLock; may allocate/free and block. */
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
 * close_temp must close and release the owned file implementation before remove/rename.
 * Null/missing services or invalid input return INVALID without resource callbacks.
 * written is optional and receives the attempted count, zero before any write. */
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
/* Callback group: mapped-frame probes, outside ModelLock; no hardware polling.
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
/* Read-only byte-span scan, O(length); call outside ModelLock. Null tag returns false. */
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

/* Callback group: page-render sequencing, outside ModelLock. Rust issues one
 * synchronous operation at a time; renderer/storage/allocator leaves keep their
 * existing allocation and locking behavior. Null/missing services do nothing.
 * No new page renderer, task, scheduler, retained callback or Rust heap owner. */
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

typedef enum {
  XP_UI_READER_REFRESH_MANUAL = 1,
  XP_UI_READER_REFRESH_COUNTER_ONLY = 2,
  XP_UI_READER_REFRESH_REQUEST_MASK = 3
} XpUiReaderRefreshRequest;
/* refresh_requests uses only the named bits; the other six bits must be zero.
 * Counter-only requests do not imply manual refresh. Both are consumed by begin. */
typedef struct {
  XpUiReaderSession session;
  int32_t pages_until_full_refresh;
  uint8_t refresh_requests, end_ready, page_rendered, book_remembered;
} XpUiReaderBaseModel;
/* Persisted IDs; resource bindings assert against their C++ providers. */
typedef enum {
  XP_UI_READER_VALUE_HOME_FOOTNOTES = 4,
  XP_UI_READER_VALUE_HOME_SYNC = 6,
  XP_UI_READER_VALUE_HOME_BOOKMARK = 7,
  XP_UI_READER_VALUE_HOME_DICTIONARY = 8,
  XP_UI_READER_VALUE_HOME_READER_MENU = 9,
  XP_UI_READER_VALUE_HOME_CREATE_CLIPPING = 11,
  XP_UI_READER_VALUE_LONG_MENU_SYNC = 0,
  XP_UI_READER_VALUE_LONG_MENU_BOOKMARK = 2,
  XP_UI_READER_VALUE_LONG_MENU_DICTIONARY = 3,
  XP_UI_READER_VALUE_LONG_MENU_CREATE_CLIPPING = 5,
  XP_UI_READER_VALUE_LONG_MENU_FOOTNOTES = 6,
  XP_UI_READER_VALUE_POWER_FOOTNOTES = 4,
  XP_UI_READER_VALUE_POWER_CREATE_CLIPPING = 6,
  XP_UI_READER_VALUE_LONG_BUTTON_SKIP = 1,
  XP_UI_READER_VALUE_LONG_BUTTON_ORIENTATION = 2,
  XP_UI_READER_VALUE_OVERLAY_NONE = 0,
  XP_UI_READER_VALUE_OVERLAY_TOOLBAR = 1,
  XP_UI_READER_VALUE_END_REDRAW = 1,
  XP_UI_READER_VALUE_END_OPEN_BOOK = 2,
  XP_UI_READER_VALUE_END_GO_HOME = 3,
  XP_UI_READER_VALUE_END_LAST_PAGE = 4
} XpUiReaderValue;
typedef enum {
  /* boolean book-open side effects already emitted. Signed values use uint32 bit representation. */
  XP_UI_READER_BASE_FIELD_REMEMBERED = 0,
  /* boolean completed-page publication. Signed values use uint32 bit representation. */
  XP_UI_READER_BASE_FIELD_RENDERED = 1,
  /* boolean child resource publication. Signed values use uint32 bit representation. */
  XP_UI_READER_BASE_FIELD_END_READY = 2,
  /* Signed pages remaining before HALF refresh; writing atomically queues counter-only bit 1. */
  XP_UI_READER_BASE_FIELD_COUNTER = 3,
  /* Boolean manual-refresh bit only; writing preserves the counter-only request bit. */
  XP_UI_READER_BASE_FIELD_FORCED = 4
} XpUiReaderBaseField;
typedef enum {
  /* returns end-of-book flag; resource read only. No a/b parameters. */
  XP_UI_READER_BASE_QUERY_AT_END = 0,
  /* returns whether the end-book resource exists; pointer read only. No a/b parameters. */
  XP_UI_READER_BASE_QUERY_HAS_END = 1,
  /* returns child menu-active flag; child model read only. No a/b parameters. */
  XP_UI_READER_BASE_QUERY_END_MENU = 2,
  /* returns path existence; may read storage and log missing path. No a/b parameters. */
  XP_UI_READER_BASE_QUERY_EXISTS = 3,
  /* returns persisted open-path emptiness; string read only. No a/b parameters. */
  XP_UI_READER_BASE_QUERY_OPEN_PATH_EMPTY = 4,
  /* returns whether exit event has subscribers; registry read only. No a/b parameters. */
  XP_UI_READER_BASE_QUERY_SUBSCRIBER_EXIT = 5,
  /* returns session emit eligibility; POD read only. No a/b parameters. */
  XP_UI_READER_BASE_QUERY_SESSION_WORTHY = 6,
  /* returns whether session event has subscribers; registry read only. No a/b parameters. */
  XP_UI_READER_BASE_QUERY_SUBSCRIBER_SESSION = 7,
  /* returns mapped-frame held milliseconds; no polling. No a/b parameters. */
  XP_UI_READER_BASE_QUERY_HELD = 8,
  /* returns persisted long-button behavior ID; settings read only. No a/b parameters. */
  XP_UI_READER_BASE_QUERY_LONG_BUTTON = 9
} XpUiReaderBaseQuery;
typedef enum {
  /* a/b ignored; return ignored; notifies existing render task. */
  XP_UI_READER_BASE_COMMAND_UPDATE = 0,
  /* a/b ignored; return ignored; requests existing activity finish. */
  XP_UI_READER_BASE_COMMAND_FINISH = 1,
  /* a/b ignored; return ignored; runs Activity entry; may log/lock. */
  XP_UI_READER_BASE_COMMAND_BASE_ENTER = 2,
  /* a/b ignored; return ignored; runs Activity exit; may log/lock. */
  XP_UI_READER_BASE_COMMAND_BASE_EXIT = 3,
  /* a/b ignored; return ignored; reads heap and logs; allocator lock possible. */
  XP_UI_READER_BASE_COMMAND_LOG_ENTER = 4,
  /* a/b ignored; return ignored; reads heap and logs; allocator lock possible. */
  XP_UI_READER_BASE_COMMAND_LOG_EXIT = 5,
  /* a/b ignored; return ignored; clears existing path content without releasing capacity. */
  XP_UI_READER_BASE_COMMAND_CLEAR_PATH = 6,
  /* a/b ignored; return ignored; persists app state; may allocate/log/block on storage. */
  XP_UI_READER_BASE_COMMAND_SAVE_STATE = 7,
  /* a/b ignored; return ignored; loads existing font resources; may allocate/log/block. */
  XP_UI_READER_BASE_COMMAND_ENSURE_FONTS = 8,
  /* a/b ignored; return ignored; applies existing orientation; may lock/rebuild resources. */
  XP_UI_READER_BASE_COMMAND_ORIENTATION_INITIAL = 9,
  /* a/b ignored; returns load success; may allocate/log/block on book storage. */
  XP_UI_READER_BASE_COMMAND_LOAD_BOOK = 10,
  /* a/b ignored; returns failure handled flag; may allocate/log/navigate/show popup. */
  XP_UI_READER_BASE_COMMAND_LOAD_FAILURE = 11,
  /* a/b ignored; return ignored; copies book path; may allocate/free. */
  XP_UI_READER_BASE_COMMAND_REMEMBER_PATH = 12,
  /* a/b ignored; return ignored; updates recent-book store; may allocate/log/block. */
  XP_UI_READER_BASE_COMMAND_RECENT_ADD = 13,
  /* a/b ignored; return ignored; invokes plugin event subscribers; may allocate/log/block. */
  XP_UI_READER_BASE_COMMAND_EMIT_OPEN = 14,
  /* a/b ignored; return ignored; releases optional font cache; may free and take allocator locks. */
  XP_UI_READER_BASE_COMMAND_RELEASE_FONTS = 15,
  /* a/b ignored; return ignored; resets Rust session POD only. */
  XP_UI_READER_BASE_COMMAND_SESSION_RESET = 16,
  /* a/b ignored; returns document byte length; creates scoped document ID; may allocate/log/read storage. */
  XP_UI_READER_BASE_COMMAND_DOCUMENT = 17,
  /* a/b ignored; return ignored; formats existing session scalars and emits event; subscribers may allocate/log/block.
   */
  XP_UI_READER_BASE_COMMAND_EMIT_SESSION = 18,
  /* a/b ignored; return ignored; formats progress and emits event; subscribers may allocate/log/block. */
  XP_UI_READER_BASE_COMMAND_EMIT_EXIT = 19,
  /* a/b ignored; return ignored; changes renderer orientation; may invalidate resources. */
  XP_UI_READER_BASE_COMMAND_ORIENTATION_PORTRAIT = 20,
  /* a/b ignored; return ignored; writes app-state scalar only. */
  XP_UI_READER_BASE_COMMAND_RESET_LOAD_COUNT = 21,
  /* a/b ignored; return ignored; destroys end-book resource; may free and take allocator locks. */
  XP_UI_READER_BASE_COMMAND_END_DROP = 22,
  /* a/b ignored; return ignored; fallibly allocates end-book resource. */
  XP_UI_READER_BASE_COMMAND_END_ALLOCATE = 23,
  /* a/b ignored; return ignored; logs allocation failure. */
  XP_UI_READER_BASE_COMMAND_END_OOM = 24,
  /* a/b ignored; return ignored; loads child suggestions; may allocate/log/read storage. */
  XP_UI_READER_BASE_COMMAND_END_LOAD = 25,
  /* a/b ignored; return ignored; paints child scene; may allocate/log/block. */
  XP_UI_READER_BASE_COMMAND_END_RENDER = 26,
  /* a/b ignored; returns EndOfBookOptions Action ID; creates scoped output path and dispatches child input; may
     allocate. */
  XP_UI_READER_BASE_COMMAND_END_INPUT = 27,
  /* a/b ignored; return ignored; navigates using scoped path; may allocate/log/lock/block. */
  XP_UI_READER_BASE_COMMAND_OPEN_NEXT = 28,
  /* a/b ignored; return ignored; runs existing Home navigation; may allocate/log/lock/block. */
  XP_UI_READER_BASE_COMMAND_HOME = 29,
  /* a/b ignored; return ignored; returns to last page; may release resources/lock. */
  XP_UI_READER_BASE_COMMAND_RETURN_END = 30,
  /* a/b ignored; returns consumed flag; runs format-specific input; may allocate/log/lock/block. */
  XP_UI_READER_BASE_COMMAND_FORMAT_INPUT = 31,
  /* a/b ignored; returns consumed flag; runs existing Back navigation; may allocate/log/lock/block. */
  XP_UI_READER_BASE_COMMAND_BACK = 32,
  /* a=forward boolean, b ignored; returns success; runs existing page-turn leaf; may free/log/lock. */
  XP_UI_READER_BASE_COMMAND_TURN = 33,
  /* a=signed page delta, b ignored; returns success; runs existing skip leaf; may free/log/lock. */
  XP_UI_READER_BASE_COMMAND_SKIP = 34,
  /* a=forward boolean, b=success boolean; return ignored; updates session POD under short model lock in production. */
  XP_UI_READER_BASE_COMMAND_NOTE_TURN = 35,
  /* a=skip boolean, b ignored; return ignored; requests existing haptic effect; no new task. */
  XP_UI_READER_BASE_COMMAND_HAPTIC = 36,
  /* a/b ignored; return ignored; clears existing framebuffer. */
  XP_UI_READER_BASE_COMMAND_CLEAR_SCREEN = 37,
  /* a/b ignored; return ignored; pushes existing panel refresh and may block. */
  XP_UI_READER_BASE_COMMAND_DISPLAY = 38,
  /* a/b ignored; return ignored; runs format-specific end-page hook; may paint/log/block. */
  XP_UI_READER_BASE_COMMAND_END_RENDERED = 39,
  /* a/b ignored; return ignored; release-publishes completed-page POD. */
  XP_UI_READER_BASE_COMMAND_MARK_RENDERED = 40,
  /* a/b ignored; return ignored; samples clocks/progress outside critical section then updates session POD. */
  XP_UI_READER_BASE_COMMAND_SESSION_RENDER = 41,
  /* a/b ignored; return ignored; runs existing book render leaf; may allocate/free/log/lock/block. */
  XP_UI_READER_BASE_COMMAND_RENDER_BOOK = 42,
  /* a/b ignored; return ignored; acquires original RenderLock; may block; starts one paired scope. */
  XP_UI_READER_BASE_COMMAND_LOCK = 43,
  /* a/b ignored; return ignored; releases exactly one active paired scope; never called without LOCK. */
  XP_UI_READER_BASE_COMMAND_UNLOCK = 44,
  /* a/b ignored; return ignored; destroys scoped menu path; may free; before caller continuation. */
  XP_UI_READER_BASE_COMMAND_PATH_DROP = 45,
  /* a/b ignored; return ignored; destroys scoped document ID; may free; before caller continuation. */
  XP_UI_READER_BASE_COMMAND_DOCUMENT_DROP = 46,
  /* a=byte index 0..31 after DOCUMENT returned 32, b ignored; returns unsigned byte; reads scoped document byte only.
   */
  XP_UI_READER_BASE_COMMAND_DOCUMENT_BYTE = 47,
  /* a/b ignored; return ignored; atomically queues forced refresh in the base POD model; short ModelLock only. */
  XP_UI_READER_BASE_COMMAND_FORCE_REFRESH = 48
} XpUiReaderBaseCommand;
typedef enum {
  /* Lifecycle entry enter; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns consumed/success
     for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_ENTER = 0,
  /* Lifecycle entry exit; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns consumed/success
     for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_EXIT = 1,
  /* Lifecycle entry remember; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns
     consumed/success for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_REMEMBER = 2,
  /* Lifecycle entry clear_end; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns
     consumed/success for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_CLEAR_END = 3,
  /* Lifecycle entry menu; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns consumed/success
     for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_MENU = 4,
  /* Lifecycle entry end_turn; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns
     consumed/success for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_END_TURN = 5,
  /* Lifecycle entry loop; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns consumed/success
     for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_LOOP = 6,
  /* Lifecycle entry render; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns
     consumed/success for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_RENDER = 7,
  /* Lifecycle entry force_refresh; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns
     consumed/success for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_FORCE_REFRESH = 8,
  /* Lifecycle entry flush; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns consumed/success
     for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_FLUSH = 9,
  /* Lifecycle entry menu_active; a/b ignored except Menu(a=suppress) and EndTurn(a=previous,b=next); returns
     consumed/success for Menu, EndTurn, ForceRefresh, MenuActive. */
  XP_UI_READER_BASE_OPERATION_MENU_ACTIVE = 10
} XpUiReaderBaseOperation;
typedef struct {
  void* context;
  uint32_t (*read)(void*, uint8_t);
  void (*write)(void*, uint8_t, uint32_t);
  uint32_t (*query)(void*, uint8_t);
  uint32_t (*command)(void*, uint8_t, int32_t, int32_t);
  XpUiReaderTurnInput (*touch)(void*);
  XpUiReaderTurnInput (*buttons)(void*);
} XpUiReaderBaseServices;

/* Required synchronous callbacks; context may be null only if every callback accepts it.
 * Model read/write copy fixed POD under ModelLock; all resource callbacks run outside it.
 * LOCK and UNLOCK pair exactly once, and no entry returns with an active resource scope.
 * Null/missing services or an invalid operation return zero without probes or effects. */
uint8_t xp_ui_reader_base_run(const XpUiReaderBaseServices*, uint8_t operation, uint8_t a, uint8_t b);
/* O(1), POD-only transactions; no callbacks, allocation, logging or waiting.
 * Null model is a no-op (read returns zero); caller holds the short ModelLock. */
void xp_ui_reader_base_init(XpUiReaderBaseModel*, uint8_t fast, int32_t frequency);
uint32_t xp_ui_reader_base_model_read(const XpUiReaderBaseModel*, uint8_t field);
void xp_ui_reader_base_model_write(XpUiReaderBaseModel*, uint8_t field, uint32_t value);
/* Additions to the approved 48-byte reader base model; no layout changes.
 * All functions are O(1), POD-only, synchronous, with no callback, allocation,
 * logging or waiting. Call under the existing short ModelLock. Null is a no-op.
 * Begin/publish arguments are disjoint; C++ bindings assert that in debug builds.
 * begin copies the published model, then consumes its pending session turn and
 * forced-refresh request into that render-owned snapshot. publish copies the
 * painted session/ready flags and refresh counter while preserving a turn or
 * forced-refresh request received during painting, and the input-owned remembered
 * flag. Only one render may be active; frame is borrowed until publish returns.
 * Exit/sleep session flush retains the original ActivityManager RenderLock. */
void xp_ui_reader_base_begin_render(XpUiReaderBaseModel* model, XpUiReaderBaseModel* frame);
void xp_ui_reader_base_publish(XpUiReaderBaseModel* model, const XpUiReaderBaseModel* frame);
void xp_ui_reader_base_force_refresh(XpUiReaderBaseModel* model);

typedef enum {
  /* boolean radio released for chapter build. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_RADIO_RELEASED = 0,
  /* orientation ID 0..3. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_APPLIED_ORIENTATION = 1,
  /* wrapping uint32 render-completion millis; zero is unset. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_LAST_RENDERED = 2,
  /* signed zero-based prewarmed spine, -1 unset. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_IDLE_SPINE = 3,
  /* signed zero-based prewarmed page, -1 unset. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_IDLE_PAGE = 4,
  /* signed zero-based current spine. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_SPINE = 5,
  /* uint16 viewport width in pixels. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_BUILD_WIDTH = 6,
  /* uint16 viewport height in pixels. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_BUILD_HEIGHT = 7,
  /* boolean deferred extension failed. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_PARTIAL_FAILED = 8,
  /* boolean book removed from recents. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_RECENTS_REMOVED = 9,
  /* boolean pending finished-book move. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_PENDING_READ_MOVE = 10,
  /* boolean bookmark message visible. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_SHOW_BOOKMARK = 11,
  /* wrapping uint32 bookmark-message start millis. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_BOOKMARK_TIME = 12,
  /* boolean dictionary message visible. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_SHOW_DICTIONARY = 13,
  /* wrapping uint32 dictionary-message start millis. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_DICTIONARY_TIME = 14,
  /* boolean damaged-font message visible. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_SHOW_FONT = 15,
  /* wrapping uint32 damaged-font message start millis. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_FONT_TIME = 16,
  /* overlay ID: 0 none, 1 toolbar, 2 contents, 3 text, 4 more. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_OVERLAY = 17,
  /* wrapping uint32 last-turn millis. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_LAST_TURN = 18,
  /* boolean automatic turns active. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_AUTO_ACTIVE = 19,
  /* signed int8 queued direction: -1 previous, 0 none, 1 next. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_PENDING_TURN = 20,
  /* boolean queued turn originated from touch. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_PENDING_TOUCH = 21,
  /* uint32 automatic-turn interval milliseconds; wrapping clock differences. Signed values use uint32 bit
     representation. */
  XP_UI_READER_LOOP_FIELD_TURN_DURATION = 22,
  /* boolean clipping message visible. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_SHOW_CLIP = 23,
  /* wrapping uint32 clipping-message start millis. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_CLIP_TIME = 24,
  /* signed saved-position depth, 0..3. Signed values use uint32 bit representation. */
  XP_UI_READER_LOOP_FIELD_FOOTNOTE_DEPTH = 25
} XpUiReaderLoopField;
typedef enum {
  /* returns load-failure popup active flag; child model read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_LOAD_POPUP = 0,
  /* returns EPUB resource presence; pointer read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_HAS_EPUB = 1,
  /* returns section resource presence; pointer read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_HAS_SECTION = 2,
  /* returns section build-running flag; resource read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_BUILDING = 3,
  /* returns framebuffer availability; resource read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_FRAMEBUFFER = 4,
  /* returns wrapping uint32 millis; clock read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_NOW = 5,
  /* returns free heap bytes; allocator query may take its internal lock. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_FREE_HEAP = 6,
  /* returns largest allocation bytes; allocator query may take its internal lock. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_MAX_BLOCK = 7,
  /* returns signed zero-based current page bits; resource read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_PAGE = 8,
  /* returns available page count; resource read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_PAGE_COUNT = 9,
  /* returns partial-cache flag; resource read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_PARTIAL = 10,
  /* returns radio heap ownership flag; service read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_RADIO_HEAP = 11,
  /* returns spine item count; resource read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_SPINE_COUNT = 12,
  /* returns remove-read-books setting; read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_REMOVE_RECENTS = 13,
  /* returns move-finished-books setting; read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_MOVE_FINISHED = 14,
  /* returns whether book path is in read folder; may allocate temporary path text. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_IN_READ_FOLDER = 15,
  /* returns persisted orientation ID; read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_ORIENTATION = 16,
  /* returns toolbar-style setting; read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_TOOLBAR_MENU = 17,
  /* returns touch controls enabled setting; read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_TOUCH_CONTROLS = 18,
  /* returns cached Home action ID; no hardware polling. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_HOME_ACTION = 19,
  /* returns touch-menu gesture result; mapped-frame probes may consume tap state. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_TOUCH_MENU = 20,
  /* returns Confirm release edge; mapped-frame read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_CONFIRM_RELEASE = 21,
  /* returns Back release edge; mapped-frame read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_BACK_RELEASE = 22,
  /* returns render-lock busy flag; nonblocking peek. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_BUSY = 23,
  /* returns end-book menu-active flag; child model read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_END_MENU_ACTIVE = 24,
  /* returns long-Confirm threshold milliseconds, zero disabled; settings read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_CONFIRM_THRESHOLD = 25,
  /* returns persisted long-Confirm action ID; settings read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_LONG_MENU_FUNCTION = 26,
  /* returns touch-input capability; read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_HAS_TOUCH = 27,
  /* returns mapped-frame held milliseconds; no polling. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_HELD = 28,
  /* returns persisted short-Power action ID; settings read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_SHORT_POWER = 29,
  /* returns Power release edge; mapped-frame read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_POWER_RELEASE = 30,
  /* returns Down release edge; mapped-frame read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_DOWN_RELEASE = 31,
  /* returns persisted long-button behavior ID; settings read only. No a/b parameters. */
  XP_UI_READER_LOOP_QUERY_LONG_BUTTON_BEHAVIOR = 32
} XpUiReaderLoopQuery;
typedef enum {
  /* a/b ignored; return ignored; dispatches load popup; may allocate/log/navigate/paint. */
  XP_UI_READER_LOOP_COMMAND_POPUP_INPUT = 0,
  /* a/b ignored; return ignored; requests existing activity finish. */
  XP_UI_READER_LOOP_COMMAND_FINISH = 1,
  /* a/b ignored; return ignored; runs remembered-book effects; may allocate/log/block. */
  XP_UI_READER_LOOP_COMMAND_REMEMBER = 2,
  /* a/b ignored; returns acquired flag, but opens a scope even on failure; nonblocking original RenderLock acquisition;
     paired UNLOCK required even if false. */
  XP_UI_READER_LOOP_COMMAND_TRY_LOCK = 3,
  /* a/b ignored; return ignored; destroys exactly one TRY_LOCK scope; debug assertion rejects unmatched release. */
  XP_UI_READER_LOOP_COMMAND_UNLOCK = 4,
  /* a/b ignored; return ignored; notifies existing radio service after paint; may log/schedule. */
  XP_UI_READER_LOOP_COMMAND_RADIO_AFTER = 5,
  /* a/b ignored; returns repaint-needed flag; returns whether link-note repaint is needed; POD update only. */
  XP_UI_READER_LOOP_COMMAND_REPAINT_LINK_NOTE = 6,
  /* a/b ignored; return ignored; notifies existing render task. */
  XP_UI_READER_LOOP_COMMAND_UPDATE = 7,
  /* a=orientation ID 0..3, b ignored; return ignored; applies orientation; may allocate/free/log/lock/block. */
  XP_UI_READER_LOOP_COMMAND_ORIENTATION = 8,
  /* a=zero-based page index, b ignored; returns page presence; loads scoped page resource; may allocate/log/read
     storage. */
  XP_UI_READER_LOOP_COMMAND_LOAD_PAGE = 9,
  /* a/b ignored; returns font-cache presence; borrows cache pointer until PREWARM_DROP; no ownership transfer. */
  XP_UI_READER_LOOP_COMMAND_FONT_CACHE = 10,
  /* a/b ignored; return ignored; constructs scoped prewarm guard; cache may allocate. */
  XP_UI_READER_LOOP_COMMAND_PREWARM_BEGIN = 11,
  /* a/b ignored; return ignored; renders page to prewarm; may allocate/read storage. */
  XP_UI_READER_LOOP_COMMAND_PREWARM_PAGE = 12,
  /* a/b ignored; return ignored; finishes prewarm scan; may allocate/read storage. */
  XP_UI_READER_LOOP_COMMAND_PREWARM_SCAN_END = 13,
  /* a=page index, b=wrapping start-millis bits; return ignored; logs timing; clock argument evaluated only when debug
     log is enabled. */
  XP_UI_READER_LOOP_COMMAND_PREWARM_LOG = 14,
  /* a/b ignored; return ignored; destroys scoped prewarm guard before PAGE_DROP. */
  XP_UI_READER_LOOP_COMMAND_PREWARM_DROP = 15,
  /* a/b ignored; return ignored; destroys scoped page; may free; before UNLOCK. */
  XP_UI_READER_LOOP_COMMAND_PAGE_DROP = 16,
  /* a/b ignored; returns build started flag; uses viewport POD and original section; may allocate/free/log/read
     storage. */
  XP_UI_READER_LOOP_COMMAND_START_BUILD = 17,
  /* a/b ignored; return ignored; logs deferred-build failure. */
  XP_UI_READER_LOOP_COMMAND_BUILD_FAILED_LOG = 18,
  /* a/b ignored; return ignored; logs current page/count after successful start. */
  XP_UI_READER_LOOP_COMMAND_BUILD_STARTED_LOG = 19,
  /* a/b ignored; return ignored; clears stale end-book resource; may free/lock. */
  XP_UI_READER_LOOP_COMMAND_CLEAR_END = 20,
  /* a/b ignored; returns removed flag; updates recent store; may allocate/log/read storage. */
  XP_UI_READER_LOOP_COMMAND_REMOVE_RECENT = 21,
  /* a/b ignored; return ignored; updates recent store; may allocate/log/read storage. */
  XP_UI_READER_LOOP_COMMAND_ADD_RECENT = 22,
  /* a/b ignored; return ignored; dispatches existing overlay input; may allocate/free/log/lock/block. */
  XP_UI_READER_LOOP_COMMAND_OVERLAY_INPUT = 23,
  /* a/b ignored; return ignored; releases page snapshot; may free. */
  XP_UI_READER_LOOP_COMMAND_DISCARD_OVERLAY = 24,
  /* a/b=logical x/y pixels, -1/-1 means no initial point; return ignored; opens selection child; may
     allocate/free/log/lock/block. */
  XP_UI_READER_LOOP_COMMAND_CLIP = 25,
  /* a=forward boolean, b ignored; returns success; runs existing turn leaf; may free/log/lock. */
  XP_UI_READER_LOOP_COMMAND_TURN = 26,
  /* a=forward boolean, b=success boolean; return ignored; updates session POD under short model lock in production. */
  XP_UI_READER_LOOP_COMMAND_NOTE_TURN = 27,
  /* a=skip boolean, b ignored; return ignored; requests existing haptic effect; no new task. */
  XP_UI_READER_LOOP_COMMAND_HAPTIC = 28,
  /* a/b ignored; returns consumed flag; dispatches child end-book input; may allocate/navigate. */
  XP_UI_READER_LOOP_COMMAND_END_MENU = 29,
  /* a=threshold milliseconds, b ignored; returns LONG edge; consumes mapped LONG and suppresses its release; no
     polling. */
  XP_UI_READER_LOOP_COMMAND_CONFIRM_LONG = 30,
  /* a/b ignored; return ignored; updates bookmark store; may allocate/log/lock/read storage. */
  XP_UI_READER_LOOP_COMMAND_BOOKMARK = 31,
  /* a/b ignored; returns sync launched flag; opens sync workflow; may allocate/free/log/lock/block. */
  XP_UI_READER_LOOP_COMMAND_SYNC = 32,
  /* a/b ignored; return ignored; opens dictionary child; may allocate/free/log/lock/block. */
  XP_UI_READER_LOOP_COMMAND_DICTIONARY = 33,
  /* a/b ignored; return ignored; restores saved position; may free/log/lock/read storage. */
  XP_UI_READER_LOOP_COMMAND_RESTORE_POSITION = 34,
  /* a/b ignored; return ignored; opens footnote child; may allocate/free/log/lock/block. */
  XP_UI_READER_LOOP_COMMAND_FOOTNOTES = 35,
  /* a=overlay ID 0..4, b ignored; return ignored; opens existing overlay; may allocate/free/log/lock/block. */
  XP_UI_READER_LOOP_COMMAND_OPEN_OVERLAY = 36,
  /* a/b ignored; return ignored; opens existing menu; may allocate/free/log/lock/block. */
  XP_UI_READER_LOOP_COMMAND_OPEN_MENU = 37,
  /* a/b=logical x/y pixels; returns match flag and stores scoped href; copies href under original resource RenderLock;
     may allocate/lock/block. */
  XP_UI_READER_LOOP_COMMAND_MATCH_LINK = 38,
  /* a/b ignored; return ignored; uses scoped href; may allocate/free/log/lock/read storage. */
  XP_UI_READER_LOOP_COMMAND_NAVIGATE_LINK = 39,
  /* a/b ignored; returns consumed flag; runs existing Back path; may allocate/free/log/lock/block. */
  XP_UI_READER_LOOP_COMMAND_BACK_NAVIGATION = 40,
  /* a/b ignored; return ignored; runs one existing bounded build tick; may allocate/free/log/try-lock/read storage. */
  XP_UI_READER_LOOP_COMMAND_BUILD_TICK = 41,
  /* a=previous boolean, b=next boolean; returns consumed flag; handles end-book page edge; may navigate/allocate. */
  XP_UI_READER_LOOP_COMMAND_END_TURN = 42,
  /* a=signed chapter delta, b ignored; returns success; runs existing chapter skip; may free/log/lock. */
  XP_UI_READER_LOOP_COMMAND_SKIP = 43
} XpUiReaderLoopCommand;
typedef enum {
  /* Mapped long point probe; writes logical x/y pixels and returns presence; may consume mapped-frame gesture, no
     polling. */
  XP_UI_READER_LOOP_POINT_LONG = 0,
  /* Mapped tap point probe; writes logical x/y pixels and returns presence; may consume mapped-frame gesture, no
     polling. */
  XP_UI_READER_LOOP_POINT_TAP = 1
} XpUiReaderLoopPoint;
typedef struct {
  void* context;
  uint32_t (*model_read)(void*, uint8_t);
  void (*model_write)(void*, uint8_t, uint32_t);
  uint32_t (*query)(void*, uint8_t);
  uint32_t (*command)(void*, uint8_t, int32_t, int32_t);
  XpUiReaderTurnInput (*touch)(void*);
  XpUiReaderTurnInput (*buttons)(void*);
  uint8_t (*point)(void*, uint8_t, int32_t*, int32_t*);
} XpUiReaderLoopServices;
/* All seven callbacks are required and synchronous; context may be null only
 * if every callback accepts it. Model access copies scalars under ModelLock.
 * Resource callbacks run outside the critical section. TRY_LOCK opens one scope
 * even on failure; exactly one UNLOCK closes it before any return. Null or
 * incomplete services produce no probes/effects; callbacks retain no arguments. */
void xp_ui_reader_loop_run(const XpUiReaderLoopServices*);

/* Idle admission only while a reader render owns the framebuffer. Nonzero
 * results require preserving the consuming mapped probes in their original order. */
typedef struct {
  int32_t spine, spine_count;
  uint32_t confirm_threshold;
  uint8_t busy, has_epub, load_popup, remembered, end_options, touch, remote, home_action;
  uint8_t edges, held_buttons, overlay, automatic;
  int8_t pending_turn;
  uint8_t bookmark_notice, dictionary_notice, font_notice, clipping_notice, read_move, recents_removed;
  uint8_t applied_orientation, orientation, long_button, short_power, tilt, footnote_depth, touch_controls;
  uint8_t reserved[2];
} XpUiReaderIdleSnapshot;
/* SCREEN_LONG asks the caller to probe wasScreenLongPress before the Back probes. */
enum { XP_UI_READER_IDLE_ADMITTED = 1, XP_UI_READER_IDLE_SCREEN_LONG = 2 };
uint8_t xp_ui_reader_idle_plan(const XpUiReaderIdleSnapshot* snapshot);

/* Stable event IDs; checked against the C++ provider. */
typedef enum {
  XP_UI_READER_OVERLAY_EVENT_NONE = 0,
  XP_UI_READER_OVERLAY_EVENT_DISMISS = 1,
  XP_UI_READER_OVERLAY_EVENT_TOOL = 2,
  XP_UI_READER_OVERLAY_EVENT_PREV_CHAPTER = 3,
  XP_UI_READER_OVERLAY_EVENT_NEXT_CHAPTER = 4,
  XP_UI_READER_OVERLAY_EVENT_SCRUB = 5,
  XP_UI_READER_OVERLAY_EVENT_ROW = 6,
} XpUiReaderOverlayEvent;
/* Stable swipe IDs; checked against the C++ provider. */
typedef enum {
  XP_UI_READER_OVERLAY_SWIPE_NONE = 0,
  XP_UI_READER_OVERLAY_SWIPE_LEFT = 1,
  XP_UI_READER_OVERLAY_SWIPE_RIGHT = 2,
  XP_UI_READER_OVERLAY_SWIPE_UP = 3,
  XP_UI_READER_OVERLAY_SWIPE_DOWN = 4,
} XpUiReaderOverlaySwipe;
/* Stable mode IDs; checked against the C++ provider. */
typedef enum {
  XP_UI_READER_OVERLAY_MODE_NONE = 0,
  XP_UI_READER_OVERLAY_MODE_TOOLBAR = 1,
  XP_UI_READER_OVERLAY_MODE_CONTENTS = 2,
  XP_UI_READER_OVERLAY_MODE_TEXT = 3,
  XP_UI_READER_OVERLAY_MODE_MORE = 4,
} XpUiReaderOverlayMode;
/* Signed fields use uint32 bits; read/write is POD-only. */
typedef enum {
  /* overlay ID 0 none, 1 toolbar, 2 contents, 3 text, 4 more. */
  XP_UI_READER_OVERLAY_FIELD_OVERLAY = 0,
  /* signed zero-based spine index. */
  XP_UI_READER_OVERLAY_FIELD_SPINE = 1,
  /* signed zero-based pending page index. */
  XP_UI_READER_OVERLAY_FIELD_NEXT_PAGE = 2,
  /* signed tool index 0..2. */
  XP_UI_READER_OVERLAY_FIELD_FOCUS = 3,
  /* signed selected panel row; negative is unselected. */
  XP_UI_READER_OVERLAY_FIELD_INDEX = 4,
  /* boolean panel cursor shown. */
  XP_UI_READER_OVERLAY_FIELD_CURSOR = 5,
  /* boolean hold jump already consumed. */
  XP_UI_READER_OVERLAY_FIELD_HOLD = 6,
  /* boolean clean-page framebuffer snapshot exists. */
  XP_UI_READER_OVERLAY_FIELD_STORED = 7,
} XpUiReaderOverlayField;
typedef enum {
  /* Returns toolbar owner presence; resource-pointer read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_HAS_TOOLBAR = 0,
  /* Returns modal picker active flag; child model read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_POPUP_ACTIVE = 1,
  /* Returns positive EPUB spine count; resource read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_SPINE_COUNT = 2,
  /* Returns TOC row count; resource read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_TOC_COUNT = 3,
  /* Returns More row count; vector size read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_MORE_COUNT = 4,
  /* Returns published visible row count; short POD critical section only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_ROWS = 5,
  /* Returns cached route consumed flag; POD read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_ROUTED = 6,
  /* Returns cached signed route row/tool value; POD read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_VALUE = 7,
  /* Returns cached signed scrub position in permille; POD read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_PERMILLE = 8,
  /* Returns cached signed logical route x in pixels; POD read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_X = 9,
  /* Returns cached signed logical route y in pixels; POD read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_Y = 10,
  /* Returns oriented screen width in pixels; renderer read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_WIDTH = 11,
  /* Returns oriented screen height in pixels; renderer read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_HEIGHT = 12,
  /* Returns focus-reading enabled setting; scalar read only. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_FOCUS_READING = 13,
  /* Returns cached XP_UI button bits currently pressed; pure read. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_PRESSED = 14,
  /* Returns cached XP_UI button release bits; pure read. No arguments. */
  XP_UI_READER_OVERLAY_QUERY_RELEASED = 15,
  /* No arguments; returns section presence. No allocation, lock, I/O or paint. */
  XP_UI_READER_OVERLAY_QUERY_HAS_SECTION = 16,
  /* No arguments; returns the atomic stale-framebuffer flag. No allocation, lock, I/O or paint. */
  XP_UI_READER_OVERLAY_QUERY_PAGE_STALE = 17,
  /* No arguments; returns touch capability. No allocation, lock, I/O or paint. */
  XP_UI_READER_OVERLAY_QUERY_TOUCH = 18,
  /* No arguments; returns signed TOC index bits. May allocate and read storage; caller holds RenderLock. */
  XP_UI_READER_OVERLAY_QUERY_CURRENT_TOC = 19,
  /* No arguments; returns the A4 board flag. No allocation, lock, I/O or paint. */
  XP_UI_READER_OVERLAY_QUERY_IS_A4 = 20,
  /* No arguments; returns the Xteink panel flag. No allocation, lock, I/O or paint. */
  XP_UI_READER_OVERLAY_QUERY_IS_XTEINK = 21,
} XpUiReaderOverlayQuery;
typedef enum {
  /* a/b ignored; return ignored; dispatches existing picker, possibly synchronously calling PopupChanged; may
     allocate/navigate/paint. */
  XP_UI_READER_OVERLAY_COMMAND_POPUP_INPUT = 0,
  /* a/b ignored; return ignored; acquires the original resource RenderLock; may wait; opens one paired scope. */
  XP_UI_READER_OVERLAY_COMMAND_LOCK = 1,
  /* a/b ignored; return ignored; destroys exactly one active paired lock scope. */
  XP_UI_READER_OVERLAY_COMMAND_UNLOCK = 2,
  /* a/b ignored; return ignored; waits for existing deferred overlay refresh. */
  XP_UI_READER_OVERLAY_COMMAND_SETTLE = 3,
  /* a/b ignored; return ignored; paints existing popup; may allocate/lock/block. */
  XP_UI_READER_OVERLAY_COMMAND_PAINT_POPUP = 4,
  /* a/b ignored; return ignored; restores the existing B/W snapshot without panel-baseline resync. */
  XP_UI_READER_OVERLAY_COMMAND_RESTORE = 5,
  /* a/b ignored; returns stored flag; stores the existing B/W snapshot; may allocate. */
  XP_UI_READER_OVERLAY_COMMAND_STORE = 6,
  /* a/b ignored; return ignored; paints existing toolbar/panel; may allocate/log/block. */
  XP_UI_READER_OVERLAY_COMMAND_RENDER = 7,
  /* a/b ignored; return ignored; requests existing overlay refresh; may block. */
  XP_UI_READER_OVERLAY_COMMAND_PUSH = 8,
  /* a/b ignored; return ignored; notifies the existing render task. */
  XP_UI_READER_OVERLAY_COMMAND_UPDATE = 9,
  /* a/b ignored; return ignored; closes existing overlay; may free/lock/block. */
  XP_UI_READER_OVERLAY_COMMAND_CLOSE = 10,
  /* a=overlay ID, b ignored; return ignored; opens existing overlay; may allocate/free/lock/block. */
  XP_UI_READER_OVERLAY_COMMAND_OPEN = 11,
  /* a/b ignored; return ignored; clears deferred position scalars. */
  XP_UI_READER_OVERLAY_COMMAND_CLEAR_DEFERRED = 12,
  /* a/b ignored; return ignored; destroys existing section; may free/log. */
  XP_UI_READER_OVERLAY_COMMAND_DROP_SECTION = 13,
  /* a/b ignored; returns toolbar Event ID and caches its POD payload; routes the mapped frame against published chrome
     geometry; may consume contact state. */
  XP_UI_READER_OVERLAY_COMMAND_ROUTE = 14,
  /* a/b ignored; return ignored; dismisses existing picker; may release resources. */
  XP_UI_READER_OVERLAY_COMMAND_DISMISS_POPUP = 15,
  /* a/b ignored; return ignored; discards page snapshot; may free. */
  XP_UI_READER_OVERLAY_COMMAND_DISCARD = 16,
  /* a/b ignored; return ignored; allocates existing text-settings child with owner-only result callback; may navigate;
     callback invokes PickerResult. */
  XP_UI_READER_OVERLAY_COMMAND_START_PICKER = 17,
  /* a/b ignored; return ignored; applies existing reader text settings; may allocate/free/log/lock/storage. */
  XP_UI_READER_OVERLAY_COMMAND_APPLY_TEXT = 18,
  /* a/b ignored; return ignored; resets existing toolbar input; short POD critical section only. */
  XP_UI_READER_OVERLAY_COMMAND_TOOLBAR_BEGIN = 19,
  /* a=boolean setting, b ignored; return ignored; writes setting scalar only. */
  XP_UI_READER_OVERLAY_COMMAND_SET_FOCUS_READING = 20,
  /* a/b ignored; return ignored; applies and persists existing text setting; may allocate/free/log/lock/storage. */
  XP_UI_READER_OVERLAY_COMMAND_APPLY_LIVE = 21,
  /* a=row index, b ignored; return ignored; opens existing setting picker; may allocate/lock/paint. */
  XP_UI_READER_OVERLAY_COMMAND_TEXT_POPUP = 22,
  /* a=row index, b ignored; return ignored; runs existing row action; may allocate/free/log/lock/storage/navigate. */
  XP_UI_READER_OVERLAY_COMMAND_MORE = 23,
  /* a=TOC row index, b ignored; returns signed target spine bits; creates scoped TOC entry; may allocate/read storage;
     retained until TocDrop. */
  XP_UI_READER_OVERLAY_COMMAND_TOC_ITEM = 24,
  /* a/b ignored; return ignored; copies scoped TOC anchor into pending path under original resource lock; may
     allocate/free. */
  XP_UI_READER_OVERLAY_COMMAND_TOC_ANCHOR = 25,
  /* a/b ignored; return ignored; destroys scoped TOC entry after navigation update; may free. */
  XP_UI_READER_OVERLAY_COMMAND_TOC_DROP = 26,
  /* a=signed row delta, b=row count; returns moved flag; moves existing viewport; short POD critical section only. */
  XP_UI_READER_OVERLAY_COMMAND_SCROLL = 27,
  /* a/b ignored; returns mapped SwipeDir ID; consumes original mapped swipe at the original lazy position. */
  XP_UI_READER_OVERLAY_COMMAND_TAKE_SWIPE = 28,
  /* a=XP_UI pressed mask, b=XP_UI released mask; samples only requested bits; return ignored; runs original lazy probes
     in requested order and may update touch-held bookkeeping; no hardware polling. */
  XP_UI_READER_OVERLAY_COMMAND_SAMPLE_BUTTONS = 29,
  /* a/b ignored; returns mapped hold duration in milliseconds; reads original held-time provider and may clear expired
     touch-held bookkeeping; no hardware polling. */
  XP_UI_READER_OVERLAY_COMMAND_TAKE_HELD_MS = 30,
  /* a/b ignored; returns 0. Resets mapped Home input; no allocation, lock, I/O or paint. */
  XP_UI_READER_OVERLAY_COMMAND_RESET_HOME = 31,
  /* a/b ignored; returns 0. Allocates toolbar resources; no painting. Requires no existing toolbar. */
  XP_UI_READER_OVERLAY_COMMAND_CREATE_TOOLBAR = 32,
  /* a=selected, b=top; returns 0. Takes the short toolbar model lock; no allocation, I/O or paint. */
  XP_UI_READER_OVERLAY_COMMAND_RESET_NAVIGATION = 33,
  /* a/b ignored; returns 0. Builds native row resources; may allocate/free and read storage, never paints. */
  XP_UI_READER_OVERLAY_COMMAND_BUILD_MORE = 34,
  /* a/b ignored; returns 0. Frees toolbar resources; no paint or panel wait. */
  XP_UI_READER_OVERLAY_COMMAND_DROP_TOOLBAR = 35,
  /* a=refresh counter, b ignored; returns 0. Takes short ModelLock; no allocation, I/O or paint. */
  XP_UI_READER_OVERLAY_COMMAND_FORCE_COUNTER = 36,
  /* a/b ignored; returns 0. Caller holds RenderLock; pushes FAST refresh and may block on the panel. */
  XP_UI_READER_OVERLAY_COMMAND_DISPLAY_FAST = 37,
} XpUiReaderOverlayCommand;
typedef enum {
  /* Handle one mapped toolbar/panel input frame. Operation dispatch returns no value. */
  XP_UI_READER_OVERLAY_OPERATION_INPUT = 0,
  /* Existing picker redraw callback, synchronous or owner-bound. Operation dispatch returns no value. */
  XP_UI_READER_OVERLAY_OPERATION_POPUP_CHANGED = 1,
  /* Existing text-settings child result callback, owner-bound. Operation dispatch returns no value. */
  XP_UI_READER_OVERLAY_OPERATION_PICKER_RESULT = 2,
  XP_UI_READER_OVERLAY_OPERATION_OPEN_NONE = 3,
  XP_UI_READER_OVERLAY_OPERATION_OPEN_TOOLBAR = 4,
  XP_UI_READER_OVERLAY_OPERATION_OPEN_CONTENTS = 5,
  XP_UI_READER_OVERLAY_OPERATION_OPEN_TEXT = 6,
  XP_UI_READER_OVERLAY_OPERATION_OPEN_MORE = 7,
  XP_UI_READER_OVERLAY_OPERATION_CLOSE = 8,
} XpUiReaderOverlayOperation;
enum { XP_UI_READER_OVERLAY_TEXT_ROWS = 5, XP_UI_READER_OVERLAY_HOLD_MS = 1500, XP_UI_READER_OVERLAY_HOLD_ROWS = 10 };
typedef struct {
  void* context;
  uint32_t (*read)(void*, uint8_t);
  void (*write)(void*, uint8_t, uint32_t);
  uint32_t (*query)(void*, uint8_t);
  uint32_t (*command)(void*, uint8_t, int32_t, int32_t);
} XpUiReaderOverlayServices;
/* All callbacks required and synchronous; null/incomplete services or invalid
 * operation have no effects. Context may be null only if every callback accepts it.
 * Scalar read/write use short ModelLock; resource callbacks run outside it.
 * Rust guards pair LOCK/UNLOCK and TOC_ITEM/TOC_DROP, including early returns. Callbacks retain no context
 * or arguments; owner-bound child callbacks create a fresh service context.
 * TOC_ITEM owns its entry until TOC_DROP after the update request. */
void xp_ui_reader_overlay_run(const XpUiReaderOverlayServices*, uint8_t operation);

/* Called under RenderLock, never ModelLock. Null/missing callbacks produce no paint.
 * query supplies scalar state or a borrowed resource result; CHAPTER_TITLE prepares
 * a string until paint returns. PROGRESS uses IEEE-754 f32 bits in both directions.
 * paint borrows the view only for its synchronous call; no pointer is retained. */
typedef enum {
  XP_UI_READER_VIEW_HAS_BOOK = 0,
  XP_UI_READER_VIEW_HAS_SECTION = 1,
  XP_UI_READER_VIEW_HAS_TOOLBAR = 2,
  XP_UI_READER_VIEW_MODE = 3,
  XP_UI_READER_VIEW_CURSOR = 4,
  XP_UI_READER_VIEW_FOCUS = 5,
  XP_UI_READER_VIEW_CHAPTER_TITLE = 6,
  XP_UI_READER_VIEW_PAGE_COUNT = 7,
  XP_UI_READER_VIEW_CURRENT_PAGE = 8,
  XP_UI_READER_VIEW_PROGRESS = 9,
  XP_UI_READER_VIEW_TOUCH = 10,
  XP_UI_READER_VIEW_INDEX = 11,
  XP_UI_READER_VIEW_TOC_COUNT = 12,
  XP_UI_READER_VIEW_MORE_COUNT = 13,
} XpUiReaderOverlayViewQuery;
typedef struct {
  int32_t active_tool, selected_index, item_count, progress_permille;
  uint8_t kind, dense, reserved[2]; /* kind: toolbar=1, contents=2, text=3, more=4 */
  uint8_t page_info[40];            /* NUL-terminated toolbar label; empty for panels. */
} XpUiReaderOverlayView;
typedef struct {
  void* context;
  uint32_t (*query)(void* context, uint8_t query, uint32_t argument);
  void (*paint)(void* context, const XpUiReaderOverlayView* view);
} XpUiReaderOverlayViewServices;
void xp_ui_reader_overlay_view(const XpUiReaderOverlayViewServices*);
typedef enum {
  XP_UI_READER_DECORATION_NONE = 0,
  XP_UI_READER_DECORATION_FOCUS = 1,
  XP_UI_READER_DECORATION_NIGHT = 2,
  XP_UI_READER_DECORATION_BLUETOOTH = 3,
  XP_UI_READER_DECORATION_FRONTLIGHT = 4,
} XpUiReaderDecoration;
/* Returns a decoration resource ID. Text mode takes a row index;
 * More mode takes the persisted menu-action ID. */
uint8_t xp_ui_reader_overlay_checkbox(uint8_t mode, int32_t row_or_action);

/* Navigation resources are synchronous and retain no pointers. Invoke outside
 * ModelLock. read/write copy one scalar under the short model lock; query and
 * command may allocate, access storage, log or acquire the original RenderLock.
 * Callbacks must not reenter navigation with the same mutable context. href is
 * borrowed for the whole call, may be null only when href_len is zero, and is
 * never mutated. Saved-position depth must remain within 0..3. Null/missing
 * services or an unknown operation return zero with no effects. */
/* Field reads/writes copy scalars only; signed values use i32 bit patterns.
 * CachedOffset/PendingOffset writes set their optional presence. HasCachedOffset
 * accepts only a zero write to clear it. HasCurrentOffset/CurrentOffset are
 * read-only. SpineProgress carries IEEE-754 f32 bits; Saved* slots are fixed. */
typedef enum {
  XP_UI_READER_NAV_FIELD_SPINE = 0,
  XP_UI_READER_NAV_FIELD_CACHED_SPINE = 1,
  XP_UI_READER_NAV_FIELD_NEXT_PAGE = 2,
  XP_UI_READER_NAV_FIELD_CACHED_COUNT = 3,
  XP_UI_READER_NAV_FIELD_HAS_CACHED_OFFSET = 4,
  XP_UI_READER_NAV_FIELD_CACHED_OFFSET = 5,
  XP_UI_READER_NAV_FIELD_HAS_CURRENT_OFFSET = 6,
  XP_UI_READER_NAV_FIELD_CURRENT_OFFSET = 7,
  XP_UI_READER_NAV_FIELD_PENDING_OFFSET = 8,
  XP_UI_READER_NAV_FIELD_PERCENT_JUMP = 9,
  XP_UI_READER_NAV_FIELD_SPINE_PROGRESS = 10,
  XP_UI_READER_NAV_FIELD_FOOTNOTE_DEPTH = 11,
  XP_UI_READER_NAV_FIELD_SAVED_SPINE0 = 12,
  XP_UI_READER_NAV_FIELD_SAVED_PAGE0 = 13,
  XP_UI_READER_NAV_FIELD_SAVED_SPINE1 = 14,
  XP_UI_READER_NAV_FIELD_SAVED_PAGE1 = 15,
  XP_UI_READER_NAV_FIELD_SAVED_SPINE2 = 16,
  XP_UI_READER_NAV_FIELD_SAVED_PAGE2 = 17,
} XpUiReaderNavField;
typedef enum {
  /* argument ignored; returns book presence. Pure, no allocation or I/O. */
  XP_UI_READER_NAV_QUERY_HAS_BOOK = 0,
  /* argument ignored; returns total book bytes as u32. Pure. */
  XP_UI_READER_NAV_QUERY_BOOK_SIZE = 1,
  /* argument ignored; returns nonnegative spine count as i32 bits. Pure. */
  XP_UI_READER_NAV_QUERY_SPINE_COUNT = 2,
  /* argument=byte offset; returns found spine as i32 bits, negative if absent. May read book metadata. */
  XP_UI_READER_NAV_QUERY_FIND_SPINE = 3,
  /* argument=spine i32 bits; returns cumulative byte size. May read book metadata. */
  XP_UI_READER_NAV_QUERY_CUMULATIVE = 4,
  /* argument ignored; returns section presence. Pure. */
  XP_UI_READER_NAV_QUERY_HAS_SECTION = 5,
  /* argument ignored; returns current page as i32 bits. Pure. */
  XP_UI_READER_NAV_QUERY_SECTION_PAGE = 6,
  /* argument ignored; returns the u16 page watermark. Pure. */
  XP_UI_READER_NAV_QUERY_SECTION_COUNT = 7,
  /* argument ignored; returns estimated u16 page total. Pure. */
  XP_UI_READER_NAV_QUERY_ESTIMATED_COUNT = 8,
  /* argument ignored; returns active section-build state. Pure. */
  XP_UI_READER_NAV_QUERY_BUILDING = 9,
  /* argument=text offset; returns 0 when absent, otherwise bit16 plus u16 page. May read storage. */
  XP_UI_READER_NAV_QUERY_MAP_OFFSET = 10,
  /* argument=u16 page; returns presence and retains a scalar result for OFFSET_VALUE. May read storage. */
  XP_UI_READER_NAV_QUERY_GET_OFFSET = 11,
  /* argument ignored; returns the last GET_OFFSET result. Pure; valid only after a present result. */
  XP_UI_READER_NAV_QUERY_OFFSET_VALUE = 12,
  /* argument ignored; returns last FALLBACK spine as i32 bits. Pure. */
  XP_UI_READER_NAV_QUERY_FALLBACK_SPINE = 13,
  /* argument ignored; returns last FALLBACK page as i32 bits. Pure. */
  XP_UI_READER_NAV_QUERY_FALLBACK_PAGE = 14,
  /* argument ignored; returns the borrowed progress result offset-present flag. Pure. */
  XP_UI_READER_NAV_QUERY_SYNC_HAS_OFFSET = 15,
  /* argument ignored; returns the borrowed progress result text offset. Pure. */
  XP_UI_READER_NAV_QUERY_SYNC_OFFSET = 16,
  /* argument ignored; returns the borrowed progress result spine as i32 bits. Pure. */
  XP_UI_READER_NAV_QUERY_SYNC_SPINE = 17,
  /* argument ignored; returns the borrowed progress result page as i32 bits. Pure. */
  XP_UI_READER_NAV_QUERY_SYNC_PAGE = 18,
  /* argument ignored; returns the borrowed progress result total as i32 bits. Pure. */
  XP_UI_READER_NAV_QUERY_SYNC_COUNT = 19,
  /* argument ignored; returns the saved XPath/progress presence flag. Pure. */
  XP_UI_READER_NAV_QUERY_SYNC_HAS_PROGRESS = 20,
  /* argument ignored; resolves the borrowed href, returning i32 spine bits. May allocate/read storage. */
  XP_UI_READER_NAV_QUERY_RESOLVE_HREF = 21,
} XpUiReaderNavQuery;
typedef enum {
  /* a/b ignored; returns 0. Acquires the original RenderLock; may block. Must pair with UNLOCK. */
  XP_UI_READER_NAV_COMMAND_LOCK = 0,
  /* a/b ignored; returns 0. Releases the RenderLock held by this operation. */
  XP_UI_READER_NAV_COMMAND_UNLOCK = 1,
  /* a/b ignored; returns 0. Destroys the section, freeing resources and closing its storage handles. */
  XP_UI_READER_NAV_COMMAND_DROP_SECTION = 2,
  /* a/b ignored; returns 0. Requests the existing render-task update; never waits for a panel refresh. */
  XP_UI_READER_NAV_COMMAND_UPDATE = 3,
  /* a=page, b ignored; returns 0. Writes the section page; no allocation or I/O. */
  XP_UI_READER_NAV_COMMAND_SET_SECTION_PAGE = 4,
  /* a=current spine, b=page total; returns 0. Maps borrowed saved XPath/progress; may allocate/read storage. */
  XP_UI_READER_NAV_COMMAND_FALLBACK = 5,
  /* a=byte start, b=length within href; returns 0. Copies the anchor into a native temporary; may allocate. */
  XP_UI_READER_NAV_COMMAND_PREPARE_ANCHOR = 6,
  /* a/b ignored; returns 0. Moves the prepared anchor into the reader resource; may free the old buffer. */
  XP_UI_READER_NAV_COMMAND_MOVE_ANCHOR = 7,
  /* a/b ignored; returns 0. Emits the existing unresolved-href diagnostic; may block in logging. */
  XP_UI_READER_NAV_COMMAND_LOG_UNRESOLVED = 8,
  /* a=new depth, b=saved page; returns 0. Logs the saved current spine/page; may block in logging. */
  XP_UI_READER_NAV_COMMAND_LOG_SAVED = 9,
  /* a=stack index 0..2, b ignored; returns 0. Logs that saved position; may block in logging. */
  XP_UI_READER_NAV_COMMAND_LOG_RESTORED = 10,
  /* a=target spine, b ignored; returns 0. Logs the borrowed href and spine; may block in logging. */
  XP_UI_READER_NAV_COMMAND_LOG_NAVIGATED = 11,
  /* a=offset present, b=u32 offset bits; uses context spine/page/count, returns bool. May allocate/write storage. */
  XP_UI_READER_NAV_COMMAND_SAVE_PROGRESS = 12,
} XpUiReaderNavCommand;
typedef enum {
  XP_UI_READER_NAV_OPERATION_PERCENT = 0,
  XP_UI_READER_NAV_OPERATION_PROGRESS = 1,
  XP_UI_READER_NAV_OPERATION_DEFERRED = 2,
  XP_UI_READER_NAV_OPERATION_CLEAR = 3,
  XP_UI_READER_NAV_OPERATION_SAVE = 4,
  XP_UI_READER_NAV_OPERATION_REMEMBER = 5,
  XP_UI_READER_NAV_OPERATION_HREF = 6,
  XP_UI_READER_NAV_OPERATION_RESTORE = 7,
} XpUiReaderNavOperation;
typedef struct {
  void* context;
  uint32_t (*read)(void* context, uint8_t field);
  void (*write)(void* context, uint8_t field, uint32_t value);
  uint32_t (*query)(void* context, uint8_t query, uint32_t argument);
  uint32_t (*command)(void* context, uint8_t command, int32_t a, int32_t b);
  const uint8_t* href;
  uint32_t href_len;
} XpUiReaderNavServices;
/* Percent: a=percent; Save: a=spine,b=page (count is a context resource);
 * Href: a=save-position; other arguments ignored. Deferred/Save return bool. */
uint32_t xp_ui_reader_navigation(const XpUiReaderNavServices*, uint8_t operation, int32_t a, int32_t b);

/* Book rendering runs synchronously under the caller's RenderLock, outside
 * ModelLock. read/write hold only a short ModelLock to copy one scalar. Native
 * resources are borrowed for this call; no service/context pointer is retained.
 * START_WITH_POPUP may install a callback in Section, but that callback captures
 * only the activity owner and may not capture the services or transient state.
 * The policy balances GET_PATH/DROP_PATH, LOAN_BEGIN/LOAN_DROP, LOAD_PAGE/DROP_PAGE
 * and SAMPLE_FONT_CACHE/RELEASE_FONT_CACHE on all returns. LOAN_END restores the framebuffer early without destroying
 * the loan object. Null or incomplete services have no effects. */
/* Scalar fields use their C3 representation: signed values preserve their i32
 * bits, flags are 0/1, SPINE_PROGRESS is IEEE f32 bits, timestamps/offsets u32,
 * dimensions/counts u16 and RETRY u8. HAS_* writes accept only zero and reset
 * the corresponding optional; value writes set it. No scalar callback performs
 * I/O, allocation, painting, or reentry. OVERLAY follows XpUiReaderOverlayMode.
 *
 * Queries take no arguments. HAS_* and PARTIAL/BUILDING/COMPLETE/POPUP_ACTIVE/
 * USES_TOOLBAR return bool; dimensions, margins, counts and PAGE return their
 * scalar value. NOW returns wrapping milliseconds. FIND_ANCHOR/PAGE_FOR_ANCHOR
 * return bit16 presence plus a low-u16 page; both require a live Section and
 * nonempty pending anchor and may read SD/allocate. HAS_HTML may access SD.
 * HAS_TTF samples the renderer's fonts without allocation. Query IDs 23 and 27
 * are reserved. PAGE_OFFSET requires a loaded page.
 * Other queries do not allocate, lock, paint, or modify resources.
 *
 * Commands return zero unless specified. Arguments are ignored unless listed:
 * SET_MARGIN(a=index 0..3,b=pixels), COUNTER(a=counter), PAGE(a=page),
 * CUMULATIVE(a=spine)->u32 bytes, REACHED_OFFSET(a=u32 bits)->bool,
 * MAP_OFFSET(a=u32 bits)->bit16 presence+u16 page, BUILD_MORE(a=page budget)->bool,
 * LOG_DEFERRED(a=target), LOG_ANCHOR(a=page), LOG_RENDERED(a=start millis),
 * DRAW_EMPTY/DRAW_BOUNDS/DRAW_LOAD_ERROR(a=y). CUMULATIVE reads book metadata;
 * MAP_OFFSET and REACHED_OFFSET require Section and may read storage/allocate.
 *
 * GET_PATH allocates a temporary spine path, DROP_PATH releases it. NEW_SECTION
 * allocates Section; DROP_SECTION destroys it. LOAD_CACHE/CREATE_ALL/
 * START_WITH_POPUP/START_PARTIAL/BUILD_MORE return bool and may allocate, access
 * storage and invoke the owner's popup callback; CREATE_ALL/START_WITH_POPUP
 * require an active framebuffer loan. LOAN_BEGIN borrows the framebuffer;
 * LOAN_END restores it early, LOAN_DROP destroys the loan. LOAD_PAGE allocates
 * and reads a page; HAS_PAGE reports success. ASSIGN_METADATA requires a page,
 * allocates metadata and returns bool. RENDER_CONTENTS consumes that page and
 * paints through the existing page/strip path. DROP_PAGE releases any remainder.
 * ABANDON_BUILD/CLEAR_CACHE release build resources and may access storage.
 * SAMPLE_FONT_CACHE borrows the current cache pointer (which may be null);
 * RELEASE_FONT_CACHE releases its caches if present and clears that pointer.
 * Both return zero; every sample is paired with release. TAKE_FONT_NOTICE
 * consumes the font notice and returns bool without allocation or painting.
 *
 * ROOM may stop/wait for radio and returns bool; RADIO_SETTLE may wait for radio.
 * SETTLE_OVERLAY, DISCARD_OVERLAY and STORE manage the native framebuffer slot;
 * STORE allocates its existing snapshot and returns bool. SAVE_PROGRESS writes
 * storage and returns bool; APPLY_DEFERRED/CLEAR_DEFERRED invoke navigation
 * policy. SET_SPEC samples settings; SAMPLE_MARGINS samples renderer geometry.
 * CLEAR_METADATA clears native metadata, BOOKMARK_FLAG reads bookmark storage,
 * MARK_RENDERED updates the render frame, UPDATE requests its next render.
 * SCREENSHOT performs existing SD screenshot I/O. LOG_* emit the original
 * diagnostics; LOG_RENDERED reads the clock only when debug logging is enabled.
 * CLEAR_SCREEN, DISPLAY, BACK_HINTS, STATUS_BAR, DRAW and POPUP commands,
 * RENDER_OVERLAY, RENDER_POPUP and PUSH_REFRESH paint or schedule existing
 * display work and may allocate existing UI resources. Commands run under the
 * caller's RenderLock and never acquire another RenderLock or hold ModelLock
 * over I/O or painting. */
typedef enum {
  XP_UI_READER_BOOK_FIELD_SPINE = 0,
  XP_UI_READER_BOOK_FIELD_CACHED_SPINE = 1,
  XP_UI_READER_BOOK_FIELD_CACHED_COUNT = 2,
  XP_UI_READER_BOOK_FIELD_HAS_CACHED_OFFSET = 3,
  XP_UI_READER_BOOK_FIELD_CACHED_OFFSET = 4,
  XP_UI_READER_BOOK_FIELD_HAS_PENDING_PAGE = 5,
  XP_UI_READER_BOOK_FIELD_PENDING_PAGE = 6,
  XP_UI_READER_BOOK_FIELD_NEXT_PAGE = 7,
  XP_UI_READER_BOOK_FIELD_HAS_PENDING_OFFSET = 8,
  XP_UI_READER_BOOK_FIELD_PENDING_OFFSET = 9,
  XP_UI_READER_BOOK_FIELD_PENDING_PERCENT = 10,
  XP_UI_READER_BOOK_FIELD_SPINE_PROGRESS = 11,
  XP_UI_READER_BOOK_FIELD_AUTOMATIC = 12,
  XP_UI_READER_BOOK_FIELD_SYNC_ERROR = 13,
  XP_UI_READER_BOOK_FIELD_PARTIAL_START_FAILED = 14,
  XP_UI_READER_BOOK_FIELD_BUILD_POPUP = 15,
  XP_UI_READER_BOOK_FIELD_BUILD_WIDTH = 16,
  XP_UI_READER_BOOK_FIELD_BUILD_HEIGHT = 17,
  XP_UI_READER_BOOK_FIELD_PAGE_STALE = 18,
  XP_UI_READER_BOOK_FIELD_RETRY = 19,
  XP_UI_READER_BOOK_FIELD_CURRENT_OFFSET = 20,
  XP_UI_READER_BOOK_FIELD_MARGIN_LEFT = 21,
  XP_UI_READER_BOOK_FIELD_MARGIN_TOP = 22,
  XP_UI_READER_BOOK_FIELD_LAST_RENDER = 23,
  XP_UI_READER_BOOK_FIELD_LAST_SAVED_SPINE = 24,
  XP_UI_READER_BOOK_FIELD_LAST_SAVED_PAGE = 25,
  XP_UI_READER_BOOK_FIELD_LAST_SAVED_COUNT = 26,
  XP_UI_READER_BOOK_FIELD_SCREENSHOT = 27,
  XP_UI_READER_BOOK_FIELD_BOOKMARK_NOTICE = 28,
  XP_UI_READER_BOOK_FIELD_BOOKMARK_REMOVED = 29,
  XP_UI_READER_BOOK_FIELD_DICTIONARY_NOTICE = 30,
  XP_UI_READER_BOOK_FIELD_FONT_NOTICE = 31,
  XP_UI_READER_BOOK_FIELD_FONT_NOTICE_MS = 32,
  XP_UI_READER_BOOK_FIELD_CLIPPING_NOTICE = 33,
  XP_UI_READER_BOOK_FIELD_OVERLAY = 34,
  XP_UI_READER_BOOK_FIELD_STORED = 35,
} XpUiReaderBookField;
typedef enum {
  XP_UI_READER_BOOK_QUERY_HAS_BOOK = 0,
  XP_UI_READER_BOOK_QUERY_SPINE_COUNT = 1,
  XP_UI_READER_BOOK_QUERY_HAS_SECTION = 2,
  XP_UI_READER_BOOK_QUERY_MARGIN_TOP = 3,
  XP_UI_READER_BOOK_QUERY_MARGIN_RIGHT = 4,
  XP_UI_READER_BOOK_QUERY_MARGIN_BOTTOM = 5,
  XP_UI_READER_BOOK_QUERY_MARGIN_LEFT = 6,
  XP_UI_READER_BOOK_QUERY_SCREEN_MARGIN = 7,
  XP_UI_READER_BOOK_QUERY_STATUS_HEIGHT = 8,
  XP_UI_READER_BOOK_QUERY_PROGRESS_HEIGHT = 9,
  XP_UI_READER_BOOK_QUERY_STATUS_MARGIN = 10,
  XP_UI_READER_BOOK_QUERY_WIDTH = 11,
  XP_UI_READER_BOOK_QUERY_HEIGHT = 12,
  XP_UI_READER_BOOK_QUERY_PARTIAL = 13,
  XP_UI_READER_BOOK_QUERY_BUILDING = 14,
  XP_UI_READER_BOOK_QUERY_COMPLETE = 15,
  XP_UI_READER_BOOK_QUERY_PAGE_COUNT = 16,
  XP_UI_READER_BOOK_QUERY_PAGE = 17,
  XP_UI_READER_BOOK_QUERY_HAS_ANCHOR = 18,
  XP_UI_READER_BOOK_QUERY_HAS_HTML = 19,
  XP_UI_READER_BOOK_QUERY_FIND_ANCHOR = 20,
  XP_UI_READER_BOOK_QUERY_PAGE_FOR_ANCHOR = 21,
  XP_UI_READER_BOOK_QUERY_HAS_TTF = 22,
  XP_UI_READER_BOOK_QUERY_NOW = 24,
  XP_UI_READER_BOOK_QUERY_POPUP_ACTIVE = 25,
  XP_UI_READER_BOOK_QUERY_USES_TOOLBAR = 26,
  XP_UI_READER_BOOK_QUERY_HAS_PAGE = 28,
  XP_UI_READER_BOOK_QUERY_PAGE_OFFSET = 29,
  XP_UI_READER_BOOK_QUERY_ESTIMATED_COUNT = 30,
} XpUiReaderBookQuery;
typedef enum {
  XP_UI_READER_BOOK_COMMAND_CLEAR_METADATA = 0,
  XP_UI_READER_BOOK_COMMAND_RADIO_SETTLE = 1,
  XP_UI_READER_BOOK_COMMAND_SETTLE_OVERLAY = 2,
  XP_UI_READER_BOOK_COMMAND_SAMPLE_MARGINS = 3,
  XP_UI_READER_BOOK_COMMAND_SET_MARGIN = 4,
  XP_UI_READER_BOOK_COMMAND_SET_SPEC = 5,
  XP_UI_READER_BOOK_COMMAND_GET_PATH = 6,
  XP_UI_READER_BOOK_COMMAND_DROP_PATH = 7,
  XP_UI_READER_BOOK_COMMAND_LOG_LOADING = 8,
  XP_UI_READER_BOOK_COMMAND_NEW_SECTION = 9,
  XP_UI_READER_BOOK_COMMAND_LOAD_CACHE = 10,
  XP_UI_READER_BOOK_COMMAND_LOG_PARTIAL = 11,
  XP_UI_READER_BOOK_COMMAND_LOG_NO_CACHE = 12,
  XP_UI_READER_BOOK_COMMAND_COUNTER = 13,
  XP_UI_READER_BOOK_COMMAND_ROOM = 14,
  XP_UI_READER_BOOK_COMMAND_DROP_SECTION = 15,
  XP_UI_READER_BOOK_COMMAND_LOAN_BEGIN = 16,
  XP_UI_READER_BOOK_COMMAND_LOAN_END = 17,
  XP_UI_READER_BOOK_COMMAND_LOAN_DROP = 18,
  XP_UI_READER_BOOK_COMMAND_CREATE_ALL = 19,
  XP_UI_READER_BOOK_COMMAND_LOG_PERSIST_FAILED = 20,
  XP_UI_READER_BOOK_COMMAND_LOG_DEFERRED = 21,
  XP_UI_READER_BOOK_COMMAND_CUMULATIVE = 22,
  XP_UI_READER_BOOK_COMMAND_RELEASE_FONT_CACHE = 23,
  XP_UI_READER_BOOK_COMMAND_LOG_HEAP = 24,
  XP_UI_READER_BOOK_COMMAND_START_WITH_POPUP = 25,
  XP_UI_READER_BOOK_COMMAND_LOG_START_FAILED = 26,
  XP_UI_READER_BOOK_COMMAND_REACHED_OFFSET = 27,
  XP_UI_READER_BOOK_COMMAND_SHOW_BUILD_POPUP = 28,
  XP_UI_READER_BOOK_COMMAND_BUILD_MORE = 29,
  XP_UI_READER_BOOK_COMMAND_LOG_STEP_FAILED = 30,
  XP_UI_READER_BOOK_COMMAND_LOG_CACHE_FOUND = 31,
  XP_UI_READER_BOOK_COMMAND_PAGE = 32,
  XP_UI_READER_BOOK_COMMAND_MAP_OFFSET = 33,
  XP_UI_READER_BOOK_COMMAND_CLEAR_DEFERRED = 34,
  XP_UI_READER_BOOK_COMMAND_LOG_ANCHOR = 35,
  XP_UI_READER_BOOK_COMMAND_CLEAR_ANCHOR = 36,
  XP_UI_READER_BOOK_COMMAND_START_PARTIAL = 37,
  XP_UI_READER_BOOK_COMMAND_LOG_PARTIAL_START_FAILED = 38,
  XP_UI_READER_BOOK_COMMAND_APPLY_DEFERRED = 39,
  XP_UI_READER_BOOK_COMMAND_CLEAR_SCREEN = 40,
  XP_UI_READER_BOOK_COMMAND_LOG_EMPTY = 41,
  XP_UI_READER_BOOK_COMMAND_STATUS_BAR = 42,
  XP_UI_READER_BOOK_COMMAND_DISPLAY = 43,
  XP_UI_READER_BOOK_COMMAND_LOG_BOUNDS = 44,
  XP_UI_READER_BOOK_COMMAND_BOOKMARK_FLAG = 45,
  XP_UI_READER_BOOK_COMMAND_LOAD_PAGE = 46,
  XP_UI_READER_BOOK_COMMAND_DROP_PAGE = 47,
  XP_UI_READER_BOOK_COMMAND_ASSIGN_METADATA = 48,
  XP_UI_READER_BOOK_COMMAND_LOG_PAGE_LOAD_FAILED = 49,
  XP_UI_READER_BOOK_COMMAND_LOG_METADATA_FAILED = 50,
  XP_UI_READER_BOOK_COMMAND_ABANDON_BUILD = 51,
  XP_UI_READER_BOOK_COMMAND_CLEAR_CACHE = 52,
  XP_UI_READER_BOOK_COMMAND_LOG_RETRY_FAILED = 53,
  XP_UI_READER_BOOK_COMMAND_UPDATE = 54,
  XP_UI_READER_BOOK_COMMAND_DISCARD_OVERLAY = 55,
  XP_UI_READER_BOOK_COMMAND_RENDER_CONTENTS = 56,
  XP_UI_READER_BOOK_COMMAND_LOG_RENDERED = 57,
  XP_UI_READER_BOOK_COMMAND_MARK_RENDERED = 58,
  XP_UI_READER_BOOK_COMMAND_SAVE_PROGRESS = 59,
  XP_UI_READER_BOOK_COMMAND_SCREENSHOT = 60,
  XP_UI_READER_BOOK_COMMAND_STORE = 61,
  XP_UI_READER_BOOK_COMMAND_RENDER_OVERLAY = 62,
  XP_UI_READER_BOOK_COMMAND_RENDER_POPUP = 63,
  XP_UI_READER_BOOK_COMMAND_PUSH_REFRESH = 64,
  XP_UI_READER_BOOK_COMMAND_BACK_HINTS = 65,
  XP_UI_READER_BOOK_COMMAND_POPUP_SYNC = 66,
  XP_UI_READER_BOOK_COMMAND_POPUP_RADIO_ERROR = 67,
  XP_UI_READER_BOOK_COMMAND_POPUP_BUILD_ERROR = 68,
  XP_UI_READER_BOOK_COMMAND_POPUP_INDEX = 69,
  XP_UI_READER_BOOK_COMMAND_POPUP_BOOKMARK_REMOVED = 70,
  XP_UI_READER_BOOK_COMMAND_POPUP_BOOKMARK_ADDED = 71,
  XP_UI_READER_BOOK_COMMAND_POPUP_DICTIONARY = 72,
  XP_UI_READER_BOOK_COMMAND_POPUP_FONT = 73,
  XP_UI_READER_BOOK_COMMAND_POPUP_CLIPPING = 74,
  XP_UI_READER_BOOK_COMMAND_DRAW_EMPTY = 75,
  XP_UI_READER_BOOK_COMMAND_DRAW_BOUNDS = 76,
  XP_UI_READER_BOOK_COMMAND_DRAW_LOAD_ERROR = 77,
  XP_UI_READER_BOOK_COMMAND_SAMPLE_FONT_CACHE = 78,
  XP_UI_READER_BOOK_COMMAND_TAKE_FONT_NOTICE = 79,
} XpUiReaderBookCommand;
typedef struct {
  void* context;
  uint32_t (*read)(void* context, uint8_t field);
  void (*write)(void* context, uint8_t field, uint32_t value);
  uint32_t (*query)(void* context, uint8_t query);
  uint32_t (*command)(void* context, uint8_t command, int32_t a, int32_t b);
} XpUiReaderBookServices;
void xp_ui_reader_book_render(const XpUiReaderBookServices* services);

/* Status and row values use synchronous borrowed native resources. STATUS runs
 * under the caller's RenderLock; other operations preserve their original
 * caller context. No callback retains service or Status pointers. query never
 * changes reader state, acquires a lock, or paints. Scalar/settings queries
 * return u32 representations; PAGE/TOC_INDEX preserve signed i32 bits. PROGRESS
 * takes an IEEE f32 fraction as bits and returns IEEE f32 book progress bits.
 * TOC_INDEX/PROGRESS require a book; page/count/build queries require Section.
 * Label queries return existing StrId values, with index argument bounded by
 * text=5, spacing=4, alignment=5, orientation=4 or MORE_COUNT as applicable.
 * Commands return zero except TAKE_LINK_NOTE (a=shows-title bool, result note
 * 0=None,1=Connecting,2=Failed,3=Connected,4=LowMemory), which updates the existing
 * native note tracker. SAMPLE_SPEC copies the status spec for TITLE_MODE.
 * LABEL takes a StrId; CHAPTER and CHAPTER_COPY take a signed TOC index.
 * CHAPTER moves the native TOC title; CHAPTER_COPY preserves the status path
 * copy while its temporary TOC item is alive. AUTOMATIC, POINT_SIZE
 * and NUMBER take the selected number. Other command arguments are ignored.
 * String commands materialize the selected value using existing native strings,
 * translation resources and formatting, and may allocate or access book storage.
 * They never paint or take ModelLock. paint consumes the prepared native string
 * and borrowed Status immediately through the existing status renderer.
 * Active automatic page turn requires a nonzero u32 period; a zero period is
 * invalid: status painting is skipped and the call returns normally. Auto-option
 * indices are nonnegative. Unknown
 * operations or null/incomplete services have no effects. Invalid row indices
 * produce the original empty string. Services begin with an empty result string.
 */
typedef enum {
  XP_UI_READER_VALUE_OPERATION_STATUS = 0,
  XP_UI_READER_VALUE_OPERATION_CHAPTER = 1,
  XP_UI_READER_VALUE_OPERATION_TEXT_NAME = 2,
  XP_UI_READER_VALUE_OPERATION_TEXT_VALUE = 3,
  XP_UI_READER_VALUE_OPERATION_MORE_NAME = 4,
  XP_UI_READER_VALUE_OPERATION_MORE_VALUE = 5,
} XpUiReaderValueOperation;
typedef enum {
  XP_UI_READER_VALUE_QUERY_HAS_SECTION = 0,
  XP_UI_READER_VALUE_QUERY_PAGE = 1,
  XP_UI_READER_VALUE_QUERY_ESTIMATED_COUNT = 2,
  XP_UI_READER_VALUE_QUERY_HAS_BOOK = 3,
  XP_UI_READER_VALUE_QUERY_PROGRESS = 4,
  XP_UI_READER_VALUE_QUERY_TITLE_MODE = 5,
  XP_UI_READER_VALUE_QUERY_AUTOMATIC = 6,
  XP_UI_READER_VALUE_QUERY_TURN_DURATION = 7,
  XP_UI_READER_VALUE_QUERY_STATUS_HEIGHT = 8,
  XP_UI_READER_VALUE_QUERY_PROGRESS_HEIGHT = 9,
  XP_UI_READER_VALUE_QUERY_STATUS_MARGIN = 10,
  XP_UI_READER_VALUE_QUERY_TOC_INDEX = 11,
  XP_UI_READER_VALUE_QUERY_BOOKMARKED = 12,
  XP_UI_READER_VALUE_QUERY_BUILDING = 13,
  XP_UI_READER_VALUE_QUERY_HAS_SD_FONT = 14,
  XP_UI_READER_VALUE_QUERY_POINT_SIZE = 15,
  XP_UI_READER_VALUE_QUERY_SPACING = 16,
  XP_UI_READER_VALUE_QUERY_ALIGNMENT = 17,
  XP_UI_READER_VALUE_QUERY_SPACING_LABEL = 18,
  XP_UI_READER_VALUE_QUERY_ALIGNMENT_LABEL = 19,
  XP_UI_READER_VALUE_QUERY_MORE_COUNT = 20,
  XP_UI_READER_VALUE_QUERY_MORE_ACTION = 21,
  XP_UI_READER_VALUE_QUERY_MORE_LABEL = 22,
  XP_UI_READER_VALUE_QUERY_ORIENTATION = 23,
  XP_UI_READER_VALUE_QUERY_ORIENTATION_LABEL = 24,
  XP_UI_READER_VALUE_QUERY_AUTO_OPTION = 25,
  XP_UI_READER_VALUE_QUERY_TEXT_NAME = 26,
} XpUiReaderValueQuery;
typedef enum {
  XP_UI_READER_VALUE_COMMAND_LABEL = 0,
  XP_UI_READER_VALUE_COMMAND_UNNAMED = 1,
  XP_UI_READER_VALUE_COMMAND_CHAPTER = 2,
  XP_UI_READER_VALUE_COMMAND_SAMPLE_SPEC = 3,
  XP_UI_READER_VALUE_COMMAND_TAKE_LINK_NOTE = 4,
  XP_UI_READER_VALUE_COMMAND_CONNECTED = 5,
  XP_UI_READER_VALUE_COMMAND_LOW_MEMORY = 6,
  XP_UI_READER_VALUE_COMMAND_CONNECTING = 7,
  XP_UI_READER_VALUE_COMMAND_FAILED = 8,
  XP_UI_READER_VALUE_COMMAND_AUTOMATIC = 9,
  XP_UI_READER_VALUE_COMMAND_BOOK = 10,
  XP_UI_READER_VALUE_COMMAND_SD_FONT = 11,
  XP_UI_READER_VALUE_COMMAND_NOTO_SANS = 12,
  XP_UI_READER_VALUE_COMMAND_POINT_SIZE = 13,
  XP_UI_READER_VALUE_COMMAND_OFF = 14,
  XP_UI_READER_VALUE_COMMAND_NUMBER = 15,
  XP_UI_READER_VALUE_COMMAND_CHAPTER_COPY = 16,
} XpUiReaderValueCommand;
typedef struct {
  float book_progress, page_count;
  int32_t page, y_offset;
  uint8_t bookmarked, building, reserved[2];
} XpUiReaderStatusValue;
typedef struct {
  void* context;
  uint32_t (*query)(void* context, uint8_t query, uint32_t argument);
  uint32_t (*command)(void* context, uint8_t command, uint32_t argument);
  void (*paint)(void* context, const XpUiReaderStatusValue* status);
} XpUiReaderValueServices;
void xp_ui_reader_view_value(const XpUiReaderValueServices* services, uint8_t operation, int32_t row);
uint8_t xp_ui_reader_toolbar_style(uint8_t style);

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

#ifdef __cplusplus
#define XP_UI_READER_BASE_ASSERT(...) static_assert((__VA_ARGS__))
#define XP_UI_READER_BASE_ALIGNOF(T) alignof(T)
#else
#define XP_UI_READER_BASE_ASSERT(...) _Static_assert((__VA_ARGS__), #__VA_ARGS__)
#define XP_UI_READER_BASE_ALIGNOF(T) _Alignof(T)
#endif
XP_UI_READER_BASE_ASSERT(sizeof(XpUiReaderBaseModel) == 48 && XP_UI_READER_BASE_ALIGNOF(XpUiReaderBaseModel) == 8);
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseModel, session) == 0);
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseModel, pages_until_full_refresh) == 40);
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseModel, refresh_requests) == 44);
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseModel, end_ready) == 45);
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseModel, page_rendered) == 46);
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseModel, book_remembered) == 47);
XP_UI_READER_BASE_ASSERT(sizeof(XpUiReaderBaseServices) == 7 * sizeof(void*));
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_ALIGNOF(XpUiReaderBaseServices) == XP_UI_READER_BASE_ALIGNOF(void*));
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseServices, context) == 0 * sizeof(void*));
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseServices, read) == 1 * sizeof(void*));
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseServices, write) == 2 * sizeof(void*));
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseServices, query) == 3 * sizeof(void*));
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseServices, command) == 4 * sizeof(void*));
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseServices, touch) == 5 * sizeof(void*));
XP_UI_READER_BASE_ASSERT(offsetof(XpUiReaderBaseServices, buttons) == 6 * sizeof(void*));
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_FIELD_REMEMBERED == 0);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_FIELD_RENDERED == 1);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_FIELD_END_READY == 2);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_FIELD_COUNTER == 3);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_FIELD_FORCED == 4);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_QUERY_AT_END == 0);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_QUERY_HAS_END == 1);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_QUERY_END_MENU == 2);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_QUERY_EXISTS == 3);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_QUERY_OPEN_PATH_EMPTY == 4);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_QUERY_SUBSCRIBER_EXIT == 5);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_QUERY_SESSION_WORTHY == 6);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_QUERY_SUBSCRIBER_SESSION == 7);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_QUERY_HELD == 8);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_QUERY_LONG_BUTTON == 9);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_UPDATE == 0);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_FINISH == 1);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_BASE_ENTER == 2);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_BASE_EXIT == 3);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_LOG_ENTER == 4);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_LOG_EXIT == 5);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_CLEAR_PATH == 6);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_SAVE_STATE == 7);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_ENSURE_FONTS == 8);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_ORIENTATION_INITIAL == 9);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_LOAD_BOOK == 10);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_LOAD_FAILURE == 11);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_REMEMBER_PATH == 12);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_RECENT_ADD == 13);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_EMIT_OPEN == 14);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_RELEASE_FONTS == 15);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_SESSION_RESET == 16);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_DOCUMENT == 17);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_EMIT_SESSION == 18);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_EMIT_EXIT == 19);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_ORIENTATION_PORTRAIT == 20);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_RESET_LOAD_COUNT == 21);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_END_DROP == 22);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_END_ALLOCATE == 23);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_END_OOM == 24);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_END_LOAD == 25);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_END_RENDER == 26);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_END_INPUT == 27);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_OPEN_NEXT == 28);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_HOME == 29);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_RETURN_END == 30);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_FORMAT_INPUT == 31);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_BACK == 32);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_TURN == 33);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_SKIP == 34);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_NOTE_TURN == 35);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_HAPTIC == 36);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_CLEAR_SCREEN == 37);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_DISPLAY == 38);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_END_RENDERED == 39);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_MARK_RENDERED == 40);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_SESSION_RENDER == 41);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_RENDER_BOOK == 42);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_LOCK == 43);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_UNLOCK == 44);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_PATH_DROP == 45);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_DOCUMENT_DROP == 46);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_DOCUMENT_BYTE == 47);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_COMMAND_FORCE_REFRESH == 48);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_ENTER == 0);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_EXIT == 1);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_REMEMBER == 2);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_CLEAR_END == 3);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_MENU == 4);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_END_TURN == 5);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_LOOP == 6);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_RENDER == 7);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_FORCE_REFRESH == 8);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_FLUSH == 9);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_BASE_OPERATION_MENU_ACTIVE == 10);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_HOME_FOOTNOTES == 4);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_HOME_SYNC == 6);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_HOME_BOOKMARK == 7);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_HOME_DICTIONARY == 8);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_HOME_READER_MENU == 9);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_HOME_CREATE_CLIPPING == 11);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_LONG_MENU_SYNC == 0);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_LONG_MENU_BOOKMARK == 2);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_LONG_MENU_DICTIONARY == 3);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_LONG_MENU_CREATE_CLIPPING == 5);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_LONG_MENU_FOOTNOTES == 6);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_POWER_FOOTNOTES == 4);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_POWER_CREATE_CLIPPING == 6);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_LONG_BUTTON_SKIP == 1);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_LONG_BUTTON_ORIENTATION == 2);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_OVERLAY_NONE == 0);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_OVERLAY_TOOLBAR == 1);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_END_REDRAW == 1);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_END_OPEN_BOOK == 2);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_END_GO_HOME == 3);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_VALUE_END_LAST_PAGE == 4);

XP_UI_READER_BASE_ASSERT(XP_UI_READER_REFRESH_MANUAL == 1);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_REFRESH_COUNTER_ONLY == 2);
XP_UI_READER_BASE_ASSERT(XP_UI_READER_REFRESH_REQUEST_MASK == 3);
#undef XP_UI_READER_BASE_ASSERT
#undef XP_UI_READER_BASE_ALIGNOF

#ifdef __cplusplus
#define XP_UI_READER_LOOP_ASSERT(...) static_assert((__VA_ARGS__))
#define XP_UI_READER_LOOP_ALIGNOF(T) alignof(T)
#else
#define XP_UI_READER_LOOP_ASSERT(...) _Static_assert((__VA_ARGS__), #__VA_ARGS__)
#define XP_UI_READER_LOOP_ALIGNOF(T) _Alignof(T)
#endif
XP_UI_READER_LOOP_ASSERT(sizeof(XpUiReaderLoopServices) == 8 * sizeof(void*));
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_ALIGNOF(XpUiReaderLoopServices) == XP_UI_READER_LOOP_ALIGNOF(void*));
XP_UI_READER_LOOP_ASSERT(offsetof(XpUiReaderLoopServices, context) == 0 * sizeof(void*));
XP_UI_READER_LOOP_ASSERT(offsetof(XpUiReaderLoopServices, model_read) == 1 * sizeof(void*));
XP_UI_READER_LOOP_ASSERT(offsetof(XpUiReaderLoopServices, model_write) == 2 * sizeof(void*));
XP_UI_READER_LOOP_ASSERT(offsetof(XpUiReaderLoopServices, query) == 3 * sizeof(void*));
XP_UI_READER_LOOP_ASSERT(offsetof(XpUiReaderLoopServices, command) == 4 * sizeof(void*));
XP_UI_READER_LOOP_ASSERT(offsetof(XpUiReaderLoopServices, touch) == 5 * sizeof(void*));
XP_UI_READER_LOOP_ASSERT(offsetof(XpUiReaderLoopServices, buttons) == 6 * sizeof(void*));
XP_UI_READER_LOOP_ASSERT(offsetof(XpUiReaderLoopServices, point) == 7 * sizeof(void*));
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_RADIO_RELEASED == 0);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_APPLIED_ORIENTATION == 1);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_LAST_RENDERED == 2);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_IDLE_SPINE == 3);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_IDLE_PAGE == 4);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_SPINE == 5);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_BUILD_WIDTH == 6);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_BUILD_HEIGHT == 7);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_PARTIAL_FAILED == 8);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_RECENTS_REMOVED == 9);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_PENDING_READ_MOVE == 10);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_SHOW_BOOKMARK == 11);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_BOOKMARK_TIME == 12);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_SHOW_DICTIONARY == 13);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_DICTIONARY_TIME == 14);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_SHOW_FONT == 15);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_FONT_TIME == 16);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_OVERLAY == 17);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_LAST_TURN == 18);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_AUTO_ACTIVE == 19);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_PENDING_TURN == 20);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_PENDING_TOUCH == 21);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_TURN_DURATION == 22);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_SHOW_CLIP == 23);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_CLIP_TIME == 24);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_FIELD_FOOTNOTE_DEPTH == 25);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_LOAD_POPUP == 0);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_HAS_EPUB == 1);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_HAS_SECTION == 2);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_BUILDING == 3);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_FRAMEBUFFER == 4);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_NOW == 5);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_FREE_HEAP == 6);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_MAX_BLOCK == 7);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_PAGE == 8);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_PAGE_COUNT == 9);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_PARTIAL == 10);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_RADIO_HEAP == 11);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_SPINE_COUNT == 12);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_REMOVE_RECENTS == 13);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_MOVE_FINISHED == 14);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_IN_READ_FOLDER == 15);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_ORIENTATION == 16);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_TOOLBAR_MENU == 17);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_TOUCH_CONTROLS == 18);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_HOME_ACTION == 19);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_TOUCH_MENU == 20);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_CONFIRM_RELEASE == 21);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_BACK_RELEASE == 22);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_BUSY == 23);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_END_MENU_ACTIVE == 24);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_CONFIRM_THRESHOLD == 25);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_LONG_MENU_FUNCTION == 26);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_HAS_TOUCH == 27);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_HELD == 28);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_SHORT_POWER == 29);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_POWER_RELEASE == 30);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_DOWN_RELEASE == 31);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_QUERY_LONG_BUTTON_BEHAVIOR == 32);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_POPUP_INPUT == 0);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_FINISH == 1);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_REMEMBER == 2);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_TRY_LOCK == 3);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_UNLOCK == 4);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_RADIO_AFTER == 5);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_REPAINT_LINK_NOTE == 6);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_UPDATE == 7);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_ORIENTATION == 8);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_LOAD_PAGE == 9);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_FONT_CACHE == 10);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_PREWARM_BEGIN == 11);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_PREWARM_PAGE == 12);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_PREWARM_SCAN_END == 13);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_PREWARM_LOG == 14);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_PREWARM_DROP == 15);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_PAGE_DROP == 16);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_START_BUILD == 17);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_BUILD_FAILED_LOG == 18);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_BUILD_STARTED_LOG == 19);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_CLEAR_END == 20);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_REMOVE_RECENT == 21);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_ADD_RECENT == 22);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_OVERLAY_INPUT == 23);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_DISCARD_OVERLAY == 24);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_CLIP == 25);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_TURN == 26);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_NOTE_TURN == 27);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_HAPTIC == 28);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_END_MENU == 29);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_CONFIRM_LONG == 30);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_BOOKMARK == 31);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_SYNC == 32);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_DICTIONARY == 33);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_RESTORE_POSITION == 34);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_FOOTNOTES == 35);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_OPEN_OVERLAY == 36);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_OPEN_MENU == 37);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_MATCH_LINK == 38);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_NAVIGATE_LINK == 39);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_BACK_NAVIGATION == 40);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_BUILD_TICK == 41);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_END_TURN == 42);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_COMMAND_SKIP == 43);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_POINT_LONG == 0);
XP_UI_READER_LOOP_ASSERT(XP_UI_READER_LOOP_POINT_TAP == 1);
#undef XP_UI_READER_LOOP_ASSERT
#undef XP_UI_READER_LOOP_ALIGNOF

#ifdef __cplusplus
#define XP_UI_READER_OVERLAY_ASSERT(...) static_assert((__VA_ARGS__))
#define XP_UI_READER_OVERLAY_ALIGNOF(T) alignof(T)
#else
#define XP_UI_READER_OVERLAY_ASSERT(...) _Static_assert((__VA_ARGS__), #__VA_ARGS__)
#define XP_UI_READER_OVERLAY_ALIGNOF(T) _Alignof(T)
#endif
XP_UI_READER_OVERLAY_ASSERT(sizeof(XpUiReaderOverlayServices) == 5 * sizeof(void*));
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_ALIGNOF(XpUiReaderOverlayServices) ==
                            XP_UI_READER_OVERLAY_ALIGNOF(void*));
XP_UI_READER_OVERLAY_ASSERT(offsetof(XpUiReaderOverlayServices, context) == 0 * sizeof(void*));
XP_UI_READER_OVERLAY_ASSERT(offsetof(XpUiReaderOverlayServices, read) == 1 * sizeof(void*));
XP_UI_READER_OVERLAY_ASSERT(offsetof(XpUiReaderOverlayServices, write) == 2 * sizeof(void*));
XP_UI_READER_OVERLAY_ASSERT(offsetof(XpUiReaderOverlayServices, query) == 3 * sizeof(void*));
XP_UI_READER_OVERLAY_ASSERT(offsetof(XpUiReaderOverlayServices, command) == 4 * sizeof(void*));
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_EVENT_NONE == 0);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_EVENT_DISMISS == 1);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_EVENT_TOOL == 2);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_EVENT_PREV_CHAPTER == 3);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_EVENT_NEXT_CHAPTER == 4);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_EVENT_SCRUB == 5);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_EVENT_ROW == 6);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_SWIPE_NONE == 0);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_SWIPE_LEFT == 1);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_SWIPE_RIGHT == 2);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_SWIPE_UP == 3);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_SWIPE_DOWN == 4);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_MODE_NONE == 0);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_MODE_TOOLBAR == 1);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_MODE_CONTENTS == 2);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_MODE_TEXT == 3);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_MODE_MORE == 4);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_FIELD_OVERLAY == 0);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_FIELD_SPINE == 1);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_FIELD_NEXT_PAGE == 2);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_FIELD_FOCUS == 3);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_FIELD_INDEX == 4);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_FIELD_CURSOR == 5);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_FIELD_HOLD == 6);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_FIELD_STORED == 7);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_HAS_TOOLBAR == 0);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_POPUP_ACTIVE == 1);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_SPINE_COUNT == 2);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_TOC_COUNT == 3);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_MORE_COUNT == 4);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_ROWS == 5);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_ROUTED == 6);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_VALUE == 7);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_PERMILLE == 8);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_X == 9);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_Y == 10);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_WIDTH == 11);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_HEIGHT == 12);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_FOCUS_READING == 13);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_PRESSED == 14);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_RELEASED == 15);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_POPUP_INPUT == 0);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_LOCK == 1);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_UNLOCK == 2);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_SETTLE == 3);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_PAINT_POPUP == 4);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_RESTORE == 5);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_STORE == 6);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_RENDER == 7);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_PUSH == 8);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_UPDATE == 9);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_CLOSE == 10);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_OPEN == 11);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_CLEAR_DEFERRED == 12);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_DROP_SECTION == 13);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_ROUTE == 14);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_DISMISS_POPUP == 15);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_DISCARD == 16);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_START_PICKER == 17);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_APPLY_TEXT == 18);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_TOOLBAR_BEGIN == 19);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_SET_FOCUS_READING == 20);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_APPLY_LIVE == 21);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_TEXT_POPUP == 22);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_MORE == 23);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_TOC_ITEM == 24);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_TOC_ANCHOR == 25);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_TOC_DROP == 26);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_SCROLL == 27);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_TAKE_SWIPE == 28);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_SAMPLE_BUTTONS == 29);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_TAKE_HELD_MS == 30);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_OPERATION_INPUT == 0);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_OPERATION_POPUP_CHANGED == 1);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_OPERATION_PICKER_RESULT == 2);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_TEXT_ROWS == 5);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_HOLD_MS == 1500);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_HOLD_ROWS == 10);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_HAS_SECTION == 16);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_PAGE_STALE == 17);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_TOUCH == 18);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_CURRENT_TOC == 19);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_IS_A4 == 20);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_QUERY_IS_XTEINK == 21);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_RESET_HOME == 31);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_CREATE_TOOLBAR == 32);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_RESET_NAVIGATION == 33);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_BUILD_MORE == 34);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_DROP_TOOLBAR == 35);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_FORCE_COUNTER == 36);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_COMMAND_DISPLAY_FAST == 37);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_OPERATION_OPEN_NONE == 3);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_OPERATION_OPEN_TOOLBAR == 4);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_OPERATION_OPEN_CONTENTS == 5);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_OPERATION_OPEN_TEXT == 6);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_OPERATION_OPEN_MORE == 7);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_OPERATION_CLOSE == 8);
#undef XP_UI_READER_OVERLAY_ASSERT
#undef XP_UI_READER_OVERLAY_ALIGNOF

#if defined(__cplusplus)
#define XP_UI_READER_IDLE_ASSERT(x) static_assert((x), #x)
#else
#define XP_UI_READER_IDLE_ASSERT(x) _Static_assert((x), #x)
#endif
XP_UI_READER_IDLE_ASSERT(sizeof(XpUiReaderIdleSnapshot) == 40);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, spine) == 0);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, spine_count) == 4);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, confirm_threshold) == 8);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, busy) == 12);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, has_epub) == 13);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, load_popup) == 14);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, remembered) == 15);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, end_options) == 16);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, touch) == 17);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, remote) == 18);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, home_action) == 19);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, edges) == 20);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, held_buttons) == 21);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, overlay) == 22);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, automatic) == 23);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, pending_turn) == 24);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, bookmark_notice) == 25);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, dictionary_notice) == 26);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, font_notice) == 27);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, clipping_notice) == 28);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, read_move) == 29);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, recents_removed) == 30);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, applied_orientation) == 31);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, orientation) == 32);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, long_button) == 33);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, short_power) == 34);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, tilt) == 35);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, footnote_depth) == 36);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, touch_controls) == 37);
XP_UI_READER_IDLE_ASSERT(offsetof(XpUiReaderIdleSnapshot, reserved) == 38);
XP_UI_READER_IDLE_ASSERT(XP_UI_READER_IDLE_ADMITTED == 1);
XP_UI_READER_IDLE_ASSERT(XP_UI_READER_IDLE_SCREEN_LONG == 2);
#undef XP_UI_READER_IDLE_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_VIEW_ASSERT(x) static_assert(x, #x)
static_assert(alignof(XpUiReaderOverlayView) == 4, "overlay view alignment");
static_assert(alignof(XpUiReaderOverlayViewServices) == alignof(void*), "overlay services alignment");
#else
#define XP_UI_READER_VIEW_ASSERT(x) _Static_assert(x, #x)
_Static_assert(_Alignof(XpUiReaderOverlayView) == 4, "overlay view alignment");
_Static_assert(_Alignof(XpUiReaderOverlayViewServices) == _Alignof(void*), "overlay services alignment");
#endif
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayView, active_tool) == 0);
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayView, selected_index) == 4);
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayView, item_count) == 8);
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayView, progress_permille) == 12);
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayView, kind) == 16);
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayView, dense) == 17);
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayView, reserved) == 18);
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayView, page_info) == 20);
XP_UI_READER_VIEW_ASSERT(sizeof(XpUiReaderOverlayView) == 60);
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayViewServices, context) == 0 * sizeof(void*));
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayViewServices, query) == 1 * sizeof(void*));
XP_UI_READER_VIEW_ASSERT(offsetof(XpUiReaderOverlayViewServices, paint) == 2 * sizeof(void*));
XP_UI_READER_VIEW_ASSERT(sizeof(XpUiReaderOverlayViewServices) == 3 * sizeof(void*));
#undef XP_UI_READER_VIEW_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_NAV_ASSERT(x) static_assert(x, #x)
static_assert(alignof(XpUiReaderNavServices) == alignof(void*), "navigation services alignment");
#else
#define XP_UI_READER_NAV_ASSERT(x) _Static_assert(x, #x)
_Static_assert(_Alignof(XpUiReaderNavServices) == _Alignof(void*), "navigation services alignment");
#endif
XP_UI_READER_NAV_ASSERT(sizeof(XpUiReaderNavServices) == 7 * sizeof(void*));
XP_UI_READER_NAV_ASSERT(offsetof(XpUiReaderNavServices, context) == 0 * sizeof(void*));
XP_UI_READER_NAV_ASSERT(offsetof(XpUiReaderNavServices, read) == 1 * sizeof(void*));
XP_UI_READER_NAV_ASSERT(offsetof(XpUiReaderNavServices, write) == 2 * sizeof(void*));
XP_UI_READER_NAV_ASSERT(offsetof(XpUiReaderNavServices, query) == 3 * sizeof(void*));
XP_UI_READER_NAV_ASSERT(offsetof(XpUiReaderNavServices, command) == 4 * sizeof(void*));
XP_UI_READER_NAV_ASSERT(offsetof(XpUiReaderNavServices, href) == 5 * sizeof(void*));
XP_UI_READER_NAV_ASSERT(offsetof(XpUiReaderNavServices, href_len) == 6 * sizeof(void*));
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_SPINE == 0);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_CACHED_SPINE == 1);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_NEXT_PAGE == 2);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_CACHED_COUNT == 3);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_HAS_CACHED_OFFSET == 4);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_CACHED_OFFSET == 5);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_HAS_CURRENT_OFFSET == 6);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_CURRENT_OFFSET == 7);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_PENDING_OFFSET == 8);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_PERCENT_JUMP == 9);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_SPINE_PROGRESS == 10);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_FOOTNOTE_DEPTH == 11);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_SAVED_SPINE0 == 12);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_SAVED_PAGE0 == 13);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_SAVED_SPINE1 == 14);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_SAVED_PAGE1 == 15);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_SAVED_SPINE2 == 16);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_FIELD_SAVED_PAGE2 == 17);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_HAS_BOOK == 0);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_BOOK_SIZE == 1);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_SPINE_COUNT == 2);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_FIND_SPINE == 3);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_CUMULATIVE == 4);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_HAS_SECTION == 5);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_SECTION_PAGE == 6);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_SECTION_COUNT == 7);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_ESTIMATED_COUNT == 8);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_BUILDING == 9);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_MAP_OFFSET == 10);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_GET_OFFSET == 11);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_OFFSET_VALUE == 12);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_FALLBACK_SPINE == 13);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_FALLBACK_PAGE == 14);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_SYNC_HAS_OFFSET == 15);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_SYNC_OFFSET == 16);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_SYNC_SPINE == 17);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_SYNC_PAGE == 18);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_SYNC_COUNT == 19);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_SYNC_HAS_PROGRESS == 20);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_QUERY_RESOLVE_HREF == 21);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_LOCK == 0);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_UNLOCK == 1);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_DROP_SECTION == 2);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_UPDATE == 3);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_SET_SECTION_PAGE == 4);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_FALLBACK == 5);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_PREPARE_ANCHOR == 6);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_MOVE_ANCHOR == 7);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_LOG_UNRESOLVED == 8);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_LOG_SAVED == 9);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_LOG_RESTORED == 10);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_LOG_NAVIGATED == 11);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_COMMAND_SAVE_PROGRESS == 12);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_OPERATION_PERCENT == 0);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_OPERATION_PROGRESS == 1);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_OPERATION_DEFERRED == 2);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_OPERATION_CLEAR == 3);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_OPERATION_SAVE == 4);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_OPERATION_REMEMBER == 5);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_OPERATION_HREF == 6);
XP_UI_READER_NAV_ASSERT(XP_UI_READER_NAV_OPERATION_RESTORE == 7);
#undef XP_UI_READER_NAV_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_BOOK_ASSERT(x) static_assert(x, #x)
static_assert(alignof(XpUiReaderBookServices) == alignof(void*), "book services alignment");
#else
#define XP_UI_READER_BOOK_ASSERT(x) _Static_assert(x, #x)
_Static_assert(_Alignof(XpUiReaderBookServices) == _Alignof(void*), "book services alignment");
#endif
XP_UI_READER_BOOK_ASSERT(sizeof(XpUiReaderBookServices) == 5 * sizeof(void*));
XP_UI_READER_BOOK_ASSERT(offsetof(XpUiReaderBookServices, context) == 0 * sizeof(void*));
XP_UI_READER_BOOK_ASSERT(offsetof(XpUiReaderBookServices, read) == 1 * sizeof(void*));
XP_UI_READER_BOOK_ASSERT(offsetof(XpUiReaderBookServices, write) == 2 * sizeof(void*));
XP_UI_READER_BOOK_ASSERT(offsetof(XpUiReaderBookServices, query) == 3 * sizeof(void*));
XP_UI_READER_BOOK_ASSERT(offsetof(XpUiReaderBookServices, command) == 4 * sizeof(void*));
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_SPINE == 0);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_CACHED_SPINE == 1);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_CACHED_COUNT == 2);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_HAS_CACHED_OFFSET == 3);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_CACHED_OFFSET == 4);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_HAS_PENDING_PAGE == 5);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_PENDING_PAGE == 6);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_NEXT_PAGE == 7);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_HAS_PENDING_OFFSET == 8);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_PENDING_OFFSET == 9);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_PENDING_PERCENT == 10);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_SPINE_PROGRESS == 11);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_AUTOMATIC == 12);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_SYNC_ERROR == 13);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_PARTIAL_START_FAILED == 14);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_BUILD_POPUP == 15);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_BUILD_WIDTH == 16);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_BUILD_HEIGHT == 17);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_PAGE_STALE == 18);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_RETRY == 19);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_CURRENT_OFFSET == 20);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_MARGIN_LEFT == 21);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_MARGIN_TOP == 22);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_LAST_RENDER == 23);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_LAST_SAVED_SPINE == 24);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_LAST_SAVED_PAGE == 25);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_LAST_SAVED_COUNT == 26);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_SCREENSHOT == 27);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_BOOKMARK_NOTICE == 28);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_BOOKMARK_REMOVED == 29);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_DICTIONARY_NOTICE == 30);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_FONT_NOTICE == 31);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_FONT_NOTICE_MS == 32);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_CLIPPING_NOTICE == 33);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_OVERLAY == 34);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_FIELD_STORED == 35);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_HAS_BOOK == 0);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_SPINE_COUNT == 1);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_HAS_SECTION == 2);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_MARGIN_TOP == 3);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_MARGIN_RIGHT == 4);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_MARGIN_BOTTOM == 5);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_MARGIN_LEFT == 6);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_SCREEN_MARGIN == 7);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_STATUS_HEIGHT == 8);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_PROGRESS_HEIGHT == 9);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_STATUS_MARGIN == 10);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_WIDTH == 11);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_HEIGHT == 12);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_PARTIAL == 13);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_BUILDING == 14);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_COMPLETE == 15);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_PAGE_COUNT == 16);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_PAGE == 17);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_HAS_ANCHOR == 18);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_HAS_HTML == 19);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_FIND_ANCHOR == 20);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_PAGE_FOR_ANCHOR == 21);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_HAS_TTF == 22);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_NOW == 24);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_POPUP_ACTIVE == 25);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_USES_TOOLBAR == 26);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_HAS_PAGE == 28);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_PAGE_OFFSET == 29);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_QUERY_ESTIMATED_COUNT == 30);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_CLEAR_METADATA == 0);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_RADIO_SETTLE == 1);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_SETTLE_OVERLAY == 2);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_SAMPLE_MARGINS == 3);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_SET_MARGIN == 4);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_SET_SPEC == 5);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_GET_PATH == 6);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_DROP_PATH == 7);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_LOADING == 8);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_NEW_SECTION == 9);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOAD_CACHE == 10);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_PARTIAL == 11);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_NO_CACHE == 12);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_COUNTER == 13);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_ROOM == 14);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_DROP_SECTION == 15);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOAN_BEGIN == 16);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOAN_END == 17);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOAN_DROP == 18);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_CREATE_ALL == 19);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_PERSIST_FAILED == 20);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_DEFERRED == 21);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_CUMULATIVE == 22);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_RELEASE_FONT_CACHE == 23);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_HEAP == 24);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_START_WITH_POPUP == 25);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_START_FAILED == 26);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_REACHED_OFFSET == 27);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_SHOW_BUILD_POPUP == 28);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_BUILD_MORE == 29);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_STEP_FAILED == 30);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_CACHE_FOUND == 31);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_PAGE == 32);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_MAP_OFFSET == 33);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_CLEAR_DEFERRED == 34);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_ANCHOR == 35);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_CLEAR_ANCHOR == 36);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_START_PARTIAL == 37);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_PARTIAL_START_FAILED == 38);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_APPLY_DEFERRED == 39);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_CLEAR_SCREEN == 40);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_EMPTY == 41);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_STATUS_BAR == 42);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_DISPLAY == 43);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_BOUNDS == 44);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_BOOKMARK_FLAG == 45);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOAD_PAGE == 46);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_DROP_PAGE == 47);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_ASSIGN_METADATA == 48);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_PAGE_LOAD_FAILED == 49);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_METADATA_FAILED == 50);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_ABANDON_BUILD == 51);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_CLEAR_CACHE == 52);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_RETRY_FAILED == 53);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_UPDATE == 54);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_DISCARD_OVERLAY == 55);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_RENDER_CONTENTS == 56);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_LOG_RENDERED == 57);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_MARK_RENDERED == 58);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_SAVE_PROGRESS == 59);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_SCREENSHOT == 60);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_STORE == 61);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_RENDER_OVERLAY == 62);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_RENDER_POPUP == 63);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_PUSH_REFRESH == 64);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_BACK_HINTS == 65);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_POPUP_SYNC == 66);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_POPUP_RADIO_ERROR == 67);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_POPUP_BUILD_ERROR == 68);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_POPUP_INDEX == 69);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_POPUP_BOOKMARK_REMOVED == 70);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_POPUP_BOOKMARK_ADDED == 71);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_POPUP_DICTIONARY == 72);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_POPUP_FONT == 73);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_POPUP_CLIPPING == 74);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_DRAW_EMPTY == 75);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_DRAW_BOUNDS == 76);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_DRAW_LOAD_ERROR == 77);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_SAMPLE_FONT_CACHE == 78);
XP_UI_READER_BOOK_ASSERT(XP_UI_READER_BOOK_COMMAND_TAKE_FONT_NOTICE == 79);
#undef XP_UI_READER_BOOK_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_VALUE_ASSERT(x) static_assert(x, #x)
static_assert(alignof(XpUiReaderValueServices) == alignof(void*), "value services alignment");
static_assert(alignof(XpUiReaderStatusValue) == 4, "status value alignment");
#else
#define XP_UI_READER_VALUE_ASSERT(x) _Static_assert(x, #x)
_Static_assert(_Alignof(XpUiReaderValueServices) == _Alignof(void*), "value services alignment");
_Static_assert(_Alignof(XpUiReaderStatusValue) == 4, "status value alignment");
#endif
XP_UI_READER_VALUE_ASSERT(sizeof(XpUiReaderValueServices) == 4 * sizeof(void*));
XP_UI_READER_VALUE_ASSERT(sizeof(XpUiReaderStatusValue) == 20);
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderValueServices, context) == 0 * sizeof(void*));
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderValueServices, query) == 1 * sizeof(void*));
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderValueServices, command) == 2 * sizeof(void*));
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderValueServices, paint) == 3 * sizeof(void*));
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderStatusValue, book_progress) == 0);
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderStatusValue, page_count) == 4);
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderStatusValue, page) == 8);
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderStatusValue, y_offset) == 12);
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderStatusValue, bookmarked) == 16);
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderStatusValue, building) == 17);
XP_UI_READER_VALUE_ASSERT(offsetof(XpUiReaderStatusValue, reserved) == 18);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_OPERATION_STATUS == 0);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_OPERATION_CHAPTER == 1);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_OPERATION_TEXT_NAME == 2);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_OPERATION_TEXT_VALUE == 3);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_OPERATION_MORE_NAME == 4);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_OPERATION_MORE_VALUE == 5);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_HAS_SECTION == 0);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_PAGE == 1);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_ESTIMATED_COUNT == 2);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_HAS_BOOK == 3);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_PROGRESS == 4);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_TITLE_MODE == 5);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_AUTOMATIC == 6);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_TURN_DURATION == 7);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_STATUS_HEIGHT == 8);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_PROGRESS_HEIGHT == 9);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_STATUS_MARGIN == 10);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_TOC_INDEX == 11);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_BOOKMARKED == 12);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_BUILDING == 13);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_HAS_SD_FONT == 14);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_POINT_SIZE == 15);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_SPACING == 16);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_ALIGNMENT == 17);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_SPACING_LABEL == 18);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_ALIGNMENT_LABEL == 19);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_MORE_COUNT == 20);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_MORE_ACTION == 21);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_MORE_LABEL == 22);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_ORIENTATION == 23);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_ORIENTATION_LABEL == 24);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_AUTO_OPTION == 25);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_QUERY_TEXT_NAME == 26);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_LABEL == 0);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_UNNAMED == 1);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_CHAPTER == 2);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_SAMPLE_SPEC == 3);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_TAKE_LINK_NOTE == 4);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_CONNECTED == 5);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_LOW_MEMORY == 6);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_CONNECTING == 7);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_FAILED == 8);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_AUTOMATIC == 9);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_BOOK == 10);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_SD_FONT == 11);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_NOTO_SANS == 12);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_POINT_SIZE == 13);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_OFF == 14);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_NUMBER == 15);
XP_UI_READER_VALUE_ASSERT(XP_UI_READER_VALUE_COMMAND_CHAPTER_COPY == 16);
#undef XP_UI_READER_VALUE_ASSERT
