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
/* Returns 1 for HALF, 0 for FAST; advances the existing counter. */
uint8_t xp_ui_reader_refresh(int32_t* remaining, int32_t frequency);

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

/* Called with the original RenderLock held. Resource context owns the original
 * scoped buffers/guards, whose cleanup runs after this synchronous plan returns.
 * Query/operation output pointers must not be retained. The operation borrows
 * existing Rust page/strip execution through the C++ resource binding. */

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

/* All seven callbacks are required and synchronous; context may be null only
 * if every callback accepts it. Model access copies scalars under ModelLock.
 * Resource callbacks run outside the critical section. TRY_LOCK opens one scope
 * even on failure; exactly one UNLOCK closes it before any return. Null or
 * incomplete services produce no probes/effects; callbacks retain no arguments. */

/* Idle admission only while a reader render owns the framebuffer. Nonzero
 * results require preserving the consuming mapped probes in their original order. */

/* SCREEN_LONG asks the caller to probe wasScreenLongPress before the Back probes. */
enum { XP_UI_READER_IDLE_ADMITTED = 1, XP_UI_READER_IDLE_SCREEN_LONG = 2 };

/* Stable event IDs; checked against the C++ provider. */

/* Stable swipe IDs; checked against the C++ provider. */

/* Stable mode IDs; checked against the C++ provider. */

/* Signed fields use uint32 bits; read/write is POD-only. */

enum { XP_UI_READER_OVERLAY_TEXT_ROWS = 5, XP_UI_READER_OVERLAY_HOLD_MS = 1500, XP_UI_READER_OVERLAY_HOLD_ROWS = 10 };

/* All callbacks required and synchronous; null/incomplete services or invalid
 * operation have no effects. Context may be null only if every callback accepts it.
 * Scalar read/write use short ModelLock; resource callbacks run outside it.
 * Rust guards pair LOCK/UNLOCK and TOC_ITEM/TOC_DROP, including early returns. Callbacks retain no context
 * or arguments; owner-bound child callbacks create a fresh service context.
 * TOC_ITEM owns its entry until TOC_DROP after the update request. */

/* Called under RenderLock, never ModelLock. Null/missing callbacks produce no paint.
 * query supplies scalar state or a borrowed resource result; CHAPTER_TITLE prepares
 * a string until paint returns. PROGRESS uses IEEE-754 f32 bits in both directions.
 * paint borrows the view only for its synchronous call; no pointer is retained. */

/* Returns a decoration resource ID. Text mode takes a row index;
 * More mode takes the persisted menu-action ID. */

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

/* Percent: a=percent; Save: a=spine,b=page (count is a context resource);
 * Href: a=save-position; other arguments ignored. Deferred/Save return bool. */

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

/* EPUB lifetime services are synchronous borrowed native resources. No service
 * or context pointer escapes. read/write copy only POD under short ModelLock;
 * commands and queries run outside that critical section. TEARDOWN is called
 * with the original ActivityManager RenderLock already held and must not acquire
 * another. LOAD/PROGRESS retain their original caller lock context.
 *
 * Fields carry i32/u32 bits or 0/1 flags. HAS_CACHED_OFFSET writes accept zero
 * to reset the optional; CACHED_OFFSET writes set its u32 value. Query arguments
 * are ignored except PROGRESS_BYTE(index 0..9), valid after READ_PROGRESS.
 * CACHE_PRESENT requires a temporary loaded book and may allocate a path and
 * read storage; TEXT_REFERENCE requires the installed book and may read its
 * metadata. EMBEDDED_STYLE and HAS_BOOK return scalar facts. Queries do not
 * change reader state, acquire locks, paint or retain resources.
 *
 * NEW_LOADED allocates the existing native Epub owner and returns bool;
 * DROP_LOADED releases its temporary slot even after allocation failure or move.
 * LOAN_BEGIN/LOAN_DROP lend and restore the framebuffer. LOAD(a=embedded-style
 * enabled) reads/parses storage and returns bool; MOVE_LOADED transfers the
 * temporary unique owner to the existing shared owner. PROTECTION_ERROR copies
 * its error string. INSTALL_EXTRACTOR retains only the installed Epub pointer
 * and a stateless extraction function; REMOVE_EXTRACTOR clears it before drop.
 * OPEN_PROGRESS constructs a native HalFile and returns open success; it may
 * allocate a path and read storage. READ_PROGRESS fills its ten-byte buffer and
 * returns the signed byte count as u32 bits. DROP_FILE releases the file even
 * after open failure. The file stays open through the rest of LOAD, as before.
 * PREPARE_MOVE constructs source/cache/destination strings; DROP_MOVE_PATHS
 * destroys them. MOVE_BOOK consumes those borrowed paths after DROP_BOOK has
 * released Epub. Rust guards balance all four temporary resource pairs.
 *
 * CLEAR_IMAGE_FAILURES, DISABLE_FAST, SETTLE_OVERLAY and DISCARD_OVERLAY preserve
 * existing resource effects; SETUP_CACHE, LOAD_LINKS, LOAD_BOOKMARKS,
 * LOAD_CLIPPINGS (bool), SAVE_LINKS, UNLOAD_CLIPPINGS and MOVE_BOOK may allocate
 * or access storage. DROP_SECTION/DROP_BOOK destroy their native owners.
 * POPUP_INDEX paints using the existing UI; RADIO_AFTER performs existing radio
 * work. LOG commands emit original diagnostics; LOG_TEXT_REFERENCE takes its
 * signed spine index in a. All other arguments, including b, are ignored.
 * LOAD returns bool; TEARDOWN/PROGRESS return zero. Unknown operations and
 * null/incomplete services have no effects. */

