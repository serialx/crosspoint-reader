#pragma once
#include <xp_app.h>

extern "C" {
void* xp_app_test_current_legacy();
uint8_t xp_app_test_is_home();
size_t xp_app_test_parent_count();
uint8_t xp_app_test_pending_kind();
}

inline XpAppLegacy appTestLegacy() {
  return {
      .create = [](const XpAppLaunch* launch) -> void* { return {}; },
      .destroy = [](void* activity) -> void {},
      .enter = [](void* activity) -> void {},
      .loop = [](void* activity) -> void {},
      .exit = [](void* activity) -> void {},
      .render = [](void* activity, XpAppRenderToken* token) -> void {},
      .facts = [](const void* activity, uint8_t delay, uint8_t input) -> XpAppLegacyFacts { return {}; },
      .with_result = [](void* activity, XpAppResultSink sink, void* context) { sink(context, nullptr); },
      .deliver_result = [](void* parent, const void* result) -> void {},
      .prepare_sleep = [](void* activity) -> void {},
      .home_gesture = [](void* activity) -> uint8_t { return {}; },
      .forced_refresh = [](void* activity) -> uint8_t { return {}; },
      .reader_turn = [](void* activity, uint8_t forward, uint8_t chapter) -> uint8_t { return {}; },
      .screenshot_info = [](const void* activity, XpAppScreenshotSink sink, void* context) -> void {},
  };
}

inline XpAppResources appTestResources() {
  return {
      .settings_snapshot = [](void) -> XpAppHomeSettings { return {}; },
      .recents_visit = [](XpAppBookSink sink, void* context) -> uint8_t { return {}; },
      .recent_missing = [](XpAppText path) -> uint8_t { return {}; },
      .recent_update = [](const XpAppBook* book) -> void {},
      .opds_count = [](void) -> uint32_t { return {}; },
      .book_progress = [](XpAppText path) -> int32_t { return {}; },
      .library_open = [](void) -> void* { return {}; },
      .library_count = [](void* library) -> uint32_t { return {}; },
      .library_row = [](void* library, uint32_t row, uint8_t metadata, XpAppBookSink sink, void* context) -> uint8_t {
        return {};
      },
      .library_close = [](void* library) -> void {},
      .library_rebuild = [](uint8_t metadata) -> uint8_t { return {}; },
      .book_open = [](XpAppBookKind kind, XpAppText path) -> void* { return {}; },
      .book_thumb_path = [](void* book, int32_t height, XpAppTextSink sink, void* context) -> uint8_t { return {}; },
      .book_load = [](void* book, uint8_t build_missing, uint8_t skip_css) -> uint8_t { return {}; },
      .book_generate_thumb = [](void* book, int32_t height) -> uint8_t { return {}; },
      .book_generate_thumb_from_source = [](void* book, int32_t height) -> uint8_t { return {}; },
      .book_close = [](void* book) -> void {},
      .path_exists = [](XpAppText path) -> uint8_t { return {}; },
      .thumbnail_path = [](XpAppText base, int32_t height, XpAppTextSink sink, void* context) -> uint8_t { return {}; },
      .bitmap_open = [](XpAppText path) -> void* { return {}; },
      .bitmap_info = [](void* bitmap) -> XpAppBitmapInfo { return {}; },
      .bitmap_draw = [](void* bitmap, XpUiRect rect, int32_t shift) -> uint8_t { return {}; },
      .bitmap_close = [](void* bitmap) -> void {},
      .theme_snapshot = [](XpAppTheme* theme) -> void { *theme = {}; },
      .translate = [](uint16_t string_id) -> XpAppText { return {}; },
      .header_draw = [](XpUiRect rect, XpAppText title) -> void {},
      .footer_draw = [](const XpAppText labels[4]) -> void {},
      .placeholder_draw = [](XpUiRect rect) -> void {},
      .popup_draw = [](XpAppText text) -> XpUiRect { return {}; },
      .popup_progress = [](XpUiRect rect, int32_t percent) -> void {},
      .icon_draw = [](uint8_t icon, XpUiRect rect) -> void {},
      .text_draw = [](const XpUiCommand* command, XpUiRect clip, XpAppText text) -> void {},
      .text_width = [](XpAppText text, int32_t font, uint8_t style) -> int32_t { return {}; },
      .line_height = [](int32_t font) -> int32_t { return {}; },
      .text_wrap = [](XpAppText text, XpAppTextLayout layout, XpAppTextSink sink, void* context) -> uint8_t {
        return {};
      },
      .text_truncate = [](XpAppText text, XpAppTextLayout layout, XpAppTextSink sink, void* context) -> uint8_t {
        return {};
      },
      .text_box_height = [](XpAppText text, XpAppTextLayout layout) -> int32_t { return {}; },
      .region_size = [](XpUiRect rect) -> size_t { return {}; },
      .region_read = [](XpUiRect rect, uint8_t* data, size_t capacity) -> uint8_t { return {}; },
      .region_write = [](XpUiRect rect, const uint8_t* data, size_t capacity) -> uint8_t { return {}; },
      .rounded_mask = [](XpUiRect rect, uint8_t radius, uint8_t paint) -> void {},
      .polygon_draw = [](const XpAppPoint* points, uint8_t count, uint8_t paint) -> void {},
      .paint_scene = [](const XpUiScene* scene, const XpUiPaint* resources) -> uint8_t { return {}; },
      .refresh = [](uint8_t refresh) -> void {},
      .buffer_allocate = [](XpAppMemoryDomain domain, size_t bytes) -> uint8_t* { return {}; },
      .buffer_release = [](XpAppMemoryDomain domain, uint8_t* data) -> void {},
  };
}

