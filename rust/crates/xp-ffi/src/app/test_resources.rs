use super::abi::*;

unsafe extern "C" fn settings_snapshot() -> HomeSettings {
    panic!("unexpected resource callback: settings_snapshot")
}
unsafe extern "C" fn recents_visit(_arg0: BookSink, _arg1: *mut core::ffi::c_void) -> u8 {
    panic!("unexpected resource callback: recents_visit")
}
unsafe extern "C" fn recent_update(_arg0: *const Book) {
    panic!("unexpected resource callback: recent_update")
}
unsafe extern "C" fn opds_count() -> u32 {
    panic!("unexpected resource callback: opds_count")
}
unsafe extern "C" fn book_progress(_arg0: Text) -> i32 {
    panic!("unexpected resource callback: book_progress")
}
unsafe extern "C" fn library_rebuild(_arg0: u8) -> u8 {
    panic!("unexpected resource callback: library_rebuild")
}
unsafe extern "C" fn book_open(_arg0: BookKind, _arg1: Text) -> *mut core::ffi::c_void {
    panic!("unexpected resource callback: book_open")
}
unsafe extern "C" fn book_thumb_path(
    _arg0: *mut core::ffi::c_void,
    _arg1: i32,
    _arg2: TextSink,
    _arg3: *mut core::ffi::c_void,
) -> u8 {
    panic!("unexpected resource callback: book_thumb_path")
}
unsafe extern "C" fn book_load(_arg0: *mut core::ffi::c_void, _arg1: u8, _arg2: u8) -> u8 {
    panic!("unexpected resource callback: book_load")
}
unsafe extern "C" fn book_generate_thumb(_arg0: *mut core::ffi::c_void, _arg1: i32) -> u8 {
    panic!("unexpected resource callback: book_generate_thumb")
}
unsafe extern "C" fn book_generate_thumb_from_source(
    _arg0: *mut core::ffi::c_void,
    _arg1: i32,
) -> u8 {
    panic!("unexpected resource callback: book_generate_thumb_from_source")
}
unsafe extern "C" fn book_close(_arg0: *mut core::ffi::c_void) {
    panic!("unexpected resource callback: book_close")
}
unsafe extern "C" fn path_exists(_arg0: Text) -> u8 {
    panic!("unexpected resource callback: path_exists")
}
unsafe extern "C" fn thumbnail_path(
    _arg0: Text,
    _arg1: i32,
    _arg2: TextSink,
    _arg3: *mut core::ffi::c_void,
) -> u8 {
    panic!("unexpected resource callback: thumbnail_path")
}
unsafe extern "C" fn bitmap_open(_arg0: Text) -> *mut core::ffi::c_void {
    panic!("unexpected resource callback: bitmap_open")
}
unsafe extern "C" fn bitmap_info(_arg0: *mut core::ffi::c_void) -> BitmapInfo {
    panic!("unexpected resource callback: bitmap_info")
}
unsafe extern "C" fn bitmap_draw(
    _arg0: *mut core::ffi::c_void,
    _arg1: xp_ui::scene::Rect,
    _arg2: i32,
) -> u8 {
    panic!("unexpected resource callback: bitmap_draw")
}
unsafe extern "C" fn bitmap_close(_arg0: *mut core::ffi::c_void) {
    panic!("unexpected resource callback: bitmap_close")
}
unsafe extern "C" fn theme_snapshot(_arg0: *mut Theme) {
    panic!("unexpected resource callback: theme_snapshot")
}
unsafe extern "C" fn translate(_arg0: u16) -> Text {
    panic!("unexpected resource callback: translate")
}
unsafe extern "C" fn header_draw(_arg0: xp_ui::scene::Rect, _arg1: Text) {
    panic!("unexpected resource callback: header_draw")
}
unsafe extern "C" fn footer_draw(_arg0: *const Text) {
    panic!("unexpected resource callback: footer_draw")
}
unsafe extern "C" fn placeholder_draw(_arg0: xp_ui::scene::Rect) {
    panic!("unexpected resource callback: placeholder_draw")
}
unsafe extern "C" fn popup_draw(_arg0: Text) -> xp_ui::scene::Rect {
    panic!("unexpected resource callback: popup_draw")
}
unsafe extern "C" fn popup_progress(_arg0: xp_ui::scene::Rect, _arg1: i32) {
    panic!("unexpected resource callback: popup_progress")
}
unsafe extern "C" fn icon_draw(_arg0: u8, _arg1: xp_ui::scene::Rect) {
    panic!("unexpected resource callback: icon_draw")
}
unsafe extern "C" fn text_draw(
    _arg0: *const xp_ui::scene::Command,
    _arg1: xp_ui::scene::Rect,
    _arg2: Text,
) {
    panic!("unexpected resource callback: text_draw")
}
unsafe extern "C" fn text_width(_arg0: Text, _arg1: i32, _arg2: u8) -> i32 {
    panic!("unexpected resource callback: text_width")
}
unsafe extern "C" fn line_height(_arg0: i32) -> i32 {
    panic!("unexpected resource callback: line_height")
}
unsafe extern "C" fn text_wrap(
    _arg0: Text,
    _arg1: TextLayout,
    _arg2: TextSink,
    _arg3: *mut core::ffi::c_void,
) -> u8 {
    panic!("unexpected resource callback: text_wrap")
}
unsafe extern "C" fn text_truncate(
    _arg0: Text,
    _arg1: TextLayout,
    _arg2: TextSink,
    _arg3: *mut core::ffi::c_void,
) -> u8 {
    panic!("unexpected resource callback: text_truncate")
}
unsafe extern "C" fn text_box_height(_arg0: Text, _arg1: TextLayout) -> i32 {
    panic!("unexpected resource callback: text_box_height")
}
unsafe extern "C" fn region_size(_arg0: xp_ui::scene::Rect) -> usize {
    panic!("unexpected resource callback: region_size")
}
unsafe extern "C" fn region_read(_arg0: xp_ui::scene::Rect, _arg1: *mut u8, _arg2: usize) -> u8 {
    panic!("unexpected resource callback: region_read")
}
unsafe extern "C" fn region_write(_arg0: xp_ui::scene::Rect, _arg1: *const u8, _arg2: usize) -> u8 {
    panic!("unexpected resource callback: region_write")
}
unsafe extern "C" fn rounded_mask(_arg0: xp_ui::scene::Rect, _arg1: u8, _arg2: u8) {
    panic!("unexpected resource callback: rounded_mask")
}
unsafe extern "C" fn polygon_draw(_arg0: *const Point, _arg1: u8, _arg2: u8) {
    panic!("unexpected resource callback: polygon_draw")
}
unsafe extern "C" fn paint_scene(
    _arg0: *const xp_ui::ffi::SceneView,
    _arg1: *const xp_ui::ffi::Paint,
) -> u8 {
    panic!("unexpected resource callback: paint_scene")
}
unsafe extern "C" fn refresh(_arg0: u8) {
    panic!("unexpected resource callback: refresh")
}
unsafe extern "C" fn buffer_allocate(_arg0: MemoryDomain, _arg1: usize) -> *mut u8 {
    panic!("unexpected resource callback: buffer_allocate")
}
unsafe extern "C" fn buffer_release(_arg0: MemoryDomain, _arg1: *mut u8) {
    panic!("unexpected resource callback: buffer_release")
}

