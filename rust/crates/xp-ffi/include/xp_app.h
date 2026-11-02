#pragma once
#include <stddef.h>
#include <stdint.h>

#include "xp_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// All views and sinks are synchronous. Only accepted activity pointers transfer ownership.
// Tables are immutable for the application lifetime. No callback retains a borrowed view.
typedef struct {
  const uint8_t* data;
  size_t len;
} XpAppText;
typedef uint8_t (*XpAppTextSink)(void* context, XpAppText text);
typedef struct {
  int32_t x, y;
} XpAppPoint;
typedef struct {
  XpAppText path, title, author, cover;
} XpAppBook;
typedef uint8_t (*XpAppBookSink)(void* context, const XpAppBook* book);

typedef uint8_t XpAppStatus;
enum { XP_APP_OK, XP_APP_INVALID, XP_APP_WRONG_CONTEXT = 3 };
typedef uint8_t XpAppEventResult;
enum { XP_APP_ACCEPTED, XP_APP_REJECTED, XP_APP_EVENT_OUT_OF_MEMORY, XP_APP_EVENT_INVALID };
typedef uint8_t XpAppLaunchKind;
enum {
  XP_APP_FILES,
  XP_APP_LIBRARY,
  XP_APP_OPDS,
  XP_APP_OPDS_PICKER,
  XP_APP_PLUGINS,
  XP_APP_FILE_TRANSFER,
  XP_APP_USB_DRIVE,
  XP_APP_SETTINGS,
  XP_APP_READER,
  XP_APP_BITMAP,
  XP_APP_BOOT,
  XP_APP_SLEEP,
  XP_APP_MESSAGE,
  XP_APP_CRASH,
  XP_APP_FRONTLIGHT_PANEL,
  XP_APP_NETWORK_MODE
};
typedef struct {
  XpAppText path;
  uint8_t fast_initial;
} XpAppReaderLaunch;
typedef struct {
  XpAppText text;
  uint8_t style;
} XpAppMessageLaunch;
typedef struct {
  XpAppLaunchKind kind;
  union {
    XpAppText file_path;
    XpAppText bitmap_path;
    XpAppReaderLaunch reader;
    XpAppMessageLaunch message;
    uint32_t opds_server;
    uint8_t settings_section;
    uint8_t plugins_show_opds;
    uint8_t transfer_join_network;
    uint8_t sleep_from_timeout;
  } value;
} XpAppLaunch;
typedef struct {
  uint8_t menu, clean_initial;
} XpAppHomeRequest;
typedef struct {
  uint8_t forward, chapter;
} XpAppRemoteTurn;
typedef struct {
  uint32_t allow_sleep_at;
  uint8_t wake_release_pending;
} XpAppBootInput;
typedef uint8_t XpAppEventKind;
enum {
  XP_APP_ADOPT_REPLACE,
  XP_APP_ADOPT_PUSH,
  XP_APP_POP,
  XP_APP_HOME_REQUESTED,
  XP_APP_NAVIGATE,
  XP_APP_RENDER_REQUESTED,
  XP_APP_SLEEP_REQUESTED,
  XP_APP_PREPARE_SLEEP,
  XP_APP_REMOTE_CLICK,
  XP_APP_REMOTE_TURN,
  XP_APP_REMOTE_SHORTCUT,
  XP_APP_FORCED_REFRESH,
  XP_APP_INPUT_POLLED,
  XP_APP_RESET_HOME_INPUT,
  XP_APP_BOOT_INPUT_STATE
};
typedef struct {
  XpAppEventKind kind;
  union {
    void* adopt_replace;
    void* adopt_push;
    XpAppHomeRequest home;
    XpAppLaunch navigate;
    uint8_t render_immediate;
    uint8_t sleep_from_timeout;
    uint8_t remote_button;
    XpAppRemoteTurn remote_turn;
    uint8_t reader_menu;
    uint8_t defer_home;
    XpAppBootInput boot_input;
  } value;
} XpAppEvent;

// Borrowed application result. Valid only during synchronous delivery.
typedef struct {
  XpAppText bytes;
  const void* native_string;
} XpAppNativeText;

typedef struct {
  int32_t action;
  uint8_t orientation;
  uint8_t page_turn_option;
} XpAppMenuResult;

typedef struct {
  int32_t spine;
  XpAppText anchor;
} XpAppChapterResult;