/* Chapter build services borrow their context synchronously; no pointer escapes.
 * ROOM waits for radio admission; HEAP evaluates/reclaims the existing idle-strip
 * buffers; POPUP paints the pending indexing notice; BUTTONS samples debounced
 * buttons; BACKGROUND executes one existing bounded build step; WANTED and DELAY
 * return scheduling predicates. Unknown operations or null/incomplete services
 * do nothing and return zero. Boolean operations return 0/1; POPUP/BACKGROUND
 * return zero. No Rust allocation or persistent state is introduced.
 *
 * read/write carry signed i32 bits or 0/1 flags. Fields through BUFFER_STALE are
 * reader scalars under the existing short ModelLock. Remaining fields are
 * read-only native facts, used in the original caller lock context. REFRESH is
 * accessed only after PAINT_INDEX; no render-frame reference is captured early.
 * BACKGROUND owns TRY_LOCK/UNLOCK with a Rust guard, including failed try-locks.
 * Other operations retain the caller's existing locking and never acquire it.
 *
 * RADIO_STATUS samples running/stopping/starting in bits 0/1/2. RADIO_BEFORE
 * requests release and returns whether the original result is not NotHeld.
 * CLOCK samples wrapping u32 milliseconds; SLEEP(a=milliseconds) waits.
 * RELEASE_STRIPS returns whether an idle buffer was released (false in builds
 * without reader strips). READ_FREE/READ_BLOCK sample bytes and retain the last
 * readings solely for LOG_RECLAIMED(a=old free,b=old block).
 * LOG_ROOM_DENIED(a=timeout ms) and LOG_BUILD_FAILED retain original diagnostics. PAINT_INDEX paints the existing
 * popup. TRY_LOCK constructs a native try-lock and returns ownership; UNLOCK
 * destroys it even after a failed acquisition. BUTTON(a=0..6) samples Back,
 * Confirm, Left, Right, Up, Down, Power in that order. RAW samples raw contact;
 * LOAN_COUNT reads the framebuffer loan counter; BUILD_SOME performs the original
 * two-page step with raw-contact callback and returns success. DROP_SECTION
 * destroys its existing owner; REPOSITION invokes deferred navigation and returns
 * bool; UPDATE requests repaint; RADIO_HEAP samples existing radio ownership.
 * Other command arguments are ignored. All command callbacks run outside the
 * short model critical section, preserving resource/render-lock ordering. */

