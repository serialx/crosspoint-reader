#pragma once
#include "xp_app.h"
#include "xp_book.h"
#include "xp_ui_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

// Text views are synchronous; no callback drains navigation.
// Section may retain the build-popup callback with its stable owner context.
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
  XpAppText font_family;
} XpEpubSettings;

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

typedef uint8_t (*XpEpubSpineSink)(void* context, const XpEpubSpine* value);
typedef uint8_t (*XpEpubTocSink)(void* context, const XpEpubToc* value);
typedef uint8_t (*XpEpubSavedProgressSink)(void* context, const XpEpubSavedProgress* value);
typedef uint8_t (*XpEpubFootnoteSink)(void* context, const XpEpubFootnote* value);
typedef uint8_t (*XpEpubLinkSink)(void* context, const XpEpubLink* value);
typedef uint8_t (*XpEpubBookmarkSink)(void* context, const XpEpubBookmark* value);
typedef uint8_t (*XpEpubToolbarRowSink)(void* context, const XpEpubToolbarRow* value);
typedef void (*XpEpubBuildPopup)(void* context);
typedef void (*XpEpubHighlight)(void* context, void* page, int32_t font, int32_t left, int32_t top);
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

typedef struct XpEpubLeaves {
  void* (*resources_open)(void);
  void (*resources_close)(void* resources);
  const XpUiReaderRenderServices* (*render_services)(void* resources);
  void (*render_bind_page)(void* resources, void* page, int32_t font, int32_t left, int32_t top, int32_t bottom,
                           XpEpubHighlight highlight, void* context);
  void (*render_finish_page)(void* resources);
  void (*render_margins)(int32_t* margins);
  void (*render_geometry)(XpEpubGeometry* out);
  void (*render_restore_bw)(uint8_t resync);
  void (*render_discard_bw)(void);
  void (*render_orientation)(uint8_t orientation);
  uint8_t (*loan_begin)(void* resources);
  void (*loan_end)(void* resources);
  void (*loan_drop)(void* resources);
  uint32_t (*loan_count)(void);
  uint8_t (*strip_release_idle)(void* resources);
  // Opens an unloaded compatibility facade; its getters forward to the Rust book.
  // Closing requires all Section/child loans to have ended; no Rust book is freed here.
  void* (*book_facade_open)(void* book, const XpEpubBookView* view, const XpEpubBookBorrow* borrow);
  void (*book_facade_close)(void* facade);
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
  void* (*section_open)(void* book, int32_t spine);
  void (*section_close)(void* section);
  uint16_t (*section_page_count)(void* section);
  int32_t (*section_estimated_pages)(void* section);
  uint8_t (*section_building)(void* section);
  uint8_t (*section_complete)(void* section);
  uint8_t (*section_partial)(void* section);
  uint8_t (*section_load_cache)(void* section, const XpEpubRenderSpec* spec);
  uint8_t (*section_create_cache)(void* section, const XpEpubRenderSpec* spec, XpEpubBuildPopup popup, void* context);
  uint8_t (*section_start_build)(void* section, const XpEpubRenderSpec* spec, XpEpubBuildPopup popup, void* context);
  uint8_t (*section_build_more)(void* section, int32_t max_pages, uint8_t yield_on_input);
  void (*section_abandon_build)(void* section);
  uint8_t (*section_clear_cache)(void* section);
  uint8_t (*section_html_cached)(void* section);
  uint8_t (*section_find_anchor)(void* section, XpAppText anchor, uint16_t* page);
  uint8_t (*section_cached_anchor)(void* section, XpAppText anchor, uint16_t* page);
  // Caller chooses null for a temporary cached Section; book stays borrowed for this call.
  int32_t (*section_toc_for_page)(void* section_or_null, void* book, int32_t spine, int32_t page);
  uint8_t (*section_page_for_offset)(void* section, uint32_t offset, uint8_t prefer_first, uint16_t* page);
  uint8_t (*section_offset_for_page)(void* section, uint16_t page, uint32_t* offset);
  uint8_t (*section_paragraph_for_page)(void* section, uint16_t page, uint16_t* paragraph);
  uint8_t (*section_reached_offset)(void* section, uint32_t offset);
  void* (*section_load_page)(void* section, int32_t page);
  uint8_t (*section_page_text)(void* section, int32_t page, XpAppTextSink sink, void* context);
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
  void (*paint_dither)(int32_t x, int32_t y, int32_t width, int32_t height);
  void (*paint_pixel)(int32_t x, int32_t y);
  void (*paint_status)(const XpEpubStatus* status);
  void (*image_clear_failures)(void);
  void (*image_release_cache)(void);
  void (*fonts_ensure_loaded)(void);
  void (*fonts_release_caches)(void);
  uint8_t (*fonts_prewarm_begin)(void* resources);
  void (*fonts_prewarm_finish_scan)(void* resources);
  void (*fonts_prewarm_drop)(void* resources);
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
  // Non-null transfers the supplied Pages to the child; null leaves every Page with the caller.
  void* (*child_menu)(const XpEpubMenuArgs* args);
  void* (*child_chapter)(void* book, int32_t toc);
  void* (*child_percent)(int32_t percent);
  void* (*child_text_settings)(void);
  void* (*child_footnote)(void* page, int32_t left, int32_t top);
  void* (*child_dictionary)(void* page, int32_t left, int32_t top, XpAppText initial_text);
  void* (*child_selection)(void** pages, uint8_t count, const XpEpubSelectionArgs* args);
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
  int32_t (*page_word_font)(void* page, uint16_t element, uint16_t word, int32_t reader_font);
  int32_t (*page_line_font)(void* page, uint16_t element, int32_t reader_font);
  int32_t (*page_word_y_offset)(void* page, uint16_t element, uint16_t word, int32_t reader_font);
  int32_t (*page_line_ruby_shift)(void* page, uint16_t element, int32_t line_font);
  int32_t (*font_text_advance)(XpAppText text, int32_t font, uint8_t style, int8_t character_spacing);
  uint8_t (*log_enabled)(uint8_t severity);
  void (*toolbar_reset_navigation)(void* toolbar, int32_t selected, int32_t top);
  void (*input_snapshot)(XpUiInput* out);
  void (*page_render)(void* page, int32_t font, int32_t left, int32_t top);
  // Lock order: ResourceAccess, cache, Storage.
  void (*cache_lock)(void);
  void (*cache_unlock)(void);
  uint8_t* (*scratch_claim)(uint32_t min_len);
  void (*scratch_release)(const uint8_t* bytes);
  void (*inflate_oom_log)(XpAppText allocation, uint32_t bytes);
} XpEpubLeaves;