typedef struct {
  int32_t spine;
  int32_t page;
  int32_t pages;
  XpAppText xpath;
  float percentage;
  uint32_t offset;
  uint8_t has_saved_progress;
  uint8_t has_offset;
} XpAppProgressResult;

typedef struct {
  XpAppNativeText text;
  uint16_t start_page_offset;
  uint16_t end_page_offset;
  uint16_t start_word;
  uint16_t end_word;
  uint16_t word_count;
  uint32_t start_offset;
  uint32_t end_offset;
  uint8_t action;
} XpAppClipResult;

typedef struct {
  XpAppText ssid;
  XpAppText ip;
  uint8_t connected;
} XpAppWifiResult;

typedef union {
  XpAppMenuResult menu;
  XpAppChapterResult chapter;
  XpAppProgressResult progress;
  XpAppClipResult clipping;
  XpAppWifiResult wifi;
  XpAppText text;
  int32_t signed_value;
  uint32_t unsigned_value;
} XpAppResultPayload;

// kind: 0 empty, 1 Wi-Fi, 2 keyboard, 3 menu, 4 chapter, 5 percent,
// 6 interval, 7 page, 8 clipping, 9 progress, 10 network mode, 11 footnote, 12 file path.
typedef struct XpAppResultView {
  const void* original;
  XpAppResultPayload value;
  uint8_t kind;
  uint8_t cancelled;
} XpAppResultView;

// The native result slot is borrowed only during this synchronous continuation.
// It must run exactly once; it may destroy the child and enqueue navigation.
typedef void (*XpAppResultSink)(void* context, const XpAppResultView* result);
typedef struct {
  uint8_t reader_type;
  char title[64];
  int32_t spine, current_page, total_pages, progress_percent;
} XpAppScreenshot;
typedef void (*XpAppScreenshotSink)(void* context, const XpAppScreenshot* info);
typedef struct {
  uint32_t visit, painted_visit;
  uint8_t current_kind, pending, reader_in_stack, foreground_reader, remote_context;
  uint8_t foreground_home_shown, prevent_sleep, exclusive_storage;
} XpAppFacts;
typedef struct {
  uint8_t kind, reader, remote_input, prevent_sleep, exclusive_storage, skip_delay, input_over_page;
} XpAppLegacyFacts;

// The token borrows the existing C++ RenderLock and power scope for one render call.
typedef struct {
  void* lock;
} XpAppRenderToken;
typedef struct {
  void* (*create)(const XpAppLaunch* launch);
  void (*destroy)(void* activity);
  void (*enter)(void* activity);
  void (*loop)(void* activity);
  void (*exit)(void* activity);
  void (*render)(void* activity, XpAppRenderToken* token);
  XpAppLegacyFacts (*facts)(const void* activity, uint8_t delay, uint8_t input);
  void (*with_result)(void* activity, XpAppResultSink sink, void* context);
  void (*deliver_result)(void* parent, const void* result);
  void (*prepare_sleep)(void* activity);
  uint8_t (*home_gesture)(void* activity);
  uint8_t (*forced_refresh)(void* activity);
  uint8_t (*reader_turn)(void* activity, uint8_t forward, uint8_t chapter);
  void (*screenshot_info)(const void* activity, XpAppScreenshotSink sink, void* context);
} XpAppLegacy;

// Manager compatibility retires with the last native screen; mapping stays native.
typedef struct {
  XpAppFacts (*activity_facts)(void);
  uint8_t (*home_action)(void);
  void (*screenshot_info)(XpAppScreenshotSink sink, void* context);
} XpAppCompatibility;

