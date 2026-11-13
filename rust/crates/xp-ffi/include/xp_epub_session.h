#pragma once
#include "xp_app.h"
#include "xp_book.h"
#include "xp_clippings.h"
#include "xp_display.h"
#include "xp_ffi.h"
#include "xp_font_registry.h"
#include "xp_hal.h"
#include "xp_plugin.h"
#include "xp_settings.h"
#include "xp_ui_dictionary.h"
#include "xp_ui_reader.h"
#include "xp_ui_reader_chrome.h"
#include "xp_ui_utility.h"

#ifdef __cplusplus
extern "C" {
#endif

// Text views are synchronous; no callback drains navigation.
// Resource handles must close before their backing owner.

typedef struct {
  int32_t spine;
  int32_t page;
  int32_t count;
  uint32_t visible_offset;
  uint16_t paragraph;
  uint16_t list_item;
  uint8_t has_visible_offset;
  uint8_t has_paragraph;
  uint8_t has_list_item;
  uint8_t resolved_spine;
  uint8_t mapped_page;
  uint8_t anchor[64];
} XpEpubPosition;

typedef struct {
  XpAppText xpath;
  float percentage;
} XpEpubSavedProgress;

typedef struct {
  XpAppText href;
  uint32_t bytes;
  int16_t toc_index;
} XpEpubSpine;

typedef struct {
  XpAppText title;
  XpAppText anchor;
  int32_t spine;
  int32_t level;
  XpAppText href;
} XpEpubToc;

typedef struct {
  int32_t font;
  float compression;
  uint16_t width;
  uint16_t height;
  uint8_t paragraph_spacing;
  uint8_t paragraph_indent;
  int8_t character_spacing;
  uint8_t word_spacing_percent;
  uint8_t alignment;
  uint8_t hyphenation;
  uint8_t embedded_style;
  uint8_t images;
  uint8_t focus_reading;
} XpEpubRenderSpec;

typedef struct {
  int32_t font;
  uint8_t orientation;
  uint8_t font_size;
  uint8_t line_spacing;
  uint8_t alignment;
  uint8_t focus_reading;
  uint8_t screen_margin;
  uint8_t text_aa;
  uint8_t embedded_style;
  uint8_t remove_read_recents;
  uint8_t move_finished;
  uint8_t menu_style;
  uint8_t long_menu;
  uint8_t long_button;
  uint8_t short_power;
  uint8_t tilt;
  uint8_t touch_controls;
  uint8_t page_gesture;
  uint8_t previous_gesture;
  uint8_t show_menu;
  uint8_t back_to_browser;
  uint8_t screen_inverted;
  uint8_t ble_enabled;
  uint8_t dictionary_available;
  uint8_t refresh_frequency;
  XpAppText font_family;
} XpEpubSettings;

typedef struct {
  uint32_t free_bytes;
  uint32_t largest_block;
} XpEpubHeap;

typedef struct {
  uint8_t compiled_in;
  uint8_t running;
  uint8_t starting;
  uint8_t stopping;
  uint8_t note;
} XpEpubRadio;

typedef struct {
  uint8_t name[64];
  uint8_t addr[18];
} XpEpubPeer;

typedef struct {
  int32_t width;
  int32_t height;
  int32_t status_height;
  int32_t progress_height;
  int32_t status_margin;
  int32_t ui_font_12;
  uint8_t title_mode;
  uint8_t has_framebuffer;
  uint8_t async_refresh;
  uint8_t xteink_panel;
  uint8_t a4;
#ifdef XP_APP_TEST
  uint8_t has_ttf;
#endif
  uint8_t strip_enabled;
  uint8_t has_font_cache;
} XpEpubGeometry;

typedef struct {
  uint32_t held_ms;
  uint32_t raw_held;
  uint8_t raw_edges;
  uint8_t remote_click;
  uint8_t touch;        // Runtime mapped-input availability.
  uint8_t board_touch;  // Board capability used by the confirm threshold.
  uint8_t home_action;
  uint8_t slide_owns_light;
} XpEpubInputFacts;

typedef struct {
  int32_t left;
  int32_t top;
  int32_t x;
  int32_t y;
  uint8_t mode;
} XpEpubSelectionArgs;

typedef struct {
  uint8_t active;
  uint8_t changed;
  uint8_t consumed;
} XpEpubPopupInput;

// Synchronous native facts for Rust composition; no font or paint callbacks.
typedef struct {
  int32_t screen_width, screen_height, margin_left, margin_right, margin_bottom;
  int32_t status_height, progress_height, status_margin, horizontal_margin, corner, offset_y;
  int32_t battery_width, battery_height, font, estimate_font;
  const uint8_t* bookmark;
  uint16_t battery_percent;
  uint8_t show_count, show_percent, title_mode, show_battery, show_battery_percent;
  uint8_t clock_mode, bar_mode, bar_height, clock_available, clock_valid, charging, battery_style;
  uint8_t clock[9];
} XpEpubStatusFacts;

#ifdef XP_APP_TEST
// Fixture-only native painter input (HOST-RETIRE).
typedef struct {
  XpAppText title;
  float book_progress;  // Negative means the whole-book size query failed.
  int32_t page;
  int32_t pages;
  int32_t y_offset;
  uint8_t bookmarked;
  uint8_t building;
} XpEpubStatus;
#endif

typedef struct {
  XpAppText xpath;
  XpAppText summary;
  XpAppText name;
  float percentage;
  uint16_t spine;
  uint16_t page_count;
  uint16_t chapter_page;
  uint8_t has_offset;
  uint32_t offset;
} XpEpubBookmark;

typedef struct {
  int64_t start_time;
  int64_t end_time;
  uint32_t duration_seconds;
  uint16_t start_progress;
  uint16_t end_progress;
} XpEpubSessionEvent;

// Immutable decoded bytes; all spans expire when the Rust owner is released.
typedef struct {
  const uint8_t* arena;
  size_t size;
  uint32_t visible_offset;
} XpEpubPageView;

// A factory borrows this record until success. Success clears owner after adoption;
// failure leaves it unchanged. The native facade invokes destroy exactly once.
typedef struct {
  XpEpubPageView view;
  void* owner;
  void (*destroy)(void* owner);
} XpEpubOwnedPage;

typedef uint8_t (*XpEpubSpineSink)(void* context, const XpEpubSpine* value);
typedef uint8_t (*XpEpubTocSink)(void* context, const XpEpubToc* value);
typedef uint8_t (*XpEpubSavedProgressSink)(void* context, const XpEpubSavedProgress* value);
typedef uint8_t (*XpEpubBookmarkSink)(void* context, const XpEpubBookmark* value);
typedef void (*XpEpubBuildPopup)(void* context);

// The reader owns all data behind this view until every native loan is released.
// Text is bytes, not UTF-8. Only cache_path and language need native string copies.
typedef struct {
  XpAppText cache_path, language;
  int32_t spine_count, toc_count;
} XpEpubBookView;

// Operations borrow the Rust book synchronously. A sink may call into a parser;
// no mutable Rust book borrow may remain live across that call.
// Row/window transactions take the cache lock; release it before any sink.
typedef struct {
  uint8_t (*spine)(void* book, int32_t index, XpEpubSpineSink sink, void* context);
  uint8_t (*toc)(void* book, int32_t index, XpEpubTocSink sink, void* context);
  uint32_t (*cumulative)(void* book, int32_t index);
  int32_t (*spine_for_size)(void* book, uint32_t size);
  uint8_t (*stream)(void* book, XpAppText href, uint32_t chunk, uint8_t early, XpBookSink sink, void* context);
  uint8_t (*item_size)(void* book, XpAppText href, uint32_t* size);
} XpEpubBookBorrow;

// Typed scan loan for host oracle fixtures only.
#ifdef XP_APP_TEST
typedef struct {
  const void* owner;
  uint8_t active;
} XpEpubFontScan;
#endif

typedef enum {
  XP_EPUB_RENDER_IS_BW,
  XP_EPUB_RENDER_IS_X3,
  XP_EPUB_RENDER_IS_UC8279,
  XP_EPUB_RENDER_ABSOLUTE_SUPPORTED,
  XP_EPUB_RENDER_OVERLAY_CAPS,
  XP_EPUB_RENDER_ABSOLUTE_CAPS,
  XP_EPUB_RENDER_STRIP_BOARD,
  XP_EPUB_RENDER_WIDTH_BYTES,
  XP_EPUB_RENDER_HEIGHT,
  XP_EPUB_RENDER_FREE_HEAP,
  XP_EPUB_RENDER_MAX_BLOCK,
  XP_EPUB_RENDER_MODE,
  XP_EPUB_RENDER_INVERTED,
  XP_EPUB_RENDER_WIDTH
} XpEpubPhysicalQuery;

typedef enum {
  XP_EPUB_RENDER_CLEAR,
  XP_EPUB_RENDER_DISPLAY,
  XP_EPUB_RENDER_DISPLAY_ASYNC,
  XP_EPUB_RENDER_GRAY_BASE,
  XP_EPUB_RENDER_ABSOLUTE_BASE,
  XP_EPUB_RENDER_PRECONDITION,
  XP_EPUB_RENDER_MODE_SET,
  XP_EPUB_RENDER_STRIP_BEGIN,
  XP_EPUB_RENDER_STRIP_END,
  XP_EPUB_RENDER_WAIT,
  XP_EPUB_RENDER_WRITE_PLANE,
  XP_EPUB_RENDER_GRAY_DISPLAY,
  XP_EPUB_RENDER_CLEANUP,
  XP_EPUB_RENDER_STORE_BW,
  XP_EPUB_RENDER_RESTORE_BW,
  XP_EPUB_RENDER_COPY_LSB,
  XP_EPUB_RENDER_COPY_MSB,
  XP_EPUB_RENDER_COPY_STATUS
} XpEpubPhysicalOperation;

// No owner, allocation, Page, timestamp or logging commands cross this boundary.
// buffer borrows a full strip/plane for the synchronous operation; mode selects
// refresh, render plane or polarity. y/rows/value retain their physical units.
typedef struct {
  uint8_t kind, mode;
  uint16_t y, rows, reserved;
  uint32_t value;
  uint8_t* buffer;
} XpEpubPhysicalOp;

typedef XpAppText (*XpEpubChildRowText)(const void* context, uint32_t index);

// Stable caller storage for retained native resources; construct after StableOwner adoption, before publication.
// Native implementations assert size/alignment and never delete this storage.
typedef struct {
  uintptr_t words[90];
} XpEpubLookupStorage;
typedef struct {
  uintptr_t words[43];
} XpDefinitionPayloadStorage;
typedef struct XpDefinitionNativeTransfer XpDefinitionNativeTransfer;
typedef struct {
  int32_t pages;
  uint8_t styled;
  uint8_t reserved[3];
} XpDefinitionPayloadView;
typedef struct {
  uint16_t start_page, end_page, start_word, end_word;
  uint8_t valid;
  uint8_t reserved[3];
} XpDefinitionSelectionPosition;
typedef struct {
  int32_t page;
  XpDefinitionSelectionPosition selection;
  int32_t x, y;
} XpDefinitionSelectionRequest;
// Fill the final native string synchronously. The callback and context never escape.
typedef uint8_t (*XpEpubDefinitionFill)(void* context, uint8_t* text, uint32_t length);

typedef struct {
  int32_t year, month, day, weekday, hour, minute;
} XpEpubCalendar;

#ifdef XP_APP_TEST
typedef int32_t (*XpEpubHyphenLanguage)(XpAppText language);
#else
typedef void (*XpEpubHyphenLanguage)(int32_t language);
#endif

typedef struct XpEpubLeaves {
  void (*render_margins)(int32_t* margins);
  void (*render_geometry)(XpEpubGeometry* out);
  void (*render_restore_bw)(uint8_t resync);
  void (*render_discard_bw)(void);
  void (*render_orientation)(uint8_t orientation);
  uint32_t (*loan_count)(void);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  // Null without error is a plain book. Error text is borrowed only during the sink.
  void* (*protection_open)(XpAppText path, XpAppTextSink error, void* context);
#else
  // ReaderProtection.h stages: inline probe file or SDK index; Scan returns its status code.
  uint8_t (*protection_index)(void* storage, uint8_t stage, XpAppText path);
#endif
  void (*protection_close)(void* protection);
  uint8_t (*protection_encrypted)(void* protection, XpAppText path);
  uint32_t (*protection_size)(void* protection, XpAppText path);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  uint8_t (*protection_stream)(void* protection, XpAppText path, XpBookSink sink, void* context);
#else
  // Prepare/Read from ReaderProtection.h; zero means failure, nonzero means success.
  uint8_t (*protection_stream)(void* protection, void* storage, uint8_t stage, XpAppText path, XpBookSink sink,
                               void* context);
#endif
  uint8_t (*file_seek)(void* file, uint32_t offset);
  uint32_t (*file_size)(void* file);
  uint8_t (*path_remove_tree)(XpAppText path);
  void (*storage_read_release)(void* storage);
  void (*paint_dither)(int32_t x, int32_t y, int32_t width, int32_t height);
  void (*paint_pixel)(int32_t x, int32_t y);
#ifdef XP_APP_TEST
  // Fixture-only native status painter (HOST-RETIRE).
  void (*paint_status)(const XpEpubStatus* status);
#else
  // Fills caller-owned facts; retained HAL continuations provide battery/RTC data.
  void (*status_facts)(XpEpubStatusFacts* out);
#endif
  // Catalog builds request native effects; no font policy runs in this callback.
  void (*fonts_ensure_loaded)(
#if defined(XP_SD_FONT_CATALOG) && !defined(XP_APP_TEST)
      uint32_t action, XpSdSettings*, XpSdContinuation*
#else
      void
#endif
  );
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  void (*fonts_release_caches)(void);
  uint8_t (*fonts_prewarm_begin)(XpEpubFontScan* storage);
  void (*fonts_prewarm_finish_scan)(XpEpubFontScan* storage);
  void (*fonts_prewarm_drop)(XpEpubFontScan* storage);
#endif
  uint8_t (*fonts_take_corrupt_notice)(void);
#ifndef XP_SD_FONT_CATALOG
  uint8_t (*font_sizes)(XpAppText family, XpAppTextSink sink, void* context);
#endif
  // Catalog firmware leaves font zero; Rust resolves it after this raw settings read.
  // Host/non-catalog builds resolve font only when resolve_font is nonzero.
  void (*settings_snapshot)(uint8_t resolve_font, XpEpubSettings* out);
  // Shipping copies raw fields; Rust fills font/compression from settings_snapshot.
  void (*settings_render_spec)(uint16_t width, uint16_t height, XpEpubRenderSpec* out);
  void (*settings_orientation)(uint8_t value);
  void (*settings_font_size)(uint8_t value);
  void (*settings_line_spacing)(uint8_t value);
  void (*settings_alignment)(uint8_t value);
  void (*settings_focus_reading)(uint8_t value);
  void (*settings_inversion)(uint8_t value);
  void (*settings_ble_enabled)(uint8_t value);
  void (*settings_save)(void);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  const XpUiReaderInputServices* (*input_services)(void);
#else
  // Callback context must be XpEpubInputQuery, initialized from the returned table's context (MappedInputManager*).
  const XpUiReaderInputServices* (*input_sources)(void);
#endif
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  void (*input_facts)(XpEpubInputFacts* out);
#else
  void (*input_facts)(uint8_t home, XpEpubInputFacts* out);
#endif
  uint8_t (*input_pressed)(uint32_t button);
  uint8_t (*input_screen_point)(uint8_t kind, int32_t* x, int32_t* y);
  uint8_t (*input_screen_tap)(int32_t* x, int32_t* y);
  uint8_t (*input_raw_contact)(void);
  uint8_t (*render_busy)(void);
  void* (*file_open_read)(XpAppText path, XpAppText origin);
  void* (*file_open_write)(XpAppText path, XpAppText origin);
  int32_t (*file_read)(void* file, uint8_t* bytes, uint32_t length);
  uint32_t (*file_write)(void* file, const uint8_t* bytes, uint32_t length);
  uint8_t (*file_close)(void* file);
  uint8_t (*path_remove)(XpAppText path);
  uint8_t (*path_rename)(XpAppText from, XpAppText to);
  uint8_t (*path_mkdir)(XpAppText path);
  uint64_t (*native_path_hash)(XpAppText path);
  void (*heap_snapshot)(XpEpubHeap* out);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  void (*radio_status)(XpEpubRadio* out, XpEpubPeer* peer_or_null);
  uint8_t (*radio_before_build)(void);
  void (*radio_settle_start)(void);
  uint8_t (*radio_holds_heap)(void);
  void (*radio_after_paint)(void);
  void (*radio_switch_off)(void);
  void (*radio_request_connect)(void);
#else
  void (*radio_peer)(XpEpubPeer* out);  // One-way: live linked name and address only.
#endif
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  int64_t (*trusted_now)(void);
  uint8_t (*trusted_sync)(uint32_t timeout_ms);
#else
  const XpHalTimeServices* (*time_services)(void);
  XpHalSyncServices (*time_start_sync)(void);
#endif
  uint8_t (*wifi_connected)(void);
  void (*wifi_disconnect)(uint8_t off);
  void (*restart_reader)(void);
  // Deferred child factories return an owned native legacy wrapper for App adoption.
  // Non-null consumes each Rust Page owner; null leaves every owner with the caller.
  void* (*child_text_settings)(void);
  void* (*child_selection)(XpEpubOwnedPage* pages, uint8_t count, const XpEpubSelectionArgs* args);
  void* (*child_wifi)(void);
  void* (*child_sync)(XpAppText path, const XpEpubPosition* position, const XpEpubSavedProgress* saved,
                      XpAppText chapter, uint8_t events_only);
  void* (*popup_open)(void);
  void (*popup_close)(void* popup);
  uint8_t (*popup_active)(void* popup);
  // Labels are copied synchronously; only choose and choice_context are retained.
  // Choice runs before haptic/update. The retained context must outlive the popup callback.
  void (*popup_show)(void* popup, XpAppText title, XpAppText message, uint32_t count, int32_t selected,
                     uint8_t (*label)(void* label_context, uint32_t index, XpAppTextSink sink, void* sink_context),
                     void* label_context, void (*choose)(void* choice_context, int32_t index), void* choice_context);
  void (*popup_dismiss)(void* popup);
  // Choice has already run synchronously. Handle changed last after this call returns.
  void (*popup_input)(void* popup, XpEpubPopupInput* out);
  void (*popup_paint)(void* popup);
  void (*popup_process_render)(void* popup);
  void* (*end_open)(void);
  void (*end_close)(void* end);
  void (*end_load)(void* end, XpAppText path);
  uint8_t (*end_menu_active)(void* end);
  void (*end_input)(void* end, void (*result)(void* context, uint8_t action, XpAppText selected_path), void* context);
  void (*end_paint)(void* end);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  uint8_t (*recents_remove)(XpAppText path);
  void (*recents_add)(const XpAppBook* book);
  void (*recents_update_path)(XpAppText from, XpAppText to, XpAppText old_cache, XpAppText new_cache);
#else
  void (*store_rows)(void* storage, XpStoreRows* out);
  void (*store_rows_close)(void* storage);
  void (*store_move_row)(size_t destination, size_t source);
#endif
  XpAppText (*state_open_path)(void);
  void (*state_set_open_path)(XpAppText path);
  void (*state_reset_reader_count)(void);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  void (*state_save)(void);
#else
  void (*store_document)(uint8_t operation, void* storage, XpStoreJsonOps* json, XpStorePersistOps* file,
                         XpStoreState* state);
#endif
  uint8_t (*has_sync_credentials)(void);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  uint8_t (*has_exit_subscriber)(void);
  uint8_t (*has_session_subscriber)(void);
#else
  // Borrowed until the synchronous query returns; subscription refresh stays on the app task.
  const XpPluginSubscriber* (*subscribers)(void);
#endif
// Fixture-only field/signature (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  uint8_t (*document_id)(XpAppText path, XpAppTextSink sink, void* context);
#else
  // Caller-owned HAL file and SDK MD5 context; synchronous, no retained pointer or callback.
  int64_t (*document_io)(void* storage, uint8_t operation, const uint8_t* input, uint8_t* output, uint32_t value);
#endif
  void (*emit_open)(XpAppText path);
  // percent is 0-100; on read failure use last known percent for this book session, initially 0.
  void (*emit_exit)(XpAppText path, int32_t percent);
  void (*emit_session)(XpAppText path, XpAppText document, const XpEpubSessionEvent* event);
  void (*log)(uint8_t severity, XpAppText origin, XpAppText message);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  int32_t (*font_text_advance)(XpAppText text, int32_t font, uint8_t style, int8_t character_spacing);
#endif
  uint8_t (*log_enabled)(uint8_t severity);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  void (*input_snapshot)(XpUiInput* out);
#else
  void (*input_snapshot)(uint8_t home, XpUiInput* out);
#endif
  // Lock order: ResourceAccess, cache, Storage.
  void (*cache_lock)(void);
  void (*cache_unlock)(void);
  uint8_t* (*scratch_claim)(uint32_t min_len);
  void (*scratch_release)(const uint8_t* bytes);
  void (*inflate_oom_log)(XpAppText allocation, uint32_t bytes);
  // Physical layout tables have process lifetime and never borrow a chapter owner.
  const XpParaMetrics* (*build_metrics)(void);
  // Firmware supplies allocation/free/logging only; Rust owns the reclaim callback.
  const XpParaAllocator* (*build_allocator)(void);
#if defined(XP_APP_TEST) || !defined(XP_SD_FONT_CATALOG)
  int32_t (*font_variant)(int32_t font, float scale);
#endif
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  int32_t (*font_line_height)(int32_t font, float compression);
#endif
#if defined(XP_APP_TEST) || !defined(XP_SD_FONT_CATALOG)
  int32_t (*font_ready)(int32_t font, int32_t fallback);
#endif
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  uint8_t (*font_codepoint)(int32_t font, uint32_t codepoint, uint8_t style, int32_t* advance, int32_t* top);
#endif
  // Rust-selected hyphenation ID for the retained native chapter/paragraph caches.
  XpEpubHyphenLanguage hyphen_language;
  uint32_t (*file_position)(void* file);
  void (*file_flush)(void* file);
  // Lend returns whether this scope owns restoration; nested scopes return zero.
  uint8_t (*framebuffer_lend)(void);
  void (*framebuffer_restore)(uint8_t owned);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  int32_t (*font_ascent)(int32_t font);
#endif
  uint32_t (*render_physical_query)(uint8_t query);
  uint32_t (*render_physical_operation)(const XpEpubPhysicalOp* operation);
  // Window/source pointers remain borrowed only until explicit detach.
  void (*image_read_window)(uint8_t* bytes, uint32_t length);
  uint8_t (*display_source)(XpDisplaySource* source);
  uint8_t* (*framebuffer_view)(uint32_t* bytes);
  // Synchronous drawing leaves; inputs borrow storage held under the resource guard.
  void (*child_list_theme)(XpUiReaderTheme* out, XpUiReaderThemeFacts* facts, uint8_t toolbar);
  uint8_t (*child_utility_layout)(XpUiUtilityLayout* out);
  void (*child_header)(XpUiRect rect, XpAppText title, uint8_t list);
  void (*child_utility_footer)(XpUiRect clip, const XpAppText* labels);
  void (*child_qr_draw)(XpUiRect bounds, XpAppText payload);
  void (*child_toolbar_icon)(uint32_t icon, XpUiRect rect, XpUiRect clip);
  // Fixture-only fields/signatures (HOST-RETIRE); absent from firmware.
#ifdef XP_APP_TEST
  void (*child_clear_fonts)(void);
  void (*child_prewarm_rows)(int32_t font, const void* context, XpEpubChildRowText text, uint32_t count);
#endif
  // Dictionary operations preserve native engine results; Rust owns ordering and popup policy.
  void (*child_lookup_init)(XpEpubLookupStorage* storage, XpAppText query);
  void (*child_lookup_drop)(XpEpubLookupStorage* storage);
  uint8_t (*child_lookup_open)(XpEpubLookupStorage* storage, uint8_t* needs_index);
  uint8_t (*child_lookup_index)(XpEpubLookupStorage* storage, uint8_t* result);
  uint8_t (*child_lookup_find)(XpEpubLookupStorage* storage, uint8_t* result);
  void (*child_lookup_discard)(XpEpubLookupStorage* storage);
  uint8_t (*child_definition_payload_lookup)(XpDefinitionPayloadStorage* storage, XpEpubLookupStorage* lookup,
                                             XpAppTextSink query, void* context, uint8_t* html);
  uint8_t (*child_definition_payload_plain)(XpDefinitionPayloadStorage* storage, XpAppText title, uint32_t length,
                                            XpEpubDefinitionFill fill, void* context);
  uint8_t (*child_definition_payload_native)(XpDefinitionPayloadStorage* storage, XpDefinitionNativeTransfer* transfer,
                                             uint8_t* html);
  void (*child_definition_payload_drop)(XpDefinitionPayloadStorage* storage);
  void (*child_definition_heap)(XpEpubHeap* out);
  uint8_t (*child_definition_payload_prepare)(XpDefinitionPayloadStorage* storage, uint8_t slot, uint8_t html,
                                              uint8_t force_plain, XpUiRect body, XpDefinitionPayloadView* out);
  void (*child_definition_payload_release)(XpDefinitionPayloadStorage* storage, uint8_t slot, uint8_t keep_headword);
  uint8_t (*child_definition_payload_find)(XpDefinitionPayloadStorage* storage, uint8_t slot,
                                           XpEpubLookupStorage* lookup, XpAppText query, uint8_t* result);
  void (*child_definition_payload_commit)(XpDefinitionPayloadStorage* storage);
  uint8_t (*child_definition_selection_build)(XpDefinitionPayloadStorage* storage, int32_t page);
  void* (*child_definition_selection_child)(XpDefinitionPayloadStorage* storage,
                                            const XpDefinitionSelectionRequest* request, XpUiRect body);
  void (*child_definition_selection_release)(XpDefinitionPayloadStorage* storage);
  // Returns the HeaderBackTapTarget hit for (x,y), recorded by the last header draw.
  // (-1,-1) means no query.
  uint8_t (*child_definition_layout)(XpUiDefinitionLayout* out, int32_t x, int32_t y);
  void (*child_definition_draw_header)(const XpDefinitionPayloadStorage* storage, XpUiRect bounds, int32_t page,
                                       int32_t pages);
  void (*child_definition_draw_body)(const XpDefinitionPayloadStorage* storage, int32_t font, int32_t x, int32_t y,
                                     int32_t page);
  void (*child_directional_hints)(const XpAppText labels[6]);
  void (*child_dictionary_popup_layout)(XpUiDictionaryPopup* out);
  // Existing ActivityManager render completion wait; call only without model/render/resource locks.
  void (*child_wait_paint)(void);
  const XpClippingPhysical* clipping_physical;
  void (*child_list_help)(XpUiRect bounds, XpAppText text);
  void (*child_list_icon)(uint8_t kind, XpUiRect bounds, XpUiRect clip, uint8_t paint);
  uint8_t (*local_calendar)(XpEpubCalendar* out);
  XpAppText (*storage_read_file)(XpAppText path, void* storage);
  uint8_t (*storage_write_file_atomic)(XpAppText path, const uint8_t* bytes, uint32_t size);
#ifndef XP_APP_TEST
  void* (*protection_finish)(void* storage, uint8_t stage, const uint8_t* key, size_t length);
  void (*key_platform)(void* storage, uint8_t operation, XpAppText path, void* out);
#endif
} XpEpubLeaves;

#ifdef __cplusplus
}
#define XP_EPUB_ASSERT(condition, message) static_assert(condition, message)
#else
#define XP_EPUB_ASSERT(condition, message) _Static_assert(condition, message)
#endif
XP_EPUB_ASSERT(sizeof(XpEpubStatusFacts) == (sizeof(void*) == 8 ? 96 : 88), "XpEpubStatusFacts size");
XP_EPUB_ASSERT(offsetof(XpEpubStatusFacts, bookmark) == (sizeof(void*) == 8 ? 64 : 60), "XpEpubStatusFacts.bookmark");
XP_EPUB_ASSERT(offsetof(XpEpubStatusFacts, clock) == (sizeof(void*) == 8 ? 86 : 78), "XpEpubStatusFacts.clock");
XP_EPUB_ASSERT(sizeof(XpEpubPageView) == (sizeof(void*) == 8 ? 24 : 12), "XpEpubPageView size");
XP_EPUB_ASSERT(sizeof(XpEpubOwnedPage) == sizeof(XpEpubPageView) + 2 * sizeof(void*), "XpEpubOwnedPage size");
XP_EPUB_ASSERT(offsetof(XpEpubOwnedPage, owner) == sizeof(XpEpubPageView), "XpEpubOwnedPage.owner");
#ifdef XP_APP_TEST
XP_EPUB_ASSERT(sizeof(XpEpubFontScan) == 2 * sizeof(void*), "XpEpubFontScan size");
#endif
XP_EPUB_ASSERT(sizeof(XpEpubPhysicalOp) == (sizeof(void*) == 8 ? 24 : 16), "XpEpubPhysicalOp size");
XP_EPUB_ASSERT(sizeof(XpEpubPosition) == 92, "XpEpubPosition size");
XP_EPUB_ASSERT(offsetof(XpEpubPosition, visible_offset) == 12, "XpEpubPosition.visible_offset");
XP_EPUB_ASSERT(offsetof(XpEpubPosition, anchor) == 25, "XpEpubPosition.anchor");
XP_EPUB_ASSERT(sizeof(XpEpubGeometry) == 32, "XpEpubGeometry size");
#ifdef XP_APP_TEST
XP_EPUB_ASSERT(offsetof(XpEpubGeometry, has_font_cache) == 31, "XpEpubGeometry.has_font_cache");
#else
XP_EPUB_ASSERT(offsetof(XpEpubGeometry, has_font_cache) == 30, "XpEpubGeometry.has_font_cache");
#endif
XP_EPUB_ASSERT(offsetof(XpEpubGeometry, status_height) == 8, "XpEpubGeometry.status_height");
XP_EPUB_ASSERT(offsetof(XpEpubGeometry, ui_font_12) == 20, "XpEpubGeometry.ui_font_12");
XP_EPUB_ASSERT(offsetof(XpEpubGeometry, title_mode) == 24, "XpEpubGeometry.title_mode");
XP_EPUB_ASSERT(sizeof(XpEpubSelectionArgs) == 20, "XpEpubSelectionArgs size");
XP_EPUB_ASSERT(offsetof(XpEpubSelectionArgs, mode) == 16, "XpEpubSelectionArgs.mode");
XP_EPUB_ASSERT(sizeof(XpEpubSessionEvent) == 24, "XpEpubSessionEvent size");
XP_EPUB_ASSERT(offsetof(XpEpubSessionEvent, duration_seconds) == 16, "XpEpubSessionEvent.duration_seconds");
XP_EPUB_ASSERT(sizeof(XpEpubPopupInput) == 3, "XpEpubPopupInput size");
XP_EPUB_ASSERT(offsetof(XpEpubPopupInput, active) == 0, "XpEpubPopupInput.active");
XP_EPUB_ASSERT(offsetof(XpEpubPopupInput, changed) == 1, "XpEpubPopupInput.changed");
XP_EPUB_ASSERT(offsetof(XpEpubPopupInput, consumed) == 2, "XpEpubPopupInput.consumed");
XP_EPUB_ASSERT(sizeof(XpEpubInputFacts) == 16, "XpEpubInputFacts size");
XP_EPUB_ASSERT(offsetof(XpEpubInputFacts, touch) == 10, "XpEpubInputFacts.touch");
XP_EPUB_ASSERT(offsetof(XpEpubInputFacts, board_touch) == 11, "XpEpubInputFacts.board_touch");
XP_EPUB_ASSERT(offsetof(XpEpubInputFacts, home_action) == 12, "XpEpubInputFacts.home_action");
XP_EPUB_ASSERT(offsetof(XpEpubInputFacts, slide_owns_light) == 13, "XpEpubInputFacts.slide_owns_light");
XP_EPUB_ASSERT(offsetof(XpEpubSettings, orientation) == 4, "XpEpubSettings.orientation");
XP_EPUB_ASSERT(offsetof(XpEpubSettings, dictionary_available) == 26, "XpEpubSettings.dictionary_available");
XP_EPUB_ASSERT(offsetof(XpEpubSettings, font_family) == (sizeof(void*) == 8 ? 32 : 28), "XpEpubSettings.font_family");
XP_EPUB_ASSERT(sizeof(XpEpubSettings) == (sizeof(void*) == 8 ? 48 : 36), "XpEpubSettings size");
XP_EPUB_ASSERT(sizeof(XpEpubRadio) == 5, "XpEpubRadio size");
XP_EPUB_ASSERT(offsetof(XpEpubRadio, note) == 4, "XpEpubRadio.note");
XP_EPUB_ASSERT(sizeof(XpEpubPeer) == 82, "XpEpubPeer size");
XP_EPUB_ASSERT(offsetof(XpEpubPeer, name) == 0, "XpEpubPeer.name");
XP_EPUB_ASSERT(offsetof(XpEpubPeer, addr) == 64, "XpEpubPeer.addr");
#ifdef XP_SD_FONT_CATALOG  // Offsets below use original native-table indices.
#ifdef XP_APP_TEST
#define XP_EPUB_LEAF_INDEX(index)                                                                                    \
  ((index) - ((index) >= 9 ? 3 : 0) - ((index) >= 23 ? 2 : 0) + ((index) >= 24 ? 4 : 0) + ((index) >= 132 ? 1 : 0) - \
   ((index) > 116 ? 2 : 0) - ((index) > 119 ? 1 : 0) - ((index) >= 26 ? 1 : 0))