inline XpAppRuntime appTestRuntime() {
  return {
      .input_mapped_snapshot = [](uint8_t poll, uint8_t full, uint8_t consume_release,
                                  XpAppMappedSnapshot* frame) -> void { *frame = {}; },
      .input_shared_power_mode = [](uint8_t enabled) -> void {},
      .input_power_confirm = [](uint8_t enabled) -> void {},
      .input_remote_click = [](uint8_t button) -> uint8_t { return {}; },
      .loop_settings = [](XpAppSettings* settings) -> void { *settings = {}; },
      .display_geometry = [](void) -> XpAppDisplayGeometry { return {}; },
      .header_back_clear = [](void) -> void {},
      .now_ms = [](void) -> uint32_t { return {}; },
      .delay_ms = [](uint32_t ms) -> void {},
      .idle_wait = [](uint32_t max_ms, uint32_t slice_ms) -> void {},
      .yield_task = [](void) -> void {},
      .power_saving = [](uint8_t enabled) -> void {},
      .tilt_update = [](uint8_t enabled, uint8_t orientation, uint8_t reader) -> uint8_t { return {}; },
      .battery_capacity_step = [](void) -> uint8_t { return {}; },
      .fading_fix = [](uint8_t enabled) -> void {},
      .memory_diagnostic = [](void) -> void {},
      .console_service = [](void) -> void {},
      .screenshot = [](void) -> void {},
      .touch_feedback = [](uint8_t long_press) -> void {},
      .frontlight_present = [](void) -> uint8_t { return {}; },
      .frontlight_state = [](void) -> uint8_t { return {}; },
      .frontlight_set = [](uint8_t enabled) -> void {},
      .settings_save = [](const XpAppSettings* settings) -> void {},
      .ble_tick = [](const XpAppBleScene* scene) -> uint8_t { return {}; },
      .ble_before_screen_change = [](uint8_t reader) -> uint8_t { return {}; },
      .sleep_enter = [](uint8_t from_timeout) -> void {},
      .diagnostic = [](uint8_t event, uint32_t first, uint32_t second) -> void {},
      .render_lock = [](void) -> void* { return {}; },
      .render_try_lock = [](void) -> void* { return {}; },
      .render_unlock = [](void* lock) -> void {},
      .model_lock = [](void) -> void {},
      .model_unlock = [](void) -> void {},
      .notify_render = [](void) -> void {},
  };
}