// Snapshot flags select driver, Home keys, or activity fields; other fields are zero.
typedef struct {
  XpUiInput input;
  uint32_t power_held_ms;
  uint8_t power_pressed, power_released, power_held, physical_down;
  uint8_t any_pressed, any_released, touch_activity;
  uint8_t home_key_pressed, home_key_tapped, home_key_long_pressed, has_home_key;
  uint8_t has_switch, switch_on, usb_changed;
  uint8_t home_gesture, light_panel_gesture, status_bar_tap;
  uint8_t suppressed_release, cpu_hold;
} XpAppMappedSnapshot;
typedef struct {
  uint32_t power_duration, sleep_timeout, idle_power_saving;
  uint8_t home_tap, home_double, home_long, short_power, double_power_light;
  uint8_t slide_action, tilt, reader_orientation, fading_fix;
  uint8_t screen_inverted, touch_reader, frontlight_on, is_x4pro, is_papermono;
} XpAppSettings;
typedef struct {
  int32_t width, height, orientation;
  uint8_t frame_available;
} XpAppDisplayGeometry;
typedef struct {
  uint32_t visit;
  uint8_t where, page_shown, storage_busy, wifi_on;
} XpAppBleScene;
typedef struct {
  uint8_t plugins, opds, library_metadata, grid;
} XpAppHomeSettings;
typedef struct {
  XpUiRect screen, content, safe;
  int32_t title_font, small_font, tiny_font, body_font;
  int16_t top_padding, header_height, home_top, tile_height, cover_height;
  int16_t side_padding, spacing, menu_top, menu_height, menu_gap, hints_height;
  int16_t scroll_width, scroll_offset, header_side, battery_height, corner;
  int16_t tab_height, tab_height_pct, tab_width_pct, tab_offset;
  uint8_t theme, continue_in_menu, columns, touch, body_bold;
} XpAppTheme;
typedef struct {
  int32_t width, height;
  uint8_t valid;
} XpAppBitmapInfo;
typedef struct {
  int32_t font, width;
  uint8_t lines, style;
} XpAppTextLayout;
typedef uint8_t XpAppMemoryDomain;
enum { XP_APP_MEMORY_DEFAULT, XP_APP_MEMORY_PSRAM_ONLY };
typedef uint8_t XpAppBookKind;
enum { XP_APP_BOOK_REFLOWABLE, XP_APP_BOOK_XTC };

typedef struct {
  XpAppHomeSettings (*settings_snapshot)(void);
  uint8_t (*recents_visit)(XpAppBookSink sink, void* context);
  uint8_t (*recent_missing)(XpAppText path);
  void (*recent_update)(const XpAppBook* book);
  uint32_t (*opds_count)(void);
  int32_t (*book_progress)(XpAppText path);
  void* (*library_open)(void);
  uint32_t (*library_count)(void* library);
  uint8_t (*library_row)(void* library, uint32_t row, uint8_t metadata, XpAppBookSink sink, void* context);
  void (*library_close)(void* library);
  uint8_t (*library_rebuild)(uint8_t metadata);
  void* (*book_open)(XpAppBookKind kind, XpAppText path);
  uint8_t (*book_thumb_path)(void* book, int32_t height, XpAppTextSink sink, void* context);
  uint8_t (*book_load)(void* book, uint8_t build_missing, uint8_t skip_css);
  uint8_t (*book_generate_thumb)(void* book, int32_t height);
  uint8_t (*book_generate_thumb_from_source)(void* book, int32_t height);
  void (*book_close)(void* book);
  uint8_t (*path_exists)(XpAppText path);
  uint8_t (*thumbnail_path)(XpAppText base, int32_t height, XpAppTextSink sink, void* context);
  void* (*bitmap_open)(XpAppText path);
  XpAppBitmapInfo (*bitmap_info)(void* bitmap);
  uint8_t (*bitmap_draw)(void* bitmap, XpUiRect rect, int32_t shift);
  void (*bitmap_close)(void* bitmap);
  void (*theme_snapshot)(XpAppTheme* theme);
  XpAppText (*translate)(uint16_t string_id);
  void (*header_draw)(XpUiRect rect, XpAppText title);
  void (*footer_draw)(const XpAppText labels[4]);
  void (*placeholder_draw)(XpUiRect rect);
  XpUiRect (*popup_draw)(XpAppText text);
  void (*popup_progress)(XpUiRect rect, int32_t percent);
  void (*icon_draw)(uint8_t icon, XpUiRect rect);
  void (*text_draw)(const XpUiCommand* command, XpUiRect clip, XpAppText text);
  int32_t (*text_width)(XpAppText text, int32_t font, uint8_t style);
  int32_t (*line_height)(int32_t font);
  uint8_t (*text_wrap)(XpAppText text, XpAppTextLayout layout, XpAppTextSink sink, void* context);
  uint8_t (*text_truncate)(XpAppText text, XpAppTextLayout layout, XpAppTextSink sink, void* context);
  int32_t (*text_box_height)(XpAppText text, XpAppTextLayout layout);
  size_t (*region_size)(XpUiRect rect);
  uint8_t (*region_read)(XpUiRect rect, uint8_t* data, size_t capacity);
  uint8_t (*region_write)(XpUiRect rect, const uint8_t* data, size_t capacity);
  void (*rounded_mask)(XpUiRect rect, uint8_t radius, uint8_t paint);
  void (*polygon_draw)(const XpAppPoint* points, uint8_t count, uint8_t paint);
  uint8_t (*paint_scene)(const XpUiScene* scene, const XpUiPaint* resources);
  void (*refresh)(uint8_t refresh);
  uint8_t* (*buffer_allocate)(XpAppMemoryDomain domain, size_t bytes);
  void (*buffer_release)(XpAppMemoryDomain domain, uint8_t* data);
} XpAppResources;