/* Reader settings services borrow the context synchronously. Operations perform
 * initial/changed orientation, automatic-turn selection, text application, live
 * text application, a text-row popup or its retained size/spacing/alignment
 * result. The argument is the orientation/auto choice, popup row or result index;
 * ignored by INITIAL_ORIENTATION/TEXT/LIVE_TEXT. No pointer is retained by Rust.
 * Null/incomplete services and unknown operations have no effects.
 *
 * read/write carry scalar u32/i32 bits. Reader fields through AUTO_ACTIVE are
 * copied under the short ModelLock; settings/resource facts are sampled outside
 * it. HAS_SECTION/PAGE_COUNT/PAGE require the original caller resource context.
 * SIZE_COUNT reads the prepared size vector or the callback's borrowed captured
 * vector, selected by the native context. Counts are bounded to i32. Enum setting
 * writes retain the original uint8 truncation. RATE_COUNT/RATE use the existing
 * five positive rates; zero rates are defensively rejected without panicking.
 *
 * LOCK(a=0 ordinary, a=1 owner-aware) constructs the original RenderLock; UNLOCK
 * releases it. Every acquisition is paired by a Rust guard. REMEMBER_OFFSET,
 * DROP_SECTION, SAVE_SETTINGS, APPLY_ORIENTATION, ENSURE_FONTS, DISCARD_OVERLAY,
 * UPDATE and BASE_ORIENTATION are existing resource effects. CLOCK samples u32
 * milliseconds. RATE(a=index) reads a rate. PREPARE_SIZES constructs the existing
 * font-size vector and an empty label vector; DROP_SIZES destroys both before
 * PAINT_POPUP. RESERVE_LABELS(a=count), ADD_LABEL(a=size index), SNAP_SIZE and
 * SIZE_AT(a=index) preserve the existing strings and font helper behavior.
 * SHOW_SIZES(a=current index) copies the prepared sizes into the retained native
 * callback exactly as before; its callback retains only owner and that vector,
 * then invokes SIZE_RESULT with a synchronous borrow after this frame is gone.
 * SHOW_SPACING/SHOW_ALIGNMENT take current index in a; their callbacks retain
 * only the owner and invoke the corresponding RESULT operation. PAINT_POPUP
 * invokes the existing locked popup painter. Other command arguments are ignored.
 * All allocation, storage, font, lock and paint commands run outside ModelLock.
 * No persistent state, new heap owner or input-wait dependency is introduced. */

/* Reader menu parents: synchronous borrowed context, no pointer retained by Rust.
 * OPEN/BUILD_MORE ignore argument; CONFIRM receives the existing MenuAction value;
 * ACTIVATE_MORE receives row; ORIENTATION_RESULT/AUTO_RESULT receive picker index.
 * Other RESULT operations borrow the matching native ActivityResult for this call.
 * MENU_RESULT always reads menu payload, even when cancelled, matching original;
 * CHAPTER/PERCENT/PROGRESS results inspect their payload only on the success path.
 * TEXT/REOPEN ignore payload. SHOW commands retain only the activity owner, then
 * enter the matching RESULT operation when the child returns; a child, popup or
 * calling stack context is never captured. All action IDs match the public enum.
 * Null/incomplete services and unknown operations have no effects.
 *
 * Fields through AUTO_OPTION are copied as POD under short ModelLock; other
 * fields are native resource/settings/result facts read outside it. Signed values
 * preserve i32 bits and flags use 0/1. TEMPORARY_COUNT is a context-local u16 cache
 * backup used by SAVE_PROGRESS; its write is outside ModelLock. STRIPS_ENABLED is
 * a compile-time flag so the original strip-only lock is absent when strips are
 * disabled. LOCK/UNLOCK balance a native RenderLock using Rust guards on all exits.
 *
 * NEW_TOOLBAR/BEGIN_TOOLBAR, DISCARD/UPDATE, APPLY_ORIENTATION(a), AUTO_TURN(a),
 * OPEN_MENU, REMEMBER_OFFSET, DROP_SECTION, CLEAR_DEFERRED, ASSIGN_ANCHOR (from
 * ChapterResult), FOOTNOTES(a=reopen), DICTIONARY, HOME, CLEAR_CACHE/SETUP_CACHE,
 * SYNC, START_CLIP, OPEN_CLIPPINGS, BOOKMARK, LOAD_BOOKMARKS, APPLY_PROGRESS (from
 * ProgressResult), JUMP_PERCENT (from PercentResult), SETTLE, RESTORE_PAGE(false),
 * PAINT_POPUP, TOGGLE_BLUETOOTH, RENDER_OVERLAY, PUSH_OVERLAY, SAVE_SETTINGS and
 * DISPLAY_FAST are existing native resource effects. CLOCK returns wrapping u32.
 * LIGHT_GET returns bool; LIGHT_SET(a=bool) writes the existing HAL. SAVE_PROGRESS
 * takes a=u16 spine,b=u16 page plus TEMPORARY_COUNT and returns bool; LOG_SAVE_FAILED
 * emits its original error. No command executes while holding ModelLock.
 *
 * PREPARE_POSITION snapshots the original ChapterPosition; PERCENT computes its
 * current percent. FOOTNOTES_PRESENT samples metadata under the caller lock.
 * CURRENT_TOC returns the current index and may allocate/read the cached Section.
 * SHOW_MENU(a=percent,b=footnotes), SHOW_CHAPTER(a=toc), SHOW_TEXT,
 * SHOW_PERCENT(a=percent), SHOW_QR and SHOW_BOOKMARKS construct the original child
 * with unchanged resource arguments and install MENU/CHAPTER/TEXT/PERCENT/REOPEN/
 * PROGRESS result callbacks respectively. PREPARE_QR constructs the original page
 * text string and returns nonempty; DROP_QR releases it even when empty. A Rust
 * guard releases text before the fallback update or after child construction.
 *
 * BUILD_ITEMS(a=footnotes) invokes the existing menu-item builder; ACTION_AT(a=row)
 * reads its action and ERASE_ITEM(a=row) removes a selected existing item. Rust
 * owns exclusion of Chapter/Text. SHOW_ORIENTATION(a=current) installs the retained
 * orientation-result callback. PREPARE_LABELS/DROP_LABELS own the original auto
 * picker label vector; RESERVE_LABELS(a=count), ADD_OFF_LABEL, ADD_RATE_LABEL(a=index)
 * and SHOW_AUTO(a=selection) preserve its reserve/order/strings and owner-only
 * auto-result callback. Its Rust guard holds labels through PAINT_POPUP, as before.
 * Other arguments are ignored. Resource allocations/failures, font/settings IO,
 * original RenderLock spans and repaint requests are preserved. */