pub fn resources() -> Resources {
    Resources {
        settings_snapshot,
        recents_visit,
        recent_missing: super::home_books::tests::recent_missing,
        recent_update,
        opds_count,
        book_progress,
        library_open: super::home_books::tests::library_open,
        library_count: super::home_books::tests::library_count,
        library_row: super::home_books::tests::library_row,
        library_close: super::home_books::tests::library_close,
        library_rebuild,
        book_open,
        book_thumb_path,
        book_load,
        book_generate_thumb,
        book_generate_thumb_from_source,
        book_close,
        path_exists,
        thumbnail_path,
        bitmap_open,
        bitmap_info,
        bitmap_draw,
        bitmap_close,
        theme_snapshot,
        translate,
        header_draw,
        footer_draw,
        placeholder_draw,
        popup_draw,
        popup_progress,
        icon_draw,
        text_draw,
        text_width,
        line_height,
        text_wrap,
        text_truncate,
        text_box_height,
        region_size,
        region_read,
        region_write,
        rounded_mask,
        polygon_draw,
        paint_scene,
        refresh,
        buffer_allocate,
        buffer_release,
    }
}

unsafe extern "C" fn runtime_input_mapped_snapshot(
    _arg0: u8,
    _arg1: u8,
    _arg2: u8,
    _arg3: *mut MappedSnapshot,
) {
    panic!("unexpected runtime callback: input_mapped_snapshot")
}
unsafe extern "C" fn runtime_input_shared_power_mode(_arg0: u8) {
    panic!("unexpected runtime callback: input_shared_power_mode")
}
unsafe extern "C" fn runtime_input_power_confirm(_arg0: u8) {
    panic!("unexpected runtime callback: input_power_confirm")
}
unsafe extern "C" fn runtime_input_remote_click(_arg0: u8) -> u8 {
    panic!("unexpected runtime callback: input_remote_click")
}
unsafe extern "C" fn runtime_loop_settings(_arg0: *mut Settings) {
    panic!("unexpected runtime callback: loop_settings")
}
unsafe extern "C" fn runtime_display_geometry() -> DisplayGeometry {
    panic!("unexpected runtime callback: display_geometry")
}
unsafe extern "C" fn runtime_header_back_clear() {
    panic!("unexpected runtime callback: header_back_clear")
}
unsafe extern "C" fn runtime_now_ms() -> u32 {
    panic!("unexpected runtime callback: now_ms")
}
unsafe extern "C" fn runtime_delay_ms(_arg0: u32) {
    panic!("unexpected runtime callback: delay_ms")
}
unsafe extern "C" fn runtime_idle_wait(_arg0: u32, _arg1: u32) {
    panic!("unexpected runtime callback: idle_wait")
}
unsafe extern "C" fn runtime_yield_task() {
    panic!("unexpected runtime callback: yield_task")
}
unsafe extern "C" fn runtime_power_saving(_arg0: u8) {
    panic!("unexpected runtime callback: power_saving")
}
unsafe extern "C" fn runtime_tilt_update(_arg0: u8, _arg1: u8, _arg2: u8) -> u8 {
    panic!("unexpected runtime callback: tilt_update")
}
unsafe extern "C" fn runtime_battery_capacity_step() -> u8 {
    panic!("unexpected runtime callback: battery_capacity_step")
}
unsafe extern "C" fn runtime_fading_fix(_arg0: u8) {
    panic!("unexpected runtime callback: fading_fix")
}
unsafe extern "C" fn runtime_memory_diagnostic() {
    panic!("unexpected runtime callback: memory_diagnostic")
}
unsafe extern "C" fn runtime_console_service() {
    panic!("unexpected runtime callback: console_service")
}
unsafe extern "C" fn runtime_screenshot() {
    panic!("unexpected runtime callback: screenshot")
}
unsafe extern "C" fn runtime_touch_feedback(_arg0: u8) {
    panic!("unexpected runtime callback: touch_feedback")
}
unsafe extern "C" fn runtime_frontlight_present() -> u8 {
    panic!("unexpected runtime callback: frontlight_present")
}
unsafe extern "C" fn runtime_frontlight_state() -> u8 {
    panic!("unexpected runtime callback: frontlight_state")
}
unsafe extern "C" fn runtime_frontlight_set(_arg0: u8) {
    panic!("unexpected runtime callback: frontlight_set")
}
unsafe extern "C" fn runtime_settings_save(_arg0: *const Settings) {
    panic!("unexpected runtime callback: settings_save")
}
unsafe extern "C" fn runtime_ble_tick(_arg0: *const BleScene) -> u8 {
    panic!("unexpected runtime callback: ble_tick")
}
unsafe extern "C" fn runtime_ble_before_screen_change(_arg0: u8) -> u8 {
    panic!("unexpected runtime callback: ble_before_screen_change")
}
unsafe extern "C" fn runtime_sleep_enter(_arg0: u8) {
    panic!("unexpected runtime callback: sleep_enter")
}
unsafe extern "C" fn runtime_diagnostic(_arg0: u8, _arg1: u32, _arg2: u32) {
    panic!("unexpected runtime callback: diagnostic")
}
unsafe extern "C" fn runtime_render_lock() -> *mut core::ffi::c_void {
    panic!("unexpected runtime callback: render_lock")
}
unsafe extern "C" fn runtime_render_try_lock() -> *mut core::ffi::c_void {
    panic!("unexpected runtime callback: render_try_lock")
}
unsafe extern "C" fn runtime_render_unlock(_arg0: *mut core::ffi::c_void) {
    panic!("unexpected runtime callback: render_unlock")
}
unsafe extern "C" fn runtime_model_lock() {
    panic!("unexpected runtime callback: model_lock")
}
unsafe extern "C" fn runtime_model_unlock() {
    panic!("unexpected runtime callback: model_unlock")
}
unsafe extern "C" fn runtime_notify_render() {
    panic!("unexpected runtime callback: notify_render")
}