#ifdef __cplusplus
}
#define XP_EPUB_ASSERT(condition, message) static_assert(condition, message)
#else
#define XP_EPUB_ASSERT(condition, message) _Static_assert(condition, message)
#endif
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
XP_EPUB_ASSERT(sizeof(XpEpubElement) == 12, "XpEpubElement size");
XP_EPUB_ASSERT(offsetof(XpEpubElement, words) == 8, "XpEpubElement.words");
XP_EPUB_ASSERT(offsetof(XpEpubElement, tag) == 10, "XpEpubElement.tag");
XP_EPUB_ASSERT(offsetof(XpEpubElement, valid) == 11, "XpEpubElement.valid");
XP_EPUB_ASSERT(sizeof(XpEpubWord) == sizeof(XpAppText) + 16, "XpEpubWord size");
XP_EPUB_ASSERT(offsetof(XpEpubWord, x) == sizeof(XpAppText), "XpEpubWord.x");
XP_EPUB_ASSERT(offsetof(XpEpubWord, source_start) == sizeof(XpAppText) + 4, "XpEpubWord.source_start");
XP_EPUB_ASSERT(offsetof(XpEpubWord, source_end) == sizeof(XpAppText) + 8, "XpEpubWord.source_end");
XP_EPUB_ASSERT(offsetof(XpEpubWord, style) == sizeof(XpAppText) + 12, "XpEpubWord.style");
XP_EPUB_ASSERT(offsetof(XpEpubWord, character_spacing) == sizeof(XpAppText) + 13, "XpEpubWord.character_spacing");
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
XP_EPUB_ASSERT(sizeof(XpEpubLeaves) == 192 * sizeof(void (*)(void)), "XpEpubLeaves size");
#define XP_EPUB_LEAF_OFFSET(field, index) \
  XP_EPUB_ASSERT(offsetof(XpEpubLeaves, field) == (index) * sizeof(void (*)(void)), "XpEpubLeaves." #field)
XP_EPUB_LEAF_OFFSET(resources_open, 0);
XP_EPUB_LEAF_OFFSET(resources_close, 1);
XP_EPUB_LEAF_OFFSET(render_services, 2);
XP_EPUB_LEAF_OFFSET(render_bind_page, 3);
XP_EPUB_LEAF_OFFSET(render_finish_page, 4);
XP_EPUB_LEAF_OFFSET(render_margins, 5);
XP_EPUB_LEAF_OFFSET(render_geometry, 6);
XP_EPUB_LEAF_OFFSET(render_restore_bw, 7);
XP_EPUB_LEAF_OFFSET(render_discard_bw, 8);
XP_EPUB_LEAF_OFFSET(render_orientation, 9);
XP_EPUB_LEAF_OFFSET(loan_begin, 10);
XP_EPUB_LEAF_OFFSET(loan_end, 11);
XP_EPUB_LEAF_OFFSET(loan_drop, 12);
XP_EPUB_LEAF_OFFSET(loan_count, 13);
XP_EPUB_LEAF_OFFSET(strip_release_idle, 14);
XP_EPUB_LEAF_OFFSET(book_facade_open, 15);
XP_EPUB_LEAF_OFFSET(book_facade_close, 16);
XP_EPUB_LEAF_OFFSET(book_set_extractor, 17);
XP_EPUB_LEAF_OFFSET(protection_open, 18);
XP_EPUB_LEAF_OFFSET(protection_close, 19);
XP_EPUB_LEAF_OFFSET(protection_encrypted, 20);
XP_EPUB_LEAF_OFFSET(protection_size, 21);
XP_EPUB_LEAF_OFFSET(protection_stream, 22);
XP_EPUB_LEAF_OFFSET(file_seek, 23);
XP_EPUB_LEAF_OFFSET(file_size, 24);
XP_EPUB_LEAF_OFFSET(path_remove_tree, 25);
XP_EPUB_LEAF_OFFSET(progress_to_reader, 26);
XP_EPUB_LEAF_OFFSET(progress_to_saved, 27);
XP_EPUB_LEAF_OFFSET(progress_save, 28);
XP_EPUB_LEAF_OFFSET(section_open, 29);
XP_EPUB_LEAF_OFFSET(section_close, 30);
XP_EPUB_LEAF_OFFSET(section_page_count, 31);
XP_EPUB_LEAF_OFFSET(section_estimated_pages, 32);
XP_EPUB_LEAF_OFFSET(section_building, 33);
XP_EPUB_LEAF_OFFSET(section_complete, 34);
XP_EPUB_LEAF_OFFSET(section_partial, 35);
XP_EPUB_LEAF_OFFSET(section_load_cache, 36);
XP_EPUB_LEAF_OFFSET(section_create_cache, 37);
XP_EPUB_LEAF_OFFSET(section_start_build, 38);
XP_EPUB_LEAF_OFFSET(section_build_more, 39);
XP_EPUB_LEAF_OFFSET(section_abandon_build, 40);
XP_EPUB_LEAF_OFFSET(section_clear_cache, 41);
XP_EPUB_LEAF_OFFSET(section_html_cached, 42);
XP_EPUB_LEAF_OFFSET(section_find_anchor, 43);
XP_EPUB_LEAF_OFFSET(section_cached_anchor, 44);
XP_EPUB_LEAF_OFFSET(section_toc_for_page, 45);
XP_EPUB_LEAF_OFFSET(section_page_for_offset, 46);
XP_EPUB_LEAF_OFFSET(section_offset_for_page, 47);
XP_EPUB_LEAF_OFFSET(section_paragraph_for_page, 48);
XP_EPUB_LEAF_OFFSET(section_reached_offset, 49);
XP_EPUB_LEAF_OFFSET(section_load_page, 50);
XP_EPUB_LEAF_OFFSET(section_page_text, 51);
XP_EPUB_LEAF_OFFSET(page_close, 52);
XP_EPUB_LEAF_OFFSET(page_info, 53);
XP_EPUB_LEAF_OFFSET(page_element, 54);
XP_EPUB_LEAF_OFFSET(page_word, 55);
XP_EPUB_LEAF_OFFSET(metadata_assign, 56);
XP_EPUB_LEAF_OFFSET(metadata_clear, 57);
XP_EPUB_LEAF_OFFSET(metadata_footnotes, 58);
XP_EPUB_LEAF_OFFSET(metadata_links, 59);
XP_EPUB_LEAF_OFFSET(metadata_footnote, 60);
XP_EPUB_LEAF_OFFSET(metadata_link, 61);
XP_EPUB_LEAF_OFFSET(paint_dither, 62);
XP_EPUB_LEAF_OFFSET(paint_pixel, 63);
XP_EPUB_LEAF_OFFSET(paint_status, 64);
XP_EPUB_LEAF_OFFSET(image_clear_failures, 65);
XP_EPUB_LEAF_OFFSET(image_release_cache, 66);
XP_EPUB_LEAF_OFFSET(fonts_ensure_loaded, 67);
XP_EPUB_LEAF_OFFSET(fonts_release_caches, 68);
XP_EPUB_LEAF_OFFSET(fonts_prewarm_begin, 69);
XP_EPUB_LEAF_OFFSET(fonts_prewarm_finish_scan, 70);
XP_EPUB_LEAF_OFFSET(fonts_prewarm_drop, 71);
XP_EPUB_LEAF_OFFSET(fonts_take_corrupt_notice, 72);
XP_EPUB_LEAF_OFFSET(font_sizes, 73);
XP_EPUB_LEAF_OFFSET(settings_snapshot, 74);
XP_EPUB_LEAF_OFFSET(settings_render_spec, 75);
XP_EPUB_LEAF_OFFSET(settings_orientation, 76);
XP_EPUB_LEAF_OFFSET(settings_font_size, 77);
XP_EPUB_LEAF_OFFSET(settings_line_spacing, 78);
XP_EPUB_LEAF_OFFSET(settings_alignment, 79);
XP_EPUB_LEAF_OFFSET(settings_focus_reading, 80);
XP_EPUB_LEAF_OFFSET(settings_inversion, 81);
XP_EPUB_LEAF_OFFSET(settings_ble_enabled, 82);
XP_EPUB_LEAF_OFFSET(settings_save, 83);
XP_EPUB_LEAF_OFFSET(input_services, 84);
XP_EPUB_LEAF_OFFSET(input_facts, 85);
XP_EPUB_LEAF_OFFSET(input_pressed, 86);
XP_EPUB_LEAF_OFFSET(input_screen_long, 87);
XP_EPUB_LEAF_OFFSET(input_screen_tap, 88);
XP_EPUB_LEAF_OFFSET(input_raw_contact, 89);
XP_EPUB_LEAF_OFFSET(render_busy, 90);
XP_EPUB_LEAF_OFFSET(file_open_read, 91);
XP_EPUB_LEAF_OFFSET(file_open_write, 92);
XP_EPUB_LEAF_OFFSET(file_read, 93);
XP_EPUB_LEAF_OFFSET(file_write, 94);
XP_EPUB_LEAF_OFFSET(file_close, 95);
XP_EPUB_LEAF_OFFSET(path_remove, 96);
XP_EPUB_LEAF_OFFSET(path_rename, 97);
XP_EPUB_LEAF_OFFSET(path_mkdir, 98);
XP_EPUB_LEAF_OFFSET(native_path_hash, 99);
XP_EPUB_LEAF_OFFSET(heap_snapshot, 100);
XP_EPUB_LEAF_OFFSET(radio_status, 101);
XP_EPUB_LEAF_OFFSET(radio_before_build, 102);
XP_EPUB_LEAF_OFFSET(radio_settle_start, 103);
XP_EPUB_LEAF_OFFSET(radio_holds_heap, 104);
XP_EPUB_LEAF_OFFSET(radio_after_paint, 105);
XP_EPUB_LEAF_OFFSET(radio_switch_off, 106);
XP_EPUB_LEAF_OFFSET(radio_request_connect, 107);
XP_EPUB_LEAF_OFFSET(trusted_now, 108);
XP_EPUB_LEAF_OFFSET(trusted_sync, 109);
XP_EPUB_LEAF_OFFSET(wifi_connected, 110);
XP_EPUB_LEAF_OFFSET(wifi_disconnect, 111);
XP_EPUB_LEAF_OFFSET(restart_reader, 112);
XP_EPUB_LEAF_OFFSET(child_menu, 113);
XP_EPUB_LEAF_OFFSET(child_chapter, 114);
XP_EPUB_LEAF_OFFSET(child_percent, 115);
XP_EPUB_LEAF_OFFSET(child_text_settings, 116);
XP_EPUB_LEAF_OFFSET(child_footnote, 117);
XP_EPUB_LEAF_OFFSET(child_dictionary, 118);
XP_EPUB_LEAF_OFFSET(child_selection, 119);
XP_EPUB_LEAF_OFFSET(child_bookmarks, 120);
XP_EPUB_LEAF_OFFSET(child_clippings, 121);
XP_EPUB_LEAF_OFFSET(child_qr, 122);
XP_EPUB_LEAF_OFFSET(child_wifi, 123);
XP_EPUB_LEAF_OFFSET(child_sync, 124);
XP_EPUB_LEAF_OFFSET(toolbar_open, 125);
XP_EPUB_LEAF_OFFSET(toolbar_close, 126);
XP_EPUB_LEAF_OFFSET(toolbar_begin, 127);
XP_EPUB_LEAF_OFFSET(toolbar_paint, 128);
XP_EPUB_LEAF_OFFSET(toolbar_route, 129);
XP_EPUB_LEAF_OFFSET(toolbar_scroll, 130);
XP_EPUB_LEAF_OFFSET(toolbar_visible_rows, 131);
XP_EPUB_LEAF_OFFSET(popup_open, 132);
XP_EPUB_LEAF_OFFSET(popup_close, 133);
XP_EPUB_LEAF_OFFSET(popup_active, 134);
XP_EPUB_LEAF_OFFSET(popup_show, 135);
XP_EPUB_LEAF_OFFSET(popup_dismiss, 136);
XP_EPUB_LEAF_OFFSET(popup_input, 137);
XP_EPUB_LEAF_OFFSET(popup_paint, 138);
XP_EPUB_LEAF_OFFSET(popup_process_render, 139);
XP_EPUB_LEAF_OFFSET(end_open, 140);
XP_EPUB_LEAF_OFFSET(end_close, 141);
XP_EPUB_LEAF_OFFSET(end_load, 142);
XP_EPUB_LEAF_OFFSET(end_menu_active, 143);
XP_EPUB_LEAF_OFFSET(end_input, 144);
XP_EPUB_LEAF_OFFSET(end_paint, 145);
XP_EPUB_LEAF_OFFSET(bookmarks_open, 146);
XP_EPUB_LEAF_OFFSET(bookmarks_close, 147);
XP_EPUB_LEAF_OFFSET(bookmarks_clear, 148);
XP_EPUB_LEAF_OFFSET(bookmarks_load, 149);
XP_EPUB_LEAF_OFFSET(bookmarks_save, 150);
XP_EPUB_LEAF_OFFSET(bookmarks_count, 151);
XP_EPUB_LEAF_OFFSET(bookmarks_row, 152);
XP_EPUB_LEAF_OFFSET(bookmarks_retain, 153);
XP_EPUB_LEAF_OFFSET(bookmarks_insert_front, 154);
XP_EPUB_LEAF_OFFSET(clippings_load, 155);
XP_EPUB_LEAF_OFFSET(clippings_unload, 156);
XP_EPUB_LEAF_OFFSET(clippings_count, 157);
XP_EPUB_LEAF_OFFSET(clippings_row, 158);
XP_EPUB_LEAF_OFFSET(clippings_remove, 159);
XP_EPUB_LEAF_OFFSET(clippings_insert, 160);
XP_EPUB_LEAF_OFFSET(clippings_export, 161);
XP_EPUB_LEAF_OFFSET(clippings_move_book, 162);
XP_EPUB_LEAF_OFFSET(recents_remove, 163);
XP_EPUB_LEAF_OFFSET(recents_add, 164);
XP_EPUB_LEAF_OFFSET(recents_update_path, 165);
XP_EPUB_LEAF_OFFSET(state_open_path, 166);
XP_EPUB_LEAF_OFFSET(state_set_open_path, 167);
XP_EPUB_LEAF_OFFSET(state_reset_reader_count, 168);
XP_EPUB_LEAF_OFFSET(state_save, 169);
XP_EPUB_LEAF_OFFSET(has_sync_credentials, 170);
XP_EPUB_LEAF_OFFSET(has_exit_subscriber, 171);
XP_EPUB_LEAF_OFFSET(has_session_subscriber, 172);
XP_EPUB_LEAF_OFFSET(document_id, 173);
XP_EPUB_LEAF_OFFSET(emit_open, 174);
XP_EPUB_LEAF_OFFSET(emit_exit, 175);
XP_EPUB_LEAF_OFFSET(emit_session, 176);
XP_EPUB_LEAF_OFFSET(log, 177);
XP_EPUB_LEAF_OFFSET(page_word_font, 178);
XP_EPUB_LEAF_OFFSET(page_line_font, 179);
XP_EPUB_LEAF_OFFSET(page_word_y_offset, 180);
XP_EPUB_LEAF_OFFSET(page_line_ruby_shift, 181);
XP_EPUB_LEAF_OFFSET(font_text_advance, 182);
XP_EPUB_LEAF_OFFSET(log_enabled, 183);
XP_EPUB_LEAF_OFFSET(toolbar_reset_navigation, 184);
XP_EPUB_LEAF_OFFSET(input_snapshot, 185);
XP_EPUB_LEAF_OFFSET(page_render, 186);
XP_EPUB_LEAF_OFFSET(cache_lock, 187);
XP_EPUB_LEAF_OFFSET(cache_unlock, 188);
XP_EPUB_LEAF_OFFSET(scratch_claim, 189);
XP_EPUB_LEAF_OFFSET(scratch_release, 190);
XP_EPUB_LEAF_OFFSET(inflate_oom_log, 191);
#undef XP_EPUB_LEAF_OFFSET
XP_EPUB_ASSERT(sizeof(XpEpubBookBorrow) == 6 * sizeof(void (*)(void)), "XpEpubBookBorrow size");
XP_EPUB_ASSERT(offsetof(XpEpubBookBorrow, spine) == 0, "XpEpubBookBorrow.spine");
XP_EPUB_ASSERT(offsetof(XpEpubBookBorrow, item_size) == 5 * sizeof(void (*)(void)), "XpEpubBookBorrow.item_size");
XP_EPUB_ASSERT(sizeof(XpEpubBookView) == (sizeof(void*) == 8 ? 40 : 24), "XpEpubBookView size");
XP_EPUB_ASSERT(offsetof(XpEpubSpine, toc_index) == sizeof(XpAppText) + 4, "XpEpubSpine.toc_index");
#undef XP_EPUB_ASSERT

#ifndef __cplusplus
_Static_assert(_Generic(((XpEpubLeaves*)0)->page_element, void (*)(void*, uint16_t, XpEpubElement*): 1, default: 0),
               "XpEpubLeaves.page_element signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->page_word, void (*)(void*, uint16_t, uint16_t, XpEpubWord*): 1, default: 0),
               "XpEpubLeaves.page_word signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->end_input,
                   void (*)(void*, void (*)(void*, uint8_t, XpAppText), void*): 1,
                   default: 0),
               "XpEpubLeaves.end_input signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->recents_add, void (*)(const XpAppBook*): 1, default: 0),
               "XpEpubLeaves.recents_add signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->page_word_font,
                   int32_t (*)(void*, uint16_t, uint16_t, int32_t): 1,
                   default: 0),
               "XpEpubLeaves.page_word_font signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->page_line_font, int32_t (*)(void*, uint16_t, int32_t): 1, default: 0),
               "XpEpubLeaves.page_line_font signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->page_word_y_offset,
                   int32_t (*)(void*, uint16_t, uint16_t, int32_t): 1,
                   default: 0),
               "XpEpubLeaves.page_word_y_offset signature");
_Static_assert(_Generic(((XpEpubLeaves*)0)->page_line_ruby_shift, int32_t (*)(void*, uint16_t, int32_t): 1, default: 0),
               "XpEpubLeaves.page_line_ruby_shift signature");
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
_Static_assert(_Generic(((XpEpubLeaves*)0)->page_render, void (*)(void*, int32_t, int32_t, int32_t): 1, default: 0),
               "XpEpubLeaves.page_render signature");
_Static_assert(_Generic(((XpEpubGeometry*)0)->has_font_cache, uint8_t: 1, default: 0),
               "XpEpubGeometry.has_font_cache type");
_Static_assert(_Alignof(XpEpubGeometry) == 4, "XpEpubGeometry alignment");
#endif

#ifndef __cplusplus
_Static_assert(_Generic(((XpEpubLeaves*)0)->radio_status, void (*)(XpEpubRadio*, XpEpubPeer*): 1, default: 0),
               "XpEpubLeaves.radio_status signature");
#endif