/* Protected-open recovery: synchronous borrowed context, no pointer retained by Rust.
 * LOAD and RENDER return handled; other operations return zero. LOAD_RESULT takes a=picker
 * index,b=original offerSync boolean captured when the popup was created.
 * RETRY_RESULT takes a=index. Other arguments are ignored. WIFI_RESULT borrows
 * ActivityResult only for this call. RENDER paints an active failure popup and
 * returns one, otherwise returns zero for the native facade to delegate base
 * rendering after the policy stack unwinds. Native callbacks capture owner and the original
 * offerSync flag, never the stack context, child, popup, result or lock.
 * Null/incomplete services and unknown operations have no effects.
 * Queries are pure bool facts: three original error-string comparisons,
 * CANCELLED from the borrowed result and POPUP_ACTIVE from the existing popup.
 * SHOW_FAILURE(a=message:0 DRM,1 expired,2 unverified;b=offerSync) constructs the
 * original translated option strings and installs LOAD_RESULT. SHOW_RETRY
 * installs RETRY_RESULT. UPDATE/FINISH retain their original lifecycle effects.
 * START_WIFI creates the original child and installs WIFI_RESULT, returning
 * allocation success; LOG_OOM emits the original error on failure. CONNECTED
 * samples WiFi.status() and returns its equality with WL_CONNECTED; SYNC(a=ms)
 * calls syncNow and returns bool. PAINT_SYNC draws the original message.
 * DISCONNECT(a=bool), DELAY(a=ms), SAVE_PATH (copies current bookPath then saves),
 * RESTART and REOPEN retain original resource/IO/relaunch operations. CLEAR,
 * PAINT_FAILURE retain original display/lock ownership. BASE_RENDER is reserved
 * and never emitted. The native facade owns its RenderLock; it does not cross
 * the recovery ABI. This avoids keeping recovery dispatch frames on deep render.
 * No ModelLock, heap owner or new scheduling operation is added. Other command
 * arguments and return values are ignored. */