pub fn runtime() -> Runtime {
    Runtime {
        input_mapped_snapshot: runtime_input_mapped_snapshot,
        input_shared_power_mode: runtime_input_shared_power_mode,
        input_power_confirm: runtime_input_power_confirm,
        input_remote_click: runtime_input_remote_click,
        loop_settings: runtime_loop_settings,
        display_geometry: runtime_display_geometry,
        header_back_clear: runtime_header_back_clear,
        now_ms: runtime_now_ms,
        delay_ms: runtime_delay_ms,
        idle_wait: runtime_idle_wait,
        yield_task: runtime_yield_task,
        power_saving: runtime_power_saving,
        tilt_update: runtime_tilt_update,
        battery_capacity_step: runtime_battery_capacity_step,
        fading_fix: runtime_fading_fix,
        memory_diagnostic: runtime_memory_diagnostic,
        console_service: runtime_console_service,
        screenshot: runtime_screenshot,
        touch_feedback: runtime_touch_feedback,
        frontlight_present: runtime_frontlight_present,
        frontlight_state: runtime_frontlight_state,
        frontlight_set: runtime_frontlight_set,
        settings_save: runtime_settings_save,
        ble_tick: runtime_ble_tick,
        ble_before_screen_change: runtime_ble_before_screen_change,
        sleep_enter: runtime_sleep_enter,
        diagnostic: runtime_diagnostic,
        render_lock: runtime_render_lock,
        render_try_lock: runtime_render_try_lock,
        render_unlock: runtime_render_unlock,
        model_lock: runtime_model_lock,
        model_unlock: runtime_model_unlock,
        notify_render: runtime_notify_render,
    }
}