#define XP_EPUB_LEAF_COUNT 159
#else
#define XP_EPUB_LEAF_INDEX(index)                                                                                   \
  ((index) - ((index) >= 60 ? 6 : 0) - ((index) >= 93 ? 1 : 0) - ((index) >= 9 ? 3 : 0) - ((index) >= 23 ? 2 : 0) - \
   ((index) >= 120 ? 1 : 0) - ((index) >= 26 ? 1 : 0) - ((index) >= 133 ? 1 : 0) - ((index) > 98 ? 1 : 0) -         \
   ((index) > 108 ? 1 : 0) - ((index) > 109 ? 1 : 0) - ((index) > 110 ? 1 : 0) - ((index) > 111 ? 1 : 0) -          \
   ((index) > 120 ? 1 : 0) - ((index) > 116 ? 2 : 0))
#define XP_EPUB_LEAF_COUNT 142
#endif
#else
#ifdef XP_APP_TEST
#define XP_EPUB_LEAF_INDEX(index)                                                                                    \
  ((index) - ((index) >= 9 ? 3 : 0) - ((index) >= 23 ? 2 : 0) + ((index) >= 24 ? 4 : 0) + ((index) >= 132 ? 1 : 0) - \
   ((index) > 116 ? 2 : 0) - ((index) > 119 ? 1 : 0))