typedef struct {
  void (*input_mapped_snapshot)(uint8_t poll, uint8_t full, uint8_t consume_release, XpAppMappedSnapshot* frame);
  void (*input_shared_power_mode)(uint8_t enabled);
  void (*input_power_confirm)(uint8_t enabled);
  uint8_t (*input_remote_click)(uint8_t button);
  void (*loop_settings)(XpAppSettings* settings);
  XpAppDisplayGeometry (*display_geometry)(void);
  void (*header_back_clear)(void);
  uint32_t (*now_ms)(void);
  void (*delay_ms)(uint32_t ms);
  void (*idle_wait)(uint32_t max_ms, uint32_t slice_ms);
  void (*yield_task)(void);
  void (*power_saving)(uint8_t enabled);
  uint8_t (*tilt_update)(uint8_t enabled, uint8_t orientation, uint8_t reader);
  uint8_t (*battery_capacity_step)(void);
  void (*fading_fix)(uint8_t enabled);
  void (*memory_diagnostic)(void);
  void (*console_service)(void);
  void (*screenshot)(void);
  void (*touch_feedback)(uint8_t long_press);
  uint8_t (*frontlight_present)(void);
  uint8_t (*frontlight_state)(void);
  void (*frontlight_set)(uint8_t enabled);
  void (*settings_save)(const XpAppSettings* settings);
  uint8_t (*ble_tick)(const XpAppBleScene* scene);
  uint8_t (*ble_before_screen_change)(uint8_t reader);
  void (*sleep_enter)(uint8_t from_timeout);
  void (*diagnostic)(uint8_t event, uint32_t first, uint32_t second);
  void* (*render_lock)(void);
  void* (*render_try_lock)(void);
  void (*render_unlock)(void* lock);
  void (*model_lock)(void);
  void (*model_unlock)(void);
  void (*notify_render)(void);
} XpAppRuntime;
struct XpEpubLeaves;
typedef struct {
  const XpAppLegacy* legacy;
  const XpAppResources* resources;
  const XpAppRuntime* runtime;
  const struct XpEpubLeaves* epub;
} XpAppPlatform;

XpAppStatus xp_app_init(const XpAppPlatform* platform, XpAppCompatibility* compatibility);
void xp_app_loop(void);
void xp_app_render(XpAppRenderToken* token);
XpAppEventResult xp_app_event(const XpAppEvent* event);

#ifdef __cplusplus
}
static_assert(sizeof(XpAppText) == 2 * sizeof(void*));
static_assert(offsetof(XpAppText, len) == sizeof(void*));
static_assert(sizeof(XpAppPoint) == 8);
static_assert(sizeof(XpAppBook) == 8 * sizeof(void*));
static_assert(sizeof(XpAppLegacy) == 14 * sizeof(void*));
static_assert(sizeof(XpAppCompatibility) == 3 * sizeof(void*));
static_assert(sizeof(XpAppResources) == 46 * sizeof(void*));
static_assert(sizeof(XpAppRuntime) == 33 * sizeof(void*));
static_assert(sizeof(XpAppPlatform) == 4 * sizeof(void*));
static_assert(offsetof(XpAppEvent, kind) == 0);
static_assert(sizeof(XpAppSettings) == 28);
static_assert(sizeof(XpAppDisplayGeometry) == 16);
static_assert(sizeof(XpAppBleScene) == 8);
static_assert(sizeof(XpAppHomeSettings) == 4);
static_assert(sizeof(XpAppBitmapInfo) == 12);
static_assert(sizeof(XpAppTextLayout) == 12);
#endif