/* Bookmark and consumed-link composition, synchronous borrowed context.
 * Pure helpers preserve original f32 arithmetic (including NaN comparisons):
 * range returns chapter-fraction bounds; matching uses direct position equality
 * or clamped percentage within the page range plus/minus 0.0001f.
 * Native getPageProgressRange retains only the two EPUB calculateProgress calls.
 * LOAD/ADD/FLAG borrow owner. Only ADD supplies native temporary resources, in
 * its separate facade stack frame; FLAG/LOAD do not reserve Entry/string storage.
 * Rust guards balance LOCK, PROGRESS, TEXT and ENTRY scopes on all return paths.
 * Fields are pure scalar facts; REMOVED/BOOKMARKED writes use short ModelLock.
 * No resource callback runs inside ModelLock. Signed fields preserve i32 bits.
 *
 * LOCK/UNLOCK retain original RenderLock. LOG_TOGGLE preserves diagnostic args.
 * ESTIMATE reads Section::estimatedTotalPages. PROGRESS_BEGIN/END own the original
 * SavedProgressPosition; RANGE(a=page,b=count) retains the two converted bounds.
 * ERASE_MATCHES/ANY_MATCH(a=page,b=count) retain native STL vector mutation/scan
 * with the Rust matching predicate; they return removed/any-match booleans.
 * Native STL owns element move/destruction order, not matching decisions.
 * TEXT_BEGIN/END own original pageText string; TEXT_READ assigns section text.
 * ENTRY_BEGIN(a=page,b=count) constructs original BookmarkEntry and copies mapped
 * progress, sanitized text and position. ENTRY_OFFSET(a=u32) sets its offset and
 * presence flag; ENTRY_INSERT copies entry to vector front; ENTRY_END destroys it.
 * OFFSET_READ(a=page) retains Section optional offset, returns presence and
 * exposes value through LOADED_OFFSET. CLEAR/LOAD/SAVE are existing BookmarkFile
 * operations (SAVE returns bool); LOG_SAVE_FAILED and UPDATE retain their effects.
 * Other arguments and return values are ignored. No pointer escapes the call.
 *
 * Link queries read depth, position spine/page by index 0..2, and spine count.
 * PATH_BEGIN/END own the original load path; EXISTS/OpenRead/REMOVE use it.
 * FILE_BEGIN/END own HalFile including its implementation release; OPEN_READ and
 * OPEN_WRITE return success, latter keeps original temporary path construction.
 * TRANSFER read=0/write=1 borrows a buffer only for that call and returns byte
 * count; writes never modify bytes. Rust owns the bounded 13-byte wire buffer and
 * 3-position codec array. SET_DEPTH(a), SET_SPINE(a=index,b=value), SET_PAGE(a,b)
 * publish decoded POD values. LOG_WRITE_FAILED/LOG_LOADED(a=depth) are original
 * diagnostics. load removes the consumed path only after FileEnd, also on failed
 * open; PathEnd is last. save never creates persistent path storage.
 * Null/incomplete services and unknown operations have no effects. */

/* load=0 saves; load=1 loads; other values have no effects. */

/* Reader clipping traversal: borrowed page/owner; no heap owner or retained pointer.
 * HIT uses input x/y, samples original margins/font/settings and returns clipping
 * index or -1. DRAW uses font/top/left and returns -1. Other inputs are ignored.
 * Rust owns traversal, clipping matching, em-space/adjacent-word geometry, hit
 * bounds, LTR/RTL bridges and A4 grayscale parity. No page rasterizer changes.
 * Native WordFont remains a font-resolution leaf using existing TextBlock APIs;
 * Measure/EmWidth retain underline masking and existing font/text metrics.
 *
 * Queries are pure resource/POD facts. VISIBLE/EM_SPACE/SOURCE_START/SOURCE_END
 * and WORD_X use index=word; other indices are ignored. VISIBLE and EM_SPACE use
 * existing clippingText byte-scan helpers. BW returns renderer mode==BW. SPINE
 * copies existing POD under ModelLock; resource queries run outside it.
 * MARGINS fills original oriented TRBL; READER_FONT and SIGNATURE call settings
 * leaves, returning original values. ELEMENT_BEGIN(a=index) owns the PageElement
 * view and returns whether it is a line; ELEMENT_END releases it. BLOCK_BEGIN
 * owns its TextBlock view and returns valid; BLOCK_END releases it. Rust guards
 * release block before element on continue, hit and normal completion.
 * WORD_FONT(a=word,b=base) resolves existing word font. MEASURE(a=word,b=font)
 * measures existing text with character spacing; EM_WIDTH(a=word,b=font)
 * measures the em-space using the same masked style and no character spacing.
 * LINE_FONT(a=base) resolves block font; RUBY(a=lineFont), Y_OFFSET(a=word,b=base)
 * and LINE_HEIGHT(a=font) retain original metrics. PIXEL(a=x,b=y) draws false.
 * CLOCK_START retains original millis for lazy diagnostic timing; LOG(a=clips,
 * b=highlightedWords) evaluates original end-clock only when logging is enabled.
 * Paint borrows one rect synchronously and fills original LightGray dither;
 * Rust invokes it only for the original non-A4-gray branch. Clipping copies one
 * original record projection by index into the non-null output; Rust owns the
 * matching predicate, leaving the shared store/other activities unchanged.
 * Other command arguments/returns are ignored; signed values preserve i32 bits.
 * No callback is invoked under ModelLock. Null/incomplete services, null input
 * and unknown operations return -1 without effects. */

