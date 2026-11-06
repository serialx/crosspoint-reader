#pragma once
#include "xp_app.h"
#include "xp_book.h"
#include "xp_display.h"
#include "xp_ffi.h"
#include "xp_ui_reader.h"

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
  uint8_t has_ttf;
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
  int32_t left;
  int32_t top;
  int32_t x;
  int32_t y;
  uint8_t mode;
} XpEpubSelectionArgs;

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

typedef struct {
  uint8_t active;
  uint8_t changed;
  uint8_t consumed;
} XpEpubPopupInput;

typedef struct {
  XpAppText title;
  float book_progress;
  int32_t page;
  int32_t pages;
  int32_t y_offset;
  uint8_t bookmarked;
  uint8_t building;
} XpEpubStatus;

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
  uint16_t spine;
  uint16_t start_page;
  uint16_t end_page;
  uint16_t page_count;
  uint16_t start_word;
  uint16_t end_word;
  uint32_t signature;
  uint32_t start_offset;
  uint32_t end_offset;
} XpEpubClip;

typedef struct {
  XpEpubClip range;
  uint16_t word_count;
  uint16_t paragraph;
  XpAppText chapter;
  XpAppNativeText text;
} XpEpubClipInsert;

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
typedef uint8_t (*XpEpubToolbarRowSink)(void* context, const XpEpubToolbarRow* value);
typedef void (*XpEpubBuildPopup)(void* context);
typedef uint8_t (*XpEpubRowSource)(void* context, int32_t index, XpEpubToolbarRowSink sink, void* sink_context);

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

// Rust owns the scope storage; native code only constructs and destroys its
// physical font-cache loan. The storage is immovable while initialized.
typedef struct {
  uintptr_t words[2];
} XpEpubPrewarm;

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