#ifdef __cplusplus
#define XP_APP_RESULT_ASSERT(condition, message) static_assert(condition, message)
#else
#define XP_APP_RESULT_ASSERT(condition, message) _Static_assert(condition, message)
#endif
XP_APP_RESULT_ASSERT(sizeof(XpAppNativeText) == 3 * sizeof(void*), "XpAppNativeText size");
XP_APP_RESULT_ASSERT(offsetof(XpAppNativeText, native_string) == 2 * sizeof(void*), "XpAppNativeText.native_string");
XP_APP_RESULT_ASSERT(sizeof(XpAppMenuResult) == 8, "XpAppMenuResult size");
XP_APP_RESULT_ASSERT(offsetof(XpAppMenuResult, action) == 0, "XpAppMenuResult.action");
XP_APP_RESULT_ASSERT(offsetof(XpAppMenuResult, orientation) == 4, "XpAppMenuResult.orientation");
XP_APP_RESULT_ASSERT(offsetof(XpAppMenuResult, page_turn_option) == 5, "XpAppMenuResult.page_turn_option");
XP_APP_RESULT_ASSERT(sizeof(XpAppChapterResult) == 3 * sizeof(void*), "XpAppChapterResult size");
XP_APP_RESULT_ASSERT(offsetof(XpAppChapterResult, anchor) == sizeof(void*), "XpAppChapterResult.anchor");
XP_APP_RESULT_ASSERT(sizeof(XpAppProgressResult) == 16 + 4 * sizeof(void*), "XpAppProgressResult size");
XP_APP_RESULT_ASSERT(offsetof(XpAppProgressResult, xpath) == 8 + sizeof(void*), "XpAppProgressResult.xpath");
XP_APP_RESULT_ASSERT(offsetof(XpAppProgressResult, percentage) == 8 + 3 * sizeof(void*),
                     "XpAppProgressResult.percentage");
XP_APP_RESULT_ASSERT(offsetof(XpAppProgressResult, offset) == 12 + 3 * sizeof(void*), "XpAppProgressResult.offset");
XP_APP_RESULT_ASSERT(offsetof(XpAppProgressResult, has_saved_progress) == 16 + 3 * sizeof(void*),
                     "XpAppProgressResult.has_saved_progress");
XP_APP_RESULT_ASSERT(offsetof(XpAppProgressResult, has_offset) == 17 + 3 * sizeof(void*),
                     "XpAppProgressResult.has_offset");
XP_APP_RESULT_ASSERT(sizeof(XpAppClipResult) == 24 + 3 * sizeof(void*), "XpAppClipResult size");
XP_APP_RESULT_ASSERT(offsetof(XpAppClipResult, start_page_offset) == 3 * sizeof(void*),
                     "XpAppClipResult.start_page_offset");
XP_APP_RESULT_ASSERT(offsetof(XpAppClipResult, start_offset) == 12 + 3 * sizeof(void*), "XpAppClipResult.start_offset");
XP_APP_RESULT_ASSERT(offsetof(XpAppClipResult, end_offset) == 16 + 3 * sizeof(void*), "XpAppClipResult.end_offset");
XP_APP_RESULT_ASSERT(offsetof(XpAppClipResult, action) == 20 + 3 * sizeof(void*), "XpAppClipResult.action");
XP_APP_RESULT_ASSERT(sizeof(XpAppWifiResult) == 5 * sizeof(void*), "XpAppWifiResult size");
XP_APP_RESULT_ASSERT(offsetof(XpAppWifiResult, connected) == 4 * sizeof(void*), "XpAppWifiResult.connected");
XP_APP_RESULT_ASSERT(sizeof(XpAppResultPayload) == sizeof(XpAppClipResult), "XpAppResultPayload size");
XP_APP_RESULT_ASSERT(sizeof(XpAppResultView) == 24 + 5 * sizeof(void*), "XpAppResultView size");
XP_APP_RESULT_ASSERT(offsetof(XpAppResultView, original) == 0, "XpAppResultView.original");
XP_APP_RESULT_ASSERT(offsetof(XpAppResultView, value) == sizeof(void*), "XpAppResultView.value");
XP_APP_RESULT_ASSERT(offsetof(XpAppResultView, kind) == sizeof(void*) + sizeof(XpAppResultPayload),
                     "XpAppResultView.kind");
XP_APP_RESULT_ASSERT(offsetof(XpAppResultView, cancelled) == 1 + sizeof(void*) + sizeof(XpAppResultPayload),
                     "XpAppResultView.cancelled");
XP_APP_RESULT_ASSERT(sizeof(XpAppPlatform) == 4 * sizeof(void*), "XpAppPlatform size");
XP_APP_RESULT_ASSERT(offsetof(XpAppPlatform, epub) == 3 * sizeof(void*), "XpAppPlatform.epub");
#undef XP_APP_RESULT_ASSERT