/* Child selection/sync parent policy. Four-pointer synchronous borrowed service.
 * Operations: DICTIONARY; SELECTION(a=x,b=y,c=ClipSelection mode); FOOTNOTE(a=reopen);
 * SYNC; CLIPPINGS; and retained RESULT operations. LOOKUP_RESULT repeats DICTIONARY
 * only on non-cancel. Other a/b/c values are ignored. Return is original SYNC bool.
 * Result fields borrow the original ActivityResult only during this call. Retained
 * native handlers keep owner plus original captured scalars/strings; they never
 * keep this service, a Rust context, a result pointer, or a temporary resource.
 * Read fields are pure facts. Writes to owner POD use a short ModelLock and never
 * invoke any resource command under it. Signed fields preserve i32 bits. Message
 * IDs are semantic: 0 removed,1 delete-failed,2 limit,3 saved,4 export-failed,5 failed.
 * ResultAction is Clip=0,Lookup=1,Bookmark=2; mode is Clip=0,Lookup=1,DefinitionLookup=2.
 * Resource acquisition/consumption is a command, not a query. Rust decides every
 * branch, validation, loop, result action and destruction boundary. Native commands
 * bind existing operations and report their outcomes; they do not redo policy.
 * LOCK/UNLOCK bracket original RenderLock. RELEASE_STRIPS releases idle strips.
 * PAGES_BEGIN/END own the original vector; RESERVE_PAGES(a=count), PAGE_LOAD(a=page),
 * PAGE_END, APPEND_PAGE retain original capacity/move/lifetime. PAGE_LOAD returns
 * presence. HIT(a=x,b=y) calls clippingAtPoint on that page. REMOVE_CLIPPING(a=index)
 * returns success; LOG_REMOVE_FAILED(a=index) retains its diagnostic.
 * SIGNATURE computes layout signature. MARGINS stores oriented margins; native
 * constructors add the same screen margin. TOC_INDEX calls the EPUB resolver.
 * CHAPTER_BEGIN(a=toc), TITLE_BEGIN, AUTHOR_BEGIN construct the original strings;
 * their END commands destroy them. NEW_SELECTION(a=x,b=y) moves pages into the
 * original activity and returns allocation success; LAUNCH_SELECTION transfers
 * the activity and captures original metadata. DROP_ACTIVITY releases ownership.
 * READ_VISIBLE_OFFSET(a=page) publishes the Section offset in its existing order.
 * LOOKUP_PAGE loads current section page or null. NEW_LOOKUP and NEW_FOOTNOTE
 * consume the owned page, returning allocation success; corresponding LAUNCH
 * commands transfer it and retain the original handler. LAUNCH_FOOTNOTE(a=reopen).
 * PARAGRAPH_INDEX(a=startPage) returns the Section optional or UINT16_MAX.
 * ADD_CLIPPING(a=startPage,b=endPage) passes original captured metadata/result;
 * its result is Added=0,LimitReached=1,Failed=2. EXPORT_CLIPPING(a=startPage+1)
 * retains the original export and returns bool. Rust selects the notice message.
 * HREF_BEGIN/END own the original href; COPY_FIRST_HREF, NAVIGATE_HREF and
 * NAVIGATE_RESULT_HREF retain original owned/borrowed string lifetime, with true
 * save-back-stack. Footnote metadata is sampled only inside the RenderLock scope.
 * NEW_CLIPPINGS/LAUNCH_CLIPPINGS retain original allocation/transfer; result
 * variant probing is a pure field and APPLY_RESULT_PROGRESS applies its borrow.
 * FLUSH_SESSION precedes credentials. LAUNCH_EVENTS_ONLY retains original native
 * argument construction/evaluation and returns no policy. ESTIMATE reads Section.
 * POSITION_BEGIN/END own the original CrossPointPosition; SAVED_POSITION_BEGIN/END
 * own the original SavedProgressPosition. PATH_BEGIN/END own saved EPUB path.
 * SAVE_PROGRESS(a=page,b=count) uses current spine. LOG_SYNC_* retain diagnostics
 * and heap sampling. DISCARD_OVERLAY/RELEASE_IMAGES/CLEAR_EXTRACTOR/DROP_SECTION,
 * CACHE_BEGIN/RELEASE_CACHE/CACHE_END preserve ordered release of existing owners.
 * LOAN_BEGIN/END own FrameBufferLoan; MAP_PROGRESS assigns SavedProgressPosition.
 * SAVE_LINKS and DROP_BOOK follow original order; LAUNCH_SYNC transfers original
 * position/path/chapter arguments into replaceActivity while RenderLock is held.
 * All *_END/DROP commands are balanced on success and early return, in original
 * reverse destruction order, including failed construction/acquisition cases.
 * LOG_*, UPDATE, ADD_BOOKMARK, OPEN_MENU and DICTIONARY bind their named existing
 * methods/effects. Unused args/results are ignored. Null/incomplete services and
 * unknown operation/mode values have no effects. No new persistent/heap owner. */