#define XP_EPUB_LEAF_COUNT 160
#else
#define XP_EPUB_LEAF_INDEX(index)                                                                                   \
  ((index) - ((index) >= 60 ? 6 : 0) - ((index) >= 93 ? 1 : 0) - ((index) >= 9 ? 3 : 0) - ((index) >= 23 ? 2 : 0) - \
   ((index) >= 120 ? 1 : 0) - ((index) >= 133 ? 1 : 0) - ((index) > 98 ? 1 : 0) - ((index) > 109 ? 1 : 0) -         \
   ((index) > 111 ? 1 : 0) - ((index) > 120 ? 1 : 0) - ((index) > 116 ? 2 : 0))
#define XP_EPUB_LEAF_COUNT 145
#endif
#endif
XP_EPUB_ASSERT(sizeof(XpEpubLeaves) == XP_EPUB_LEAF_COUNT * sizeof(void (*)(void)), "XpEpubLeaves size");
#define XP_EPUB_LEAF_OFFSET(field, index)                                                             \
  XP_EPUB_ASSERT(offsetof(XpEpubLeaves, field) == XP_EPUB_LEAF_INDEX(index) * sizeof(void (*)(void)), \
                 "XpEpubLeaves." #field)
XP_EPUB_LEAF_OFFSET(render_margins, 0);
XP_EPUB_LEAF_OFFSET(render_geometry, 1);
XP_EPUB_LEAF_OFFSET(render_restore_bw, 2);
XP_EPUB_LEAF_OFFSET(render_discard_bw, 3);
XP_EPUB_LEAF_OFFSET(render_orientation, 4);
XP_EPUB_LEAF_OFFSET(loan_count, 5);
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(protection_open, 9);
#else
XP_EPUB_LEAF_OFFSET(protection_index, 9);
XP_EPUB_LEAF_OFFSET(protection_finish, 163);
XP_EPUB_LEAF_OFFSET(key_platform, 164);
#endif
XP_EPUB_LEAF_OFFSET(protection_close, 10);
XP_EPUB_LEAF_OFFSET(protection_encrypted, 11);
XP_EPUB_LEAF_OFFSET(protection_size, 12);
XP_EPUB_LEAF_OFFSET(protection_stream, 13);
XP_EPUB_LEAF_OFFSET(file_seek, 14);
XP_EPUB_LEAF_OFFSET(file_size, 15);
XP_EPUB_LEAF_OFFSET(path_remove_tree, 16);
XP_EPUB_LEAF_OFFSET(storage_read_release, 17);
XP_EPUB_LEAF_OFFSET(paint_dither, 18);
XP_EPUB_LEAF_OFFSET(paint_pixel, 19);
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(paint_status, 20);
#else
XP_EPUB_LEAF_OFFSET(status_facts, 20);
#endif
XP_EPUB_LEAF_OFFSET(fonts_ensure_loaded, 23);
#ifdef XP_APP_TEST
XP_EPUB_ASSERT(offsetof(XpEpubLeaves, fonts_release_caches) == 19 * sizeof(void (*)(void)), "host font fixture offset");
#endif
#ifdef XP_APP_TEST
XP_EPUB_ASSERT(offsetof(XpEpubLeaves, fonts_prewarm_begin) == 20 * sizeof(void (*)(void)), "host font fixture offset");
#endif
#ifdef XP_APP_TEST
XP_EPUB_ASSERT(offsetof(XpEpubLeaves, fonts_prewarm_finish_scan) == 21 * sizeof(void (*)(void)),
               "host font fixture offset");
#endif
#ifdef XP_APP_TEST
XP_EPUB_ASSERT(offsetof(XpEpubLeaves, fonts_prewarm_drop) == 22 * sizeof(void (*)(void)), "host font fixture offset");
#endif
XP_EPUB_LEAF_OFFSET(fonts_take_corrupt_notice, 24);
#ifndef XP_SD_FONT_CATALOG
XP_EPUB_LEAF_OFFSET(font_sizes, 25);
#endif
XP_EPUB_LEAF_OFFSET(settings_snapshot, 26);
XP_EPUB_LEAF_OFFSET(settings_render_spec, 27);
XP_EPUB_LEAF_OFFSET(settings_orientation, 28);
XP_EPUB_LEAF_OFFSET(settings_font_size, 29);
XP_EPUB_LEAF_OFFSET(settings_line_spacing, 30);
XP_EPUB_LEAF_OFFSET(settings_alignment, 31);
XP_EPUB_LEAF_OFFSET(settings_focus_reading, 32);
XP_EPUB_LEAF_OFFSET(settings_inversion, 33);
XP_EPUB_LEAF_OFFSET(settings_ble_enabled, 34);
XP_EPUB_LEAF_OFFSET(settings_save, 35);
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(input_services, 36);
#else
XP_EPUB_LEAF_OFFSET(input_sources, 36);
#endif
XP_EPUB_LEAF_OFFSET(input_facts, 37);
XP_EPUB_LEAF_OFFSET(input_pressed, 38);
XP_EPUB_LEAF_OFFSET(input_screen_point, 39);
XP_EPUB_LEAF_OFFSET(input_screen_tap, 40);
XP_EPUB_LEAF_OFFSET(input_raw_contact, 41);
XP_EPUB_LEAF_OFFSET(render_busy, 42);
XP_EPUB_LEAF_OFFSET(file_open_read, 43);
XP_EPUB_LEAF_OFFSET(file_open_write, 44);
XP_EPUB_LEAF_OFFSET(file_read, 45);
XP_EPUB_LEAF_OFFSET(file_write, 46);
XP_EPUB_LEAF_OFFSET(file_close, 47);
XP_EPUB_LEAF_OFFSET(path_remove, 48);
XP_EPUB_LEAF_OFFSET(path_rename, 49);
XP_EPUB_LEAF_OFFSET(path_mkdir, 50);
XP_EPUB_LEAF_OFFSET(native_path_hash, 51);
XP_EPUB_LEAF_OFFSET(heap_snapshot, 52);
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(radio_status, 53);
XP_EPUB_LEAF_OFFSET(radio_before_build, 54);
XP_EPUB_LEAF_OFFSET(radio_settle_start, 55);
XP_EPUB_LEAF_OFFSET(radio_holds_heap, 56);
XP_EPUB_LEAF_OFFSET(radio_after_paint, 57);
XP_EPUB_LEAF_OFFSET(radio_switch_off, 58);
XP_EPUB_LEAF_OFFSET(radio_request_connect, 59);
#else
XP_EPUB_LEAF_OFFSET(radio_peer, 53);
#endif
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(trusted_now, 60);
#else
XP_EPUB_LEAF_OFFSET(time_services, 60);
#endif
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(trusted_sync, 61);
#else
XP_EPUB_LEAF_OFFSET(time_start_sync, 61);
#endif
XP_EPUB_LEAF_OFFSET(wifi_connected, 62);
XP_EPUB_LEAF_OFFSET(wifi_disconnect, 63);
XP_EPUB_LEAF_OFFSET(restart_reader, 64);
XP_EPUB_LEAF_OFFSET(child_text_settings, 65);
XP_EPUB_LEAF_OFFSET(child_selection, 66);

XP_EPUB_LEAF_OFFSET(child_wifi, 67);
XP_EPUB_LEAF_OFFSET(child_sync, 68);
XP_EPUB_LEAF_OFFSET(popup_open, 69);
XP_EPUB_LEAF_OFFSET(popup_close, 70);
XP_EPUB_LEAF_OFFSET(popup_active, 71);
XP_EPUB_LEAF_OFFSET(popup_show, 72);
XP_EPUB_LEAF_OFFSET(popup_dismiss, 73);
XP_EPUB_LEAF_OFFSET(popup_input, 74);
XP_EPUB_LEAF_OFFSET(popup_paint, 75);
XP_EPUB_LEAF_OFFSET(popup_process_render, 76);
XP_EPUB_LEAF_OFFSET(end_open, 77);
XP_EPUB_LEAF_OFFSET(end_close, 78);
XP_EPUB_LEAF_OFFSET(end_load, 79);
XP_EPUB_LEAF_OFFSET(end_menu_active, 80);
XP_EPUB_LEAF_OFFSET(end_input, 81);
XP_EPUB_LEAF_OFFSET(end_paint, 82);

#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(recents_remove, 83);
#else
XP_EPUB_LEAF_OFFSET(store_rows, 83);
#endif
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(recents_add, 84);
#else
XP_EPUB_LEAF_OFFSET(store_rows_close, 84);
#endif
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(recents_update_path, 85);
#else
XP_EPUB_LEAF_OFFSET(store_move_row, 85);
#endif
XP_EPUB_LEAF_OFFSET(state_open_path, 86);
XP_EPUB_LEAF_OFFSET(state_set_open_path, 87);
XP_EPUB_LEAF_OFFSET(state_reset_reader_count, 88);
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(state_save, 89);
#else
XP_EPUB_LEAF_OFFSET(store_document, 89);
#endif
XP_EPUB_LEAF_OFFSET(has_sync_credentials, 90);
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(has_exit_subscriber, 91);
XP_EPUB_LEAF_OFFSET(has_session_subscriber, 92);
#else
XP_EPUB_LEAF_OFFSET(subscribers, 91);
#endif
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(document_id, 93);
#else
XP_EPUB_LEAF_OFFSET(document_io, 93);
#endif
XP_EPUB_LEAF_OFFSET(emit_open, 94);
XP_EPUB_LEAF_OFFSET(emit_exit, 95);
XP_EPUB_LEAF_OFFSET(emit_session, 96);
XP_EPUB_LEAF_OFFSET(log, 97);
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(font_text_advance, 98);
#endif
XP_EPUB_LEAF_OFFSET(log_enabled, 99);
XP_EPUB_LEAF_OFFSET(input_snapshot, 100);
XP_EPUB_LEAF_OFFSET(cache_lock, 101);
XP_EPUB_LEAF_OFFSET(cache_unlock, 102);
XP_EPUB_LEAF_OFFSET(scratch_claim, 103);
XP_EPUB_LEAF_OFFSET(scratch_release, 104);
XP_EPUB_LEAF_OFFSET(inflate_oom_log, 105);
XP_EPUB_LEAF_OFFSET(build_metrics, 106);
XP_EPUB_LEAF_OFFSET(build_allocator, 107);
#if defined(XP_APP_TEST) || !defined(XP_SD_FONT_CATALOG)
XP_EPUB_LEAF_OFFSET(font_variant, 108);
#endif
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(font_line_height, 109);
#endif
#if defined(XP_APP_TEST) || !defined(XP_SD_FONT_CATALOG)
XP_EPUB_LEAF_OFFSET(font_ready, 110);
#endif
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(font_codepoint, 111);
#endif
XP_EPUB_LEAF_OFFSET(hyphen_language, 112);
XP_EPUB_LEAF_OFFSET(file_position, 113);
XP_EPUB_LEAF_OFFSET(file_flush, 114);
XP_EPUB_LEAF_OFFSET(framebuffer_lend, 117);
XP_EPUB_LEAF_OFFSET(framebuffer_restore, 118);
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(font_ascent, 120);
#endif
XP_EPUB_LEAF_OFFSET(render_physical_query, 121);
XP_EPUB_LEAF_OFFSET(render_physical_operation, 122);
XP_EPUB_LEAF_OFFSET(image_read_window, 123);
XP_EPUB_LEAF_OFFSET(display_source, 124);
XP_EPUB_LEAF_OFFSET(framebuffer_view, 125);
XP_EPUB_LEAF_OFFSET(child_list_theme, 126);
XP_EPUB_LEAF_OFFSET(child_utility_layout, 127);
XP_EPUB_LEAF_OFFSET(child_header, 128);
XP_EPUB_LEAF_OFFSET(child_utility_footer, 129);
XP_EPUB_LEAF_OFFSET(child_qr_draw, 130);
XP_EPUB_LEAF_OFFSET(child_toolbar_icon, 131);
#ifdef XP_APP_TEST
#ifdef XP_SD_FONT_CATALOG
XP_EPUB_ASSERT(offsetof(XpEpubLeaves, child_clear_fonts) == 127 * sizeof(void (*)(void)), "host font fixture offset");
#else
XP_EPUB_ASSERT(offsetof(XpEpubLeaves, child_clear_fonts) == 128 * sizeof(void (*)(void)), "host font fixture offset");
#endif
#endif
#ifdef XP_APP_TEST
XP_EPUB_LEAF_OFFSET(child_prewarm_rows, 132);
#endif
XP_EPUB_LEAF_OFFSET(child_lookup_init, 133);
XP_EPUB_LEAF_OFFSET(child_lookup_drop, 134);
XP_EPUB_LEAF_OFFSET(child_lookup_open, 135);
XP_EPUB_LEAF_OFFSET(child_lookup_index, 136);
XP_EPUB_LEAF_OFFSET(child_lookup_find, 137);
XP_EPUB_LEAF_OFFSET(child_lookup_discard, 138);
XP_EPUB_LEAF_OFFSET(child_definition_payload_lookup, 139);
XP_EPUB_LEAF_OFFSET(child_definition_payload_plain, 140);
XP_EPUB_LEAF_OFFSET(child_definition_payload_native, 141);
XP_EPUB_LEAF_OFFSET(child_definition_payload_drop, 142);
XP_EPUB_LEAF_OFFSET(child_definition_heap, 143);
XP_EPUB_LEAF_OFFSET(child_definition_payload_prepare, 144);
XP_EPUB_LEAF_OFFSET(child_definition_payload_release, 145);
XP_EPUB_LEAF_OFFSET(child_definition_payload_find, 146);
XP_EPUB_LEAF_OFFSET(child_definition_payload_commit, 147);
XP_EPUB_LEAF_OFFSET(child_definition_selection_build, 148);
XP_EPUB_LEAF_OFFSET(child_definition_selection_child, 149);
XP_EPUB_LEAF_OFFSET(child_definition_selection_release, 150);
XP_EPUB_LEAF_OFFSET(child_definition_layout, 151);
XP_EPUB_LEAF_OFFSET(child_definition_draw_header, 152);
XP_EPUB_LEAF_OFFSET(child_definition_draw_body, 153);
XP_EPUB_LEAF_OFFSET(child_directional_hints, 154);
XP_EPUB_LEAF_OFFSET(child_dictionary_popup_layout, 155);
XP_EPUB_LEAF_OFFSET(child_wait_paint, 156);
XP_EPUB_LEAF_OFFSET(clipping_physical, 157);
XP_EPUB_LEAF_OFFSET(child_list_help, 158);
XP_EPUB_LEAF_OFFSET(child_list_icon, 159);
XP_EPUB_LEAF_OFFSET(local_calendar, 160);
XP_EPUB_LEAF_OFFSET(storage_read_file, 161);
XP_EPUB_LEAF_OFFSET(storage_write_file_atomic, 162);
#undef XP_EPUB_LEAF_OFFSET
#undef XP_EPUB_LEAF_INDEX
#undef XP_EPUB_LEAF_COUNT
XP_EPUB_ASSERT(sizeof(XpEpubBookBorrow) == 6 * sizeof(void (*)(void)), "XpEpubBookBorrow size");
XP_EPUB_ASSERT(offsetof(XpEpubBookBorrow, spine) == 0, "XpEpubBookBorrow.spine");
XP_EPUB_ASSERT(offsetof(XpEpubBookBorrow, item_size) == 5 * sizeof(void (*)(void)), "XpEpubBookBorrow.item_size");
XP_EPUB_ASSERT(sizeof(XpEpubBookView) == (sizeof(void*) == 8 ? 40 : 24), "XpEpubBookView size");
XP_EPUB_ASSERT(offsetof(XpEpubSpine, toc_index) == sizeof(XpAppText) + 4, "XpEpubSpine.toc_index");
XP_EPUB_ASSERT(sizeof(XpDefinitionPayloadStorage) == 43 * sizeof(uintptr_t), "payload storage size");
XP_EPUB_ASSERT(sizeof(XpDefinitionPayloadView) == 8, "payload view size");
XP_EPUB_ASSERT(sizeof(XpDefinitionSelectionPosition) == 12, "selection position size");
XP_EPUB_ASSERT(sizeof(XpDefinitionSelectionRequest) == 24, "selection request size");
XP_EPUB_ASSERT(offsetof(XpDefinitionSelectionRequest, selection) == 4, "selection request position");
XP_EPUB_ASSERT(offsetof(XpDefinitionSelectionRequest, x) == 16, "selection request x");
XP_EPUB_ASSERT(offsetof(XpDefinitionSelectionRequest, y) == 20, "selection request y");
#undef XP_EPUB_ASSERT

#ifndef __cplusplus
_Static_assert(_Generic(((XpEpubLeaves*)0)->end_input,
                   void (*)(void*, void (*)(void*, uint8_t, XpAppText), void*): 1,
                   default: 0),
               "XpEpubLeaves.end_input signature");
#ifdef XP_APP_TEST
_Static_assert(_Generic(((XpEpubLeaves*)0)->recents_add, void (*)(const XpAppBook*): 1, default: 0),
               "XpEpubLeaves.recents_add signature");
#endif
#ifdef XP_APP_TEST
_Static_assert(_Generic(((XpEpubLeaves*)0)->font_text_advance,
                   int32_t (*)(XpAppText, int32_t, uint8_t, int8_t): 1,
                   default: 0),
               "XpEpubLeaves.font_text_advance signature");
#endif
#endif

#ifndef __cplusplus
_Static_assert(_Generic(((XpEpubLeaves*)0)->log_enabled, uint8_t (*)(uint8_t): 1, default: 0),
               "XpEpubLeaves.log_enabled signature");
#endif

#ifndef __cplusplus
#ifdef XP_APP_TEST
_Static_assert(_Generic(((XpEpubLeaves*)0)->input_facts, void (*)(XpEpubInputFacts*): 1, default: 0),
               "XpEpubLeaves.input_facts signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->input_snapshot, void (*)(XpUiInput*): 1, default: 0),
               "XpEpubLeaves.input_snapshot signature");
#else
_Static_assert(_Generic(((XpEpubLeaves*)0)->input_facts, void (*)(uint8_t, XpEpubInputFacts*): 1, default: 0),
               "XpEpubLeaves.input_facts signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->input_snapshot, void (*)(uint8_t, XpUiInput*): 1, default: 0),
               "XpEpubLeaves.input_snapshot signature");
#endif
_Static_assert(_Generic(((XpEpubGeometry*)0)->has_font_cache, uint8_t: 1, default: 0),
               "XpEpubGeometry.has_font_cache type");
_Static_assert(_Alignof(XpEpubGeometry) == 4, "XpEpubGeometry alignment");
#endif

#if !defined(__cplusplus) && defined(XP_APP_TEST)
_Static_assert(_Generic(((XpEpubLeaves*)0)->radio_status, void (*)(XpEpubRadio*, XpEpubPeer*): 1, default: 0),
               "XpEpubLeaves.radio_status signature");
#endif

#if !defined(__cplusplus) && !defined(XP_APP_TEST)
_Static_assert(_Generic(((XpEpubLeaves*)0)->radio_peer, void (*)(XpEpubPeer*): 1, default: 0),
               "XpEpubLeaves.radio_peer signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->subscribers, const XpPluginSubscriber* (*)(void): 1, default: 0),
               "XpEpubLeaves.subscribers signature");
#endif