typedef struct XpEpubLeaves {
  void (*render_margins)(int32_t* margins);
  void (*render_geometry)(XpEpubGeometry* out);
  void (*render_restore_bw)(uint8_t resync);
  void (*render_discard_bw)(void);
  void (*render_orientation)(uint8_t orientation);
  uint32_t (*loan_count)(void);
  // Opens an unloaded compatibility facade; its getters forward to the Rust book.
  // Closing requires all Section/child loans to have ended; no Rust book is freed here.
  void* (*book_facade_open)(void* book, const XpEpubBookView* view, const XpEpubBookBorrow* borrow);
  void (*book_facade_close)(void* facade);
  // Null detaches the extractor; non-null must be this session's own live facade.
  void (*book_set_extractor)(void* facade);
  // Null without error is a plain book. Error text is borrowed only during the sink.
  void* (*protection_open)(XpAppText path, XpAppTextSink error, void* context);
  void (*protection_close)(void* protection);
  uint8_t (*protection_encrypted)(void* protection, XpAppText path);
  uint32_t (*protection_size)(void* protection, XpAppText path);
  uint8_t (*protection_stream)(void* protection, XpAppText path, XpBookSink sink, void* context);
  uint8_t (*file_seek)(void* file, uint32_t offset);
  uint32_t (*file_size)(void* file);
  uint8_t (*path_remove_tree)(XpAppText path);
  void (*progress_to_reader)(void* book, const XpEpubSavedProgress* saved, int32_t spine, int32_t count,
                             XpEpubPosition* out);
  uint8_t (*progress_to_saved)(void* book, const XpEpubPosition* position, XpEpubSavedProgressSink sink, void* context);
  uint8_t (*progress_save)(void* book, const XpEpubPosition* position);
  void (*paint_dither)(int32_t x, int32_t y, int32_t width, int32_t height);
  void (*paint_pixel)(int32_t x, int32_t y);
  void (*paint_status)(const XpEpubStatus* status);
  void (*image_clear_failures)(void);
  void (*image_release_cache)(void);
  void (*fonts_ensure_loaded)(void);
  void (*fonts_release_caches)(void);
  uint8_t (*fonts_prewarm_begin)(XpEpubPrewarm* storage);
  void (*fonts_prewarm_finish_scan)(XpEpubPrewarm* storage);
  void (*fonts_prewarm_drop)(XpEpubPrewarm* storage);
  uint8_t (*fonts_take_corrupt_notice)(void);
  uint8_t (*font_sizes)(XpAppText family, XpAppTextSink sink, void* context);
  // Font is meaningful only when requested; otherwise font is zero and resolution is skipped.
  void (*settings_snapshot)(uint8_t resolve_font, XpEpubSettings* out);
  void (*settings_render_spec)(uint16_t width, uint16_t height, XpEpubRenderSpec* out);
  void (*settings_orientation)(uint8_t value);
  void (*settings_font_size)(uint8_t value);
  void (*settings_line_spacing)(uint8_t value);
  void (*settings_alignment)(uint8_t value);
  void (*settings_focus_reading)(uint8_t value);
  void (*settings_inversion)(uint8_t value);
  void (*settings_ble_enabled)(uint8_t value);
  void (*settings_save)(void);
  const XpUiReaderInputServices* (*input_services)(void);
  void (*input_facts)(XpEpubInputFacts* out);
  uint8_t (*input_pressed)(uint32_t button);
  uint8_t (*input_screen_long)(int32_t* x, int32_t* y);
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
  void (*radio_status)(XpEpubRadio* out, XpEpubPeer* peer_or_null);
  uint8_t (*radio_before_build)(void);
  void (*radio_settle_start)(void);
  uint8_t (*radio_holds_heap)(void);
  void (*radio_after_paint)(void);
  void (*radio_switch_off)(void);
  void (*radio_request_connect)(void);
  int64_t (*trusted_now)(void);
  uint8_t (*trusted_sync)(uint32_t timeout_ms);
  uint8_t (*wifi_connected)(void);
  void (*wifi_disconnect)(uint8_t off);
  void (*restart_reader)(void);
  // Child factories return an owned native legacy wrapper for App adoption, never a raw Activity.
  // Non-null consumes each Rust Page owner; null leaves every owner with the caller.
  void* (*child_menu)(const XpEpubMenuArgs* args);
  void* (*child_chapter)(void* book, int32_t toc);
  void* (*child_percent)(int32_t percent);
  void* (*child_text_settings)(void);
  void* (*child_footnote)(XpEpubOwnedPage* page, int32_t left, int32_t top);
  void* (*child_dictionary)(XpEpubOwnedPage* page, int32_t left, int32_t top, XpAppText initial_text);
  void* (*child_selection)(XpEpubOwnedPage* pages, uint8_t count, const XpEpubSelectionArgs* args);
  void* (*child_bookmarks)(void* book, XpAppText path);
  void* (*child_clippings)(void);
  void* (*child_qr)(XpAppText text);
  void* (*child_wifi)(void);
  void* (*child_sync)(XpAppText path, const XpEpubPosition* position, const XpEpubSavedProgress* saved,
                      XpAppText chapter, uint8_t events_only);
  void* (*toolbar_open)(void);
  void (*toolbar_close)(void* toolbar);
  void (*toolbar_begin)(void* toolbar);
  uint8_t (*toolbar_paint)(void* toolbar, const XpEpubToolbarModel* model, XpEpubRowSource rows, void* context);
  void (*toolbar_route)(void* toolbar, XpEpubRouted* out);
  uint8_t (*toolbar_scroll)(void* toolbar, int32_t delta, int32_t count);
  int32_t (*toolbar_visible_rows)(void* toolbar);
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
  void* (*bookmarks_open)(void);
  void (*bookmarks_close)(void* bookmarks);
  void (*bookmarks_clear)(void* bookmarks);
  uint8_t (*bookmarks_load)(void* bookmarks, XpAppText path);
  uint8_t (*bookmarks_save)(void* bookmarks, XpAppText path);
  uint32_t (*bookmarks_count)(void* bookmarks);
  uint8_t (*bookmarks_row)(void* bookmarks, uint32_t index, XpEpubBookmarkSink sink, void* context);
  // Requires mutation access; returns the removed count. The predicate must not mutate or reenter.
  // The row and its text views are borrowed only for the synchronous predicate call.
  uint32_t (*bookmarks_retain)(void* bookmarks, uint8_t (*keep)(void*, const XpEpubBookmark*), void* context);
  uint8_t (*bookmarks_insert_front)(void* bookmarks, const XpEpubBookmark* row);
  uint8_t (*clippings_load)(const XpAppBook* book);
  void (*clippings_unload)(void);
  uint32_t (*clippings_count)(void);
  void (*clippings_row)(uint32_t index, XpEpubClip* out);
  uint8_t (*clippings_remove)(uint32_t index);
  uint8_t (*clippings_insert)(const XpEpubClipInsert* clipping);
  uint8_t (*clippings_export)(XpAppText title, XpAppText author, XpAppText chapter, int32_t percent,
                              XpAppNativeText text);
  uint8_t (*clippings_move_book)(XpAppText from, XpAppText to);
  uint8_t (*recents_remove)(XpAppText path);
  void (*recents_add)(const XpAppBook* book);
  void (*recents_update_path)(XpAppText from, XpAppText to, XpAppText old_cache, XpAppText new_cache);
  uint8_t (*state_open_path)(XpAppTextSink sink, void* context);
  void (*state_set_open_path)(XpAppText path);
  void (*state_reset_reader_count)(void);
  void (*state_save)(void);
  uint8_t (*has_sync_credentials)(void);
  uint8_t (*has_exit_subscriber)(void);
  uint8_t (*has_session_subscriber)(void);
  uint8_t (*document_id)(XpAppText path, XpAppTextSink sink, void* context);
  void (*emit_open)(XpAppText path);
  void (*emit_exit)(XpAppText path, int32_t percent);
  void (*emit_session)(XpAppText path, XpAppText document, const XpEpubSessionEvent* event);
  void (*log)(uint8_t severity, XpAppText origin, XpAppText message);
  int32_t (*font_text_advance)(XpAppText text, int32_t font, uint8_t style, int8_t character_spacing);
  uint8_t (*log_enabled)(uint8_t severity);
  void (*toolbar_reset_navigation)(void* toolbar, int32_t selected, int32_t top);
  void (*input_snapshot)(XpUiInput* out);
  // Lock order: ResourceAccess, cache, Storage.
  void (*cache_lock)(void);
  void (*cache_unlock)(void);
  uint8_t* (*scratch_claim)(uint32_t min_len);
  void (*scratch_release)(const uint8_t* bytes);
  void (*inflate_oom_log)(XpAppText allocation, uint32_t bytes);
  // Physical layout tables have process lifetime and never borrow a chapter owner.
  const XpParaMetrics* (*build_metrics)(void);
  const XpParaAllocator* (*build_allocator)(void);
  int32_t (*font_variant)(int32_t font, float scale);
  int32_t (*font_line_height)(int32_t font, float compression);
  int32_t (*font_ready)(int32_t font, int32_t fallback);
  uint8_t (*font_codepoint)(int32_t font, uint32_t codepoint, uint8_t style, int32_t* advance, int32_t* top);
  // Preserve the hint observed by retained native dictionary/paragraph clients.
  int32_t (*hyphen_language)(XpAppText language);
  uint32_t (*file_position)(void* file);
  void (*file_flush)(void* file);
  uint8_t (*image_supported)(XpAppText path);
  // 0: no decoder, 1: dimensions failed, 2: success. Path borrows terminated storage.
  uint8_t (*image_dimensions)(XpAppText path, uint16_t* width, uint16_t* height);
  // Lend returns whether this scope owns restoration; nested scopes return zero.
  uint8_t (*framebuffer_lend)(void);
  void (*framebuffer_restore)(uint8_t owned);
  // Painting borrows immutable arena bytes only for the synchronous call.
  void (*paint_arena)(const XpEpubPageView* page, int32_t font, int32_t left, int32_t top, uint8_t mode);
  uint8_t (*image_needs_decode)(XpAppText path, XpAppText source, int16_t width, int16_t height);
  int32_t (*font_ascent)(int32_t font);
  uint32_t (*render_physical_query)(uint8_t query);
  uint32_t (*render_physical_operation)(const XpEpubPhysicalOp* operation);
  // Plan/window/source pointers remain borrowed only until explicit detach.
  void (*render_plan)(XpPagePlan* plan, XpPageRowBounds* bounds, uint16_t count, uint8_t recording);
  void (*image_read_window)(uint8_t* bytes, uint32_t length);
  uint8_t (*display_source)(XpDisplaySource* source);
  uint8_t* (*framebuffer_view)(uint32_t* bytes);
} XpEpubLeaves;