/* Final reader projections, remote turns and end-book policy. Synchronous borrowed
 * four-pointer services; no pointers or callback tables escape the call.
 * CHAPTER writes OutputPage/OutputTotal. BOOK_PERCENT(a=page,b=total) returns 0..100.
 * TOC returns nonnegative index. SCREENSHOT writes native ScreenshotInfo fields;
 * caller initializes readerType=Epub, and TITLE copies the original title with
 * the original snprintf and temporary-string lifetime. BASIS_POINTS returns 0..10000
 * or UINT32_MAX to request the original getProgressPercent()*100 fallback after
 * Rust unwinds (avoids nesting projection frames on the render stack).
 * POSITION writes the existing native CrossPointPosition result in place;
 * INIT_POSITION(a=page,b=total) constructs it with current spine and both resolved
 * flags. PARAGRAPH(a=u16 page) returns bit16=present plus low16=value; OFFSET(a=u16
 * page) copies an optional offset into the result and returns presence. Rust sets
 * the presence flags and paragraph value after the original query/construct order.
 * CONFIRM_THRESHOLD returns milliseconds; long-press setting IDs preserve native
 * 0=sync,1=disabled,2=bookmark,3=dictionary,4=menu,5=clip,6=footnotes. Touch returns 0.
 * REMOTE(a=readerMenu), AT_END and INPUT_OVER return bool. RETURN_END/RENDERED_END
 * mutate the original scalar state and invoke only the original popup effect.
 * TURN(a=forward), SKIP(a=amount), APPLY_MOVE(a=kind,b=value,c=last_page bit0 |
 * record_turn bit1) reuse the existing Rust movement arithmetic. Result is bool.
 * Core read/write callbacks are pure scalar/POD accesses. Owner scalar writes use
 * a short ModelLock; borrowed output writes do not acquire it. No command runs
 * under ModelLock. Read-only operations do not mutate the activity.
 * BOOK_SIZE/SPINE_COUNT/ESTIMATE call existing book/section accessors. TOC_CURRENT
 * uses the section current page; TOC_CACHED constructs/destroys the original stack
 * Section around its saved next-page lookup. PROGRESS(a=spine,b=f32 fraction bits)
 * calls calculateProgress and returns f32 bits. Floating point order is preserved:
 * BOOK_PERCENT clamps fraction before scaling; screenshot/basis clamp after cast.
 * CREDENTIALS/TOOLBAR_MENU/BASE_INPUT call the original providers, lazily. TITLE,
 * PARAGRAPH, OFFSET, INIT_POSITION and all read commands ignore unused arguments.
 * START_SELECTION/OPEN_TOOLBAR/OPEN_MENU/SAVE_ERROR_POPUP bind original effects.
 * LOCK/UNLOCK bracket the original RenderLock; CLEAR_DEFERRED/ACKNOWLEDGE/DROP_SECTION
 * preserve original ordering. CLOCK returns monotonic milliseconds. Chapter turns
 * record time after section destruction and before unlock. No heap owner or lock
 * scope is added; all resource effects stay outside ModelLock. Unknown operation
 * or null/incomplete services have no effects and return zero. Signed values use
 * i32 bit representation; output pointers remain native-only in the context. */

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

#endif

#ifdef __cplusplus
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

#undef XP_UI_READER_LOOP_ASSERT
#undef XP_UI_READER_LOOP_ALIGNOF

