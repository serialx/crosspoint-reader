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