#ifdef __cplusplus
}
#define XP_EPUB_ASSERT(condition, message) static_assert(condition, message)
#else
#define XP_EPUB_ASSERT(condition, message) _Static_assert(condition, message)
#endif
XP_EPUB_ASSERT(sizeof(XpEpubPageView) == (sizeof(void*) == 8 ? 24 : 12), "XpEpubPageView size");
XP_EPUB_ASSERT(sizeof(XpEpubOwnedPage) == sizeof(XpEpubPageView) + 2 * sizeof(void*), "XpEpubOwnedPage size");
XP_EPUB_ASSERT(offsetof(XpEpubOwnedPage, owner) == sizeof(XpEpubPageView), "XpEpubOwnedPage.owner");
XP_EPUB_ASSERT(sizeof(XpEpubPrewarm) == 2 * sizeof(void*), "XpEpubPrewarm size");
XP_EPUB_ASSERT(sizeof(XpEpubPhysicalOp) == (sizeof(void*) == 8 ? 24 : 16), "XpEpubPhysicalOp size");
XP_EPUB_ASSERT(sizeof(XpEpubPosition) == 92, "XpEpubPosition size");
XP_EPUB_ASSERT(offsetof(XpEpubPosition, visible_offset) == 12, "XpEpubPosition.visible_offset");
XP_EPUB_ASSERT(offsetof(XpEpubPosition, anchor) == 25, "XpEpubPosition.anchor");
XP_EPUB_ASSERT(sizeof(XpEpubGeometry) == 32, "XpEpubGeometry size");
XP_EPUB_ASSERT(offsetof(XpEpubGeometry, has_font_cache) == 31, "XpEpubGeometry.has_font_cache");
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
XP_EPUB_ASSERT(sizeof(XpEpubToolbarRow) == 2 * sizeof(XpAppText) + 8, "XpEpubToolbarRow size");
XP_EPUB_ASSERT(offsetof(XpEpubToolbarRow, label_spaces) == 2 * sizeof(XpAppText), "XpEpubToolbarRow.label_spaces");
XP_EPUB_ASSERT(offsetof(XpEpubToolbarRow, toggle) == 2 * sizeof(XpAppText) + 4, "XpEpubToolbarRow.toggle");
XP_EPUB_ASSERT(offsetof(XpEpubToolbarRow, checked) == 2 * sizeof(XpAppText) + 5, "XpEpubToolbarRow.checked");
XP_EPUB_ASSERT(sizeof(XpEpubLeaves) == 167 * sizeof(void (*)(void)), "XpEpubLeaves size");
#define XP_EPUB_LEAF_OFFSET(field, index) \
  XP_EPUB_ASSERT(offsetof(XpEpubLeaves, field) == (index) * sizeof(void (*)(void)), "XpEpubLeaves." #field)
XP_EPUB_LEAF_OFFSET(render_margins, 0);
XP_EPUB_LEAF_OFFSET(render_geometry, 1);
XP_EPUB_LEAF_OFFSET(render_restore_bw, 2);
XP_EPUB_LEAF_OFFSET(render_discard_bw, 3);
XP_EPUB_LEAF_OFFSET(render_orientation, 4);
XP_EPUB_LEAF_OFFSET(loan_count, 5);
XP_EPUB_LEAF_OFFSET(book_facade_open, 6);
XP_EPUB_LEAF_OFFSET(book_facade_close, 7);
XP_EPUB_LEAF_OFFSET(book_set_extractor, 8);
XP_EPUB_LEAF_OFFSET(protection_open, 9);
XP_EPUB_LEAF_OFFSET(protection_close, 10);
XP_EPUB_LEAF_OFFSET(protection_encrypted, 11);
XP_EPUB_LEAF_OFFSET(protection_size, 12);
XP_EPUB_LEAF_OFFSET(protection_stream, 13);
XP_EPUB_LEAF_OFFSET(file_seek, 14);
XP_EPUB_LEAF_OFFSET(file_size, 15);
XP_EPUB_LEAF_OFFSET(path_remove_tree, 16);
XP_EPUB_LEAF_OFFSET(progress_to_reader, 17);
XP_EPUB_LEAF_OFFSET(progress_to_saved, 18);
XP_EPUB_LEAF_OFFSET(progress_save, 19);
XP_EPUB_LEAF_OFFSET(paint_dither, 20);
XP_EPUB_LEAF_OFFSET(paint_pixel, 21);
XP_EPUB_LEAF_OFFSET(paint_status, 22);
XP_EPUB_LEAF_OFFSET(image_clear_failures, 23);
XP_EPUB_LEAF_OFFSET(image_release_cache, 24);
XP_EPUB_LEAF_OFFSET(fonts_ensure_loaded, 25);
XP_EPUB_LEAF_OFFSET(fonts_release_caches, 26);
XP_EPUB_LEAF_OFFSET(fonts_prewarm_begin, 27);
XP_EPUB_LEAF_OFFSET(fonts_prewarm_finish_scan, 28);
XP_EPUB_LEAF_OFFSET(fonts_prewarm_drop, 29);
XP_EPUB_LEAF_OFFSET(fonts_take_corrupt_notice, 30);
XP_EPUB_LEAF_OFFSET(font_sizes, 31);
XP_EPUB_LEAF_OFFSET(settings_snapshot, 32);
XP_EPUB_LEAF_OFFSET(settings_render_spec, 33);
XP_EPUB_LEAF_OFFSET(settings_orientation, 34);
XP_EPUB_LEAF_OFFSET(settings_font_size, 35);
XP_EPUB_LEAF_OFFSET(settings_line_spacing, 36);
XP_EPUB_LEAF_OFFSET(settings_alignment, 37);
XP_EPUB_LEAF_OFFSET(settings_focus_reading, 38);
XP_EPUB_LEAF_OFFSET(settings_inversion, 39);
XP_EPUB_LEAF_OFFSET(settings_ble_enabled, 40);
XP_EPUB_LEAF_OFFSET(settings_save, 41);
XP_EPUB_LEAF_OFFSET(input_services, 42);
XP_EPUB_LEAF_OFFSET(input_facts, 43);
XP_EPUB_LEAF_OFFSET(input_pressed, 44);
XP_EPUB_LEAF_OFFSET(input_screen_long, 45);
XP_EPUB_LEAF_OFFSET(input_screen_tap, 46);
XP_EPUB_LEAF_OFFSET(input_raw_contact, 47);
XP_EPUB_LEAF_OFFSET(render_busy, 48);
XP_EPUB_LEAF_OFFSET(file_open_read, 49);
XP_EPUB_LEAF_OFFSET(file_open_write, 50);
XP_EPUB_LEAF_OFFSET(file_read, 51);
XP_EPUB_LEAF_OFFSET(file_write, 52);
XP_EPUB_LEAF_OFFSET(file_close, 53);
XP_EPUB_LEAF_OFFSET(path_remove, 54);
XP_EPUB_LEAF_OFFSET(path_rename, 55);
XP_EPUB_LEAF_OFFSET(path_mkdir, 56);
XP_EPUB_LEAF_OFFSET(native_path_hash, 57);
XP_EPUB_LEAF_OFFSET(heap_snapshot, 58);
XP_EPUB_LEAF_OFFSET(radio_status, 59);
XP_EPUB_LEAF_OFFSET(radio_before_build, 60);
XP_EPUB_LEAF_OFFSET(radio_settle_start, 61);
XP_EPUB_LEAF_OFFSET(radio_holds_heap, 62);
XP_EPUB_LEAF_OFFSET(radio_after_paint, 63);
XP_EPUB_LEAF_OFFSET(radio_switch_off, 64);
XP_EPUB_LEAF_OFFSET(radio_request_connect, 65);
XP_EPUB_LEAF_OFFSET(trusted_now, 66);
XP_EPUB_LEAF_OFFSET(trusted_sync, 67);
XP_EPUB_LEAF_OFFSET(wifi_connected, 68);
XP_EPUB_LEAF_OFFSET(wifi_disconnect, 69);
XP_EPUB_LEAF_OFFSET(restart_reader, 70);
XP_EPUB_LEAF_OFFSET(child_menu, 71);
XP_EPUB_LEAF_OFFSET(child_chapter, 72);
XP_EPUB_LEAF_OFFSET(child_percent, 73);
XP_EPUB_LEAF_OFFSET(child_text_settings, 74);
XP_EPUB_LEAF_OFFSET(child_footnote, 75);
XP_EPUB_LEAF_OFFSET(child_dictionary, 76);
XP_EPUB_LEAF_OFFSET(child_selection, 77);
XP_EPUB_LEAF_OFFSET(child_bookmarks, 78);
XP_EPUB_LEAF_OFFSET(child_clippings, 79);
XP_EPUB_LEAF_OFFSET(child_qr, 80);
XP_EPUB_LEAF_OFFSET(child_wifi, 81);
XP_EPUB_LEAF_OFFSET(child_sync, 82);
XP_EPUB_LEAF_OFFSET(toolbar_open, 83);
XP_EPUB_LEAF_OFFSET(toolbar_close, 84);
XP_EPUB_LEAF_OFFSET(toolbar_begin, 85);
XP_EPUB_LEAF_OFFSET(toolbar_paint, 86);
XP_EPUB_LEAF_OFFSET(toolbar_route, 87);
XP_EPUB_LEAF_OFFSET(toolbar_scroll, 88);
XP_EPUB_LEAF_OFFSET(toolbar_visible_rows, 89);
XP_EPUB_LEAF_OFFSET(popup_open, 90);
XP_EPUB_LEAF_OFFSET(popup_close, 91);
XP_EPUB_LEAF_OFFSET(popup_active, 92);
XP_EPUB_LEAF_OFFSET(popup_show, 93);
XP_EPUB_LEAF_OFFSET(popup_dismiss, 94);
XP_EPUB_LEAF_OFFSET(popup_input, 95);
XP_EPUB_LEAF_OFFSET(popup_paint, 96);
XP_EPUB_LEAF_OFFSET(popup_process_render, 97);
XP_EPUB_LEAF_OFFSET(end_open, 98);
XP_EPUB_LEAF_OFFSET(end_close, 99);
XP_EPUB_LEAF_OFFSET(end_load, 100);
XP_EPUB_LEAF_OFFSET(end_menu_active, 101);
XP_EPUB_LEAF_OFFSET(end_input, 102);
XP_EPUB_LEAF_OFFSET(end_paint, 103);
XP_EPUB_LEAF_OFFSET(bookmarks_open, 104);
XP_EPUB_LEAF_OFFSET(bookmarks_close, 105);
XP_EPUB_LEAF_OFFSET(bookmarks_clear, 106);
XP_EPUB_LEAF_OFFSET(bookmarks_load, 107);
XP_EPUB_LEAF_OFFSET(bookmarks_save, 108);
XP_EPUB_LEAF_OFFSET(bookmarks_count, 109);
XP_EPUB_LEAF_OFFSET(bookmarks_row, 110);
XP_EPUB_LEAF_OFFSET(bookmarks_retain, 111);
XP_EPUB_LEAF_OFFSET(bookmarks_insert_front, 112);
XP_EPUB_LEAF_OFFSET(clippings_load, 113);
XP_EPUB_LEAF_OFFSET(clippings_unload, 114);
XP_EPUB_LEAF_OFFSET(clippings_count, 115);
XP_EPUB_LEAF_OFFSET(clippings_row, 116);
XP_EPUB_LEAF_OFFSET(clippings_remove, 117);
XP_EPUB_LEAF_OFFSET(clippings_insert, 118);
XP_EPUB_LEAF_OFFSET(clippings_export, 119);
XP_EPUB_LEAF_OFFSET(clippings_move_book, 120);
XP_EPUB_LEAF_OFFSET(recents_remove, 121);
XP_EPUB_LEAF_OFFSET(recents_add, 122);
XP_EPUB_LEAF_OFFSET(recents_update_path, 123);
XP_EPUB_LEAF_OFFSET(state_open_path, 124);
XP_EPUB_LEAF_OFFSET(state_set_open_path, 125);
XP_EPUB_LEAF_OFFSET(state_reset_reader_count, 126);
XP_EPUB_LEAF_OFFSET(state_save, 127);
XP_EPUB_LEAF_OFFSET(has_sync_credentials, 128);
XP_EPUB_LEAF_OFFSET(has_exit_subscriber, 129);
XP_EPUB_LEAF_OFFSET(has_session_subscriber, 130);
XP_EPUB_LEAF_OFFSET(document_id, 131);
XP_EPUB_LEAF_OFFSET(emit_open, 132);
XP_EPUB_LEAF_OFFSET(emit_exit, 133);
XP_EPUB_LEAF_OFFSET(emit_session, 134);
XP_EPUB_LEAF_OFFSET(log, 135);
XP_EPUB_LEAF_OFFSET(font_text_advance, 136);
XP_EPUB_LEAF_OFFSET(log_enabled, 137);
XP_EPUB_LEAF_OFFSET(toolbar_reset_navigation, 138);
XP_EPUB_LEAF_OFFSET(input_snapshot, 139);
XP_EPUB_LEAF_OFFSET(cache_lock, 140);
XP_EPUB_LEAF_OFFSET(cache_unlock, 141);
XP_EPUB_LEAF_OFFSET(scratch_claim, 142);
XP_EPUB_LEAF_OFFSET(scratch_release, 143);
XP_EPUB_LEAF_OFFSET(inflate_oom_log, 144);
XP_EPUB_LEAF_OFFSET(build_metrics, 145);
XP_EPUB_LEAF_OFFSET(build_allocator, 146);
XP_EPUB_LEAF_OFFSET(font_variant, 147);
XP_EPUB_LEAF_OFFSET(font_line_height, 148);
XP_EPUB_LEAF_OFFSET(font_ready, 149);
XP_EPUB_LEAF_OFFSET(font_codepoint, 150);
XP_EPUB_LEAF_OFFSET(hyphen_language, 151);
XP_EPUB_LEAF_OFFSET(file_position, 152);
XP_EPUB_LEAF_OFFSET(file_flush, 153);
XP_EPUB_LEAF_OFFSET(image_supported, 154);
XP_EPUB_LEAF_OFFSET(image_dimensions, 155);
XP_EPUB_LEAF_OFFSET(framebuffer_lend, 156);
XP_EPUB_LEAF_OFFSET(framebuffer_restore, 157);
XP_EPUB_LEAF_OFFSET(paint_arena, 158);
XP_EPUB_LEAF_OFFSET(image_needs_decode, 159);
XP_EPUB_LEAF_OFFSET(font_ascent, 160);
XP_EPUB_LEAF_OFFSET(render_physical_query, 161);
XP_EPUB_LEAF_OFFSET(render_physical_operation, 162);
XP_EPUB_LEAF_OFFSET(render_plan, 163);
XP_EPUB_LEAF_OFFSET(image_read_window, 164);
XP_EPUB_LEAF_OFFSET(display_source, 165);
XP_EPUB_LEAF_OFFSET(framebuffer_view, 166);
#undef XP_EPUB_LEAF_OFFSET
XP_EPUB_ASSERT(sizeof(XpEpubBookBorrow) == 6 * sizeof(void (*)(void)), "XpEpubBookBorrow size");
XP_EPUB_ASSERT(offsetof(XpEpubBookBorrow, spine) == 0, "XpEpubBookBorrow.spine");
XP_EPUB_ASSERT(offsetof(XpEpubBookBorrow, item_size) == 5 * sizeof(void (*)(void)), "XpEpubBookBorrow.item_size");
XP_EPUB_ASSERT(sizeof(XpEpubBookView) == (sizeof(void*) == 8 ? 40 : 24), "XpEpubBookView size");
XP_EPUB_ASSERT(offsetof(XpEpubSpine, toc_index) == sizeof(XpAppText) + 4, "XpEpubSpine.toc_index");
#undef XP_EPUB_ASSERT

#ifndef __cplusplus
_Static_assert(_Generic(((XpEpubLeaves*)0)->end_input,
                   void (*)(void*, void (*)(void*, uint8_t, XpAppText), void*): 1,
                   default: 0),
               "XpEpubLeaves.end_input signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->recents_add, void (*)(const XpAppBook*): 1, default: 0),
               "XpEpubLeaves.recents_add signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->font_text_advance,
                   int32_t (*)(XpAppText, int32_t, uint8_t, int8_t): 1,
                   default: 0),
               "XpEpubLeaves.font_text_advance signature");
#endif

#ifndef __cplusplus
_Static_assert(_Generic(((XpEpubLeaves*)0)->log_enabled, uint8_t (*)(uint8_t): 1, default: 0),
               "XpEpubLeaves.log_enabled signature");
#endif

#ifndef __cplusplus
_Static_assert(_Generic(((XpEpubLeaves*)0)->toolbar_reset_navigation, void (*)(void*, int32_t, int32_t): 1, default: 0),
               "XpEpubLeaves.toolbar_reset_navigation signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->input_snapshot, void (*)(XpUiInput*): 1, default: 0),
               "XpEpubLeaves.input_snapshot signature");
_Static_assert(_Generic(((XpEpubGeometry*)0)->has_font_cache, uint8_t: 1, default: 0),
               "XpEpubGeometry.has_font_cache type");
_Static_assert(_Alignof(XpEpubGeometry) == 4, "XpEpubGeometry alignment");
#endif

#ifndef __cplusplus
_Static_assert(_Generic(((XpEpubLeaves*)0)->radio_status, void (*)(XpEpubRadio*, XpEpubPeer*): 1, default: 0),
               "XpEpubLeaves.radio_status signature");
#endif