#ifdef __cplusplus
#define XP_UI_READER_OVERLAY_ASSERT(...) static_assert((__VA_ARGS__))
#define XP_UI_READER_OVERLAY_ALIGNOF(T) alignof(T)
#else
#define XP_UI_READER_OVERLAY_ASSERT(...) _Static_assert((__VA_ARGS__), #__VA_ARGS__)
#define XP_UI_READER_OVERLAY_ALIGNOF(T) _Alignof(T)
#endif

XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_TEXT_ROWS == 5);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_HOLD_MS == 1500);
XP_UI_READER_OVERLAY_ASSERT(XP_UI_READER_OVERLAY_HOLD_ROWS == 10);

#undef XP_UI_READER_OVERLAY_ASSERT
#undef XP_UI_READER_OVERLAY_ALIGNOF

#if defined(__cplusplus)
#define XP_UI_READER_IDLE_ASSERT(x) static_assert((x), #x)
#else
#define XP_UI_READER_IDLE_ASSERT(x) _Static_assert((x), #x)
#endif

XP_UI_READER_IDLE_ASSERT(XP_UI_READER_IDLE_ADMITTED == 1);
XP_UI_READER_IDLE_ASSERT(XP_UI_READER_IDLE_SCREEN_LONG == 2);
#undef XP_UI_READER_IDLE_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_VIEW_ASSERT(x) static_assert(x, #x)

#else
#define XP_UI_READER_VIEW_ASSERT(x) _Static_assert(x, #x)

#endif

#undef XP_UI_READER_VIEW_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_NAV_ASSERT(x) static_assert(x, #x)

#else
#define XP_UI_READER_NAV_ASSERT(x) _Static_assert(x, #x)

#endif

#undef XP_UI_READER_NAV_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_BOOK_ASSERT(x) static_assert(x, #x)

#else
#define XP_UI_READER_BOOK_ASSERT(x) _Static_assert(x, #x)

#endif

#undef XP_UI_READER_BOOK_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_VALUE_ASSERT(x) static_assert(x, #x)

#else
#define XP_UI_READER_VALUE_ASSERT(x) _Static_assert(x, #x)

#endif

#undef XP_UI_READER_VALUE_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_LIFETIME_ASSERT(x) static_assert(x, #x)

#else
#define XP_UI_READER_LIFETIME_ASSERT(x) _Static_assert(x, #x)

#endif

#undef XP_UI_READER_LIFETIME_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_BUILD_ASSERT(x) static_assert(x, #x)

#else
#define XP_UI_READER_BUILD_ASSERT(x) _Static_assert(x, #x)

#endif

#undef XP_UI_READER_BUILD_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_SETTINGS_ASSERT(x) static_assert(x, #x)

#else
#define XP_UI_READER_SETTINGS_ASSERT(x) _Static_assert(x, #x)

#endif

#undef XP_UI_READER_SETTINGS_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_PARENT_ASSERT(x) static_assert(x, #x)

#else
#define XP_UI_READER_PARENT_ASSERT(x) _Static_assert(x, #x)

#endif

#undef XP_UI_READER_PARENT_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_RECOVERY_ASSERT(x) static_assert(x, #x)

#else
#define XP_UI_READER_RECOVERY_ASSERT(x) _Static_assert(x, #x)

#endif

#undef XP_UI_READER_RECOVERY_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_BOOKMARK_ASSERT(x) static_assert(x, #x)
#else
#define XP_UI_READER_BOOKMARK_ASSERT(x) _Static_assert(x, #x)
#endif

#undef XP_UI_READER_BOOKMARK_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_CLIP_ASSERT(x) static_assert(x, #x)
#define XP_UI_READER_CLIP_ALIGN(t) alignof(t)
#else
#define XP_UI_READER_CLIP_ASSERT(x) _Static_assert(x, #x)
#define XP_UI_READER_CLIP_ALIGN(t) _Alignof(t)
#endif

#undef XP_UI_READER_CLIP_ASSERT
#undef XP_UI_READER_CLIP_ALIGN

#if defined(__cplusplus)
#define XP_UI_READER_CHILD_ASSERT(x) static_assert(x, #x)
#else
#define XP_UI_READER_CHILD_ASSERT(x) _Static_assert(x, #x)
#endif

#undef XP_UI_READER_CHILD_ASSERT

#if defined(__cplusplus)
#define XP_UI_READER_PROJECTION_ASSERT(x) static_assert(x, #x)
#else
#define XP_UI_READER_PROJECTION_ASSERT(x) _Static_assert(x, #x)
#endif

#undef XP_UI_READER_PROJECTION_ASSERT
