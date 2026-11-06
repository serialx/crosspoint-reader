// Typed, fail-closed physical leaves for the real EpubSession Miri composition.
use crate::app::abi::{
    self as app_abi, Text as XpAppText, TextSink as XpAppTextSink, XpAppNativeText,
};
use crate::app::epub::abi::*;
use xp_ui::reader::input::Services as XpUiReaderInputServices;
macro_rules! unused_leaves {
    ($( $name:ident ($($arg:ident : $ty:ty),* $(,)?) $(-> $ret:ty)?; )*) => {
        $(unsafe extern "C" fn $name($($arg: $ty),*) $(-> $ret)? {
            let _ = ($($arg,)*);
            panic!(concat!("unexpected EPUB leaf: ", stringify!($name)))
        })*
        pub(in crate::app::epub::children) fn leaves() -> XpEpubLeaves { XpEpubLeaves { $($name,)* clipping_physical: core::ptr::null(), child_selection_physical: selection(), child_drawing: super::Physical { list_theme: super::theme, utility_layout: super::layout, header: super::header, utility_footer: super::footer, qr: super::qr, toolbar_icon: super::icon, clear_fonts: super::lock, prewarm_rows: super::prewarm } } }
    };
}
unused_leaves! {
    render_margins(_a0: *mut i32) ;
    render_geometry(_a0: *mut XpEpubGeometry) ;
    render_restore_bw(_a0: u8) ;
    render_discard_bw() ;
    render_orientation(_a0: u8) ;
    loan_count() -> u32;
    book_facade_open(_a0: *mut core::ffi::c_void, _a1: *const XpEpubBookView, _a2: *const XpEpubBookBorrow) -> *mut core::ffi::c_void;
    book_facade_close(_a0: *mut core::ffi::c_void) ;
    book_set_extractor(_a0: *mut core::ffi::c_void) ;
    protection_open(_a0: XpAppText, _a1: XpAppTextSink, _a2: *mut core::ffi::c_void) -> *mut core::ffi::c_void;
    protection_close(_a0: *mut core::ffi::c_void) ;
    protection_encrypted(_a0: *mut core::ffi::c_void, _a1: XpAppText) -> u8;
    protection_size(_a0: *mut core::ffi::c_void, _a1: XpAppText) -> u32;
    protection_stream(_a0: *mut core::ffi::c_void, _a1: XpAppText, _a2: xp_core::book::io::Sink, _a3: *mut core::ffi::c_void) -> u8;
    file_seek(_a0: *mut core::ffi::c_void, _a1: u32) -> u8;
    file_size(_a0: *mut core::ffi::c_void) -> u32;
    path_remove_tree(_a0: XpAppText) -> u8;
    progress_to_reader(_a0: *mut core::ffi::c_void, _a1: *const XpEpubSavedProgress, _a2: i32, _a3: i32, _a4: *mut XpEpubPosition) ;
    progress_to_saved(_a0: *mut core::ffi::c_void, _a1: *const XpEpubPosition, _a2: XpEpubSavedProgressSink, _a3: *mut core::ffi::c_void) -> u8;
    progress_save(_a0: *mut core::ffi::c_void, _a1: *const XpEpubPosition) -> u8;
    paint_dither(_a0: i32, _a1: i32, _a2: i32, _a3: i32) ;
    paint_pixel(_a0: i32, _a1: i32) ;
    paint_status(_a0: *const XpEpubStatus) ;
    image_clear_failures() ;
    image_release_cache() ;
    fonts_ensure_loaded() ;
    fonts_release_caches() ;
    fonts_prewarm_begin(_a0: *mut crate::app::epub::render_resources::PrewarmStorage) -> u8;
    fonts_prewarm_finish_scan(_a0: *mut crate::app::epub::render_resources::PrewarmStorage) ;
    fonts_prewarm_drop(_a0: *mut crate::app::epub::render_resources::PrewarmStorage) ;
    fonts_take_corrupt_notice() -> u8;
    font_sizes(_a0: XpAppText, _a1: XpAppTextSink, _a2: *mut core::ffi::c_void) -> u8;
    settings_snapshot(_a0: u8, _a1: *mut XpEpubSettings) ;
    settings_render_spec(_a0: u16, _a1: u16, _a2: *mut XpEpubRenderSpec) ;
    settings_orientation(_a0: u8) ;
    settings_font_size(_a0: u8) ;
    settings_line_spacing(_a0: u8) ;
    settings_alignment(_a0: u8) ;
    settings_focus_reading(_a0: u8) ;
    settings_inversion(_a0: u8) ;
    settings_ble_enabled(_a0: u8) ;
    settings_save() ;
    input_services() -> *const XpUiReaderInputServices;
    input_facts(_a0: *mut XpEpubInputFacts) ;
    input_pressed(_a0: u32) -> u8;
    input_screen_long(_a0: *mut i32, _a1: *mut i32) -> u8;
    input_screen_tap(_a0: *mut i32, _a1: *mut i32) -> u8;
    input_raw_contact() -> u8;
    render_busy() -> u8;
    file_open_read(_a0: XpAppText, _a1: XpAppText) -> *mut core::ffi::c_void;
    file_open_write(_a0: XpAppText, _a1: XpAppText) -> *mut core::ffi::c_void;
    file_read(_a0: *mut core::ffi::c_void, _a1: *mut u8, _a2: u32) -> i32;
    file_write(_a0: *mut core::ffi::c_void, _a1: *const u8, _a2: u32) -> u32;
    file_close(_a0: *mut core::ffi::c_void) -> u8;
    path_remove(_a0: XpAppText) -> u8;
    path_rename(_a0: XpAppText, _a1: XpAppText) -> u8;
    path_mkdir(_a0: XpAppText) -> u8;
    native_path_hash(_a0: XpAppText) -> u64;
    heap_snapshot(_a0: *mut XpEpubHeap) ;
    radio_status(_a0: *mut XpEpubRadio, _a1: *mut XpEpubPeer) ;
    radio_before_build() -> u8;
    radio_settle_start() ;
    radio_holds_heap() -> u8;
    radio_after_paint() ;
    radio_switch_off() ;
    radio_request_connect() ;
    trusted_now() -> i64;
    trusted_sync(_a0: u32) -> u8;
    wifi_connected() -> u8;
    wifi_disconnect(_a0: u8) ;
    restart_reader() ;
    child_text_settings() -> *mut core::ffi::c_void;
    child_selection(_a0: *mut XpEpubOwnedPage, _a1: u8, _a2: *const XpEpubSelectionArgs) -> *mut core::ffi::c_void;
    child_bookmarks(_a0: *mut core::ffi::c_void, _a1: XpAppText) -> *mut core::ffi::c_void;
    child_clippings() -> *mut core::ffi::c_void;
    child_wifi() -> *mut core::ffi::c_void;
    child_sync(_a0: XpAppText, _a1: *const XpEpubPosition, _a2: *const XpEpubSavedProgress, _a3: XpAppText, _a4: u8) -> *mut core::ffi::c_void;
    popup_open() -> *mut core::ffi::c_void;
    popup_close(_a0: *mut core::ffi::c_void) ;
    popup_active(_a0: *mut core::ffi::c_void) -> u8;
    popup_show(_a0: *mut core::ffi::c_void, _a1: XpAppText, _a2: XpAppText, _a3: u32, _a4: i32, _a5: unsafe extern "C" fn( label_context: *mut core::ffi::c_void, index: u32, sink: XpAppTextSink, sink_context: *mut core::ffi::c_void, ) -> u8, label_context: *mut core::ffi::c_void, choose: unsafe extern "C" fn(choice_context: *mut core::ffi::c_void, _a6: i32), choice_context: *mut core::ffi::c_void,) ;
    popup_dismiss(_a0: *mut core::ffi::c_void) ;
    popup_input(_a0: *mut core::ffi::c_void, _a1: *mut XpEpubPopupInput) ;
    popup_paint(_a0: *mut core::ffi::c_void) ;
    popup_process_render(_a0: *mut core::ffi::c_void) ;
    end_open() -> *mut core::ffi::c_void;
    end_close(_a0: *mut core::ffi::c_void) ;
    end_load(_a0: *mut core::ffi::c_void, _a1: XpAppText) ;
    end_menu_active(_a0: *mut core::ffi::c_void) -> u8;
    end_input(_a0: *mut core::ffi::c_void, _a1: unsafe extern "C" fn(*mut core::ffi::c_void, u8, XpAppText), _a2: *mut core::ffi::c_void) ;
    end_paint(_a0: *mut core::ffi::c_void) ;
    bookmarks_open() -> *mut core::ffi::c_void;
    bookmarks_close(_a0: *mut core::ffi::c_void) ;
    bookmarks_clear(_a0: *mut core::ffi::c_void) ;
    bookmarks_load(_a0: *mut core::ffi::c_void, _a1: XpAppText) -> u8;
    bookmarks_save(_a0: *mut core::ffi::c_void, _a1: XpAppText) -> u8;
    bookmarks_count(_a0: *mut core::ffi::c_void) -> u32;
    bookmarks_row(_a0: *mut core::ffi::c_void, _a1: u32, _a2: XpEpubBookmarkSink, _a3: *mut core::ffi::c_void) -> u8;
    bookmarks_retain(_a0: *mut core::ffi::c_void, _a1: unsafe extern "C" fn(*mut core::ffi::c_void, *const XpEpubBookmark) -> u8, context: *mut core::ffi::c_void,) -> u32;
    bookmarks_insert_front(_a0: *mut core::ffi::c_void, _a1: *const XpEpubBookmark) -> u8;
    clippings_load(_a0: *const app_abi::Book) -> u8;
    clippings_unload() ;
    clippings_count() -> u32;
    clippings_row(_a0: u32, _a1: *mut XpEpubClip) ;
    clippings_remove(_a0: u32) -> u8;
    clippings_insert(_a0: *const XpEpubClipInsert) -> u8;
    clippings_export(_a0: XpAppText, _a1: XpAppText, _a2: XpAppText, _a3: i32, _a4: XpAppNativeText) -> u8;
    clippings_move_book(_a0: XpAppText, _a1: XpAppText) -> u8;
    recents_remove(_a0: XpAppText) -> u8;
    recents_add(_a0: *const app_abi::Book) ;
    recents_update_path(_a0: XpAppText, _a1: XpAppText, _a2: XpAppText, _a3: XpAppText) ;
    state_open_path(_a0: XpAppTextSink, _a1: *mut core::ffi::c_void) -> u8;
    state_set_open_path(_a0: XpAppText) ;
    state_reset_reader_count() ;
    state_save() ;
    has_sync_credentials() -> u8;
    has_exit_subscriber() -> u8;
    has_session_subscriber() -> u8;
    document_id(_a0: XpAppText, _a1: XpAppTextSink, _a2: *mut core::ffi::c_void) -> u8;
    emit_open(_a0: XpAppText) ;
    emit_exit(_a0: XpAppText, _a1: i32) ;
    emit_session(_a0: XpAppText, _a1: XpAppText, _a2: *const XpEpubSessionEvent) ;
    log(_a0: u8, _a1: XpAppText, _a2: XpAppText) ;
    font_text_advance(_a0: XpAppText, _a1: i32, _a2: u8, _a3: i8) -> i32;
    log_enabled(_a0: u8) -> u8;
    input_snapshot(_a0: *mut xp_ui::model::Input) ;
    cache_lock() ;
    cache_unlock() ;
    scratch_claim(_a0: u32) -> *mut u8;
    scratch_release(_a0: *const u8) ;
    inflate_oom_log(_a0: XpAppText, _a1: u32) ;
    build_metrics() -> *const xp_core::para::paragraph::Metrics;
    build_allocator() -> *const xp_core::para::Allocator;
    font_variant(_a0: i32, _a1: f32) -> i32;
    font_line_height(_a0: i32, _a1: f32) -> i32;
    font_ready(_a0: i32, _a1: i32) -> i32;
    font_codepoint(_a0: i32, _a1: u32, _a2: u8, _a3: *mut i32, _a4: *mut i32) -> u8;
    hyphen_language(_a0: XpAppText) -> i32;
    file_position(_a0: *mut core::ffi::c_void) -> u32;
    file_flush(_a0: *mut core::ffi::c_void) ;
    image_supported(_a0: XpAppText) -> u8;
    image_dimensions(_a0: XpAppText, _a1: *mut u16, _a2: *mut u16) -> u8;
    framebuffer_lend() -> u8;
    framebuffer_restore(_a0: u8) ;
    paint_arena(_a0: *const XpEpubPageView, _a1: i32, _a2: i32, _a3: i32, _a4: u8) ;
    image_needs_decode(_a0: XpAppText, _a1: XpAppText, _a2: i16, _a3: i16) -> u8;
    font_ascent(_a0: i32) -> i32;
    render_physical_query(_a0: u8) -> u32;
    render_physical_operation(_a0: *const crate::app::epub::render_resources::physical::Op) -> u32;
    render_plan(_a0: *mut crate::page_render::Plan, _a1: *mut u8, _a2: u16, _a3: u8) ;
    image_read_window(_a0: *mut u8, _a1: u32) ;
    display_source(_a0: *mut crate::display_source::DisplaySource) -> u8;
    framebuffer_view(_a0: *mut u32) -> *mut u8;
}

use crate::app::epub::children::selection_physical::{
    DefinitionFill, DefinitionStorage, LookupStorage, Physical as Selection,
};
unsafe extern "C" fn unused_init(_: *mut LookupStorage, _: XpAppText) {
    panic!("lookup init");
}
unsafe extern "C" fn unused_drop(_: *mut LookupStorage) {
    panic!("lookup drop");
}
unsafe extern "C" fn unused_result(_: *mut LookupStorage, _: *mut u8) -> u8 {
    panic!("lookup operation");
}
unsafe extern "C" fn unused_lookup(
    _: *mut DefinitionStorage,
    _: *mut LookupStorage,
) -> *mut core::ffi::c_void {
    panic!("definition lookup");
}
unsafe extern "C" fn unused_plain(
    _: *mut DefinitionStorage,
    _: XpAppText,
    _: u32,
    _: DefinitionFill,
    _: *mut core::ffi::c_void,
) -> *mut core::ffi::c_void {
    panic!("definition plain");
}
unsafe extern "C" fn unused_definition_drop(_: *mut DefinitionStorage) {
    panic!("definition drop");
}
unsafe extern "C" fn unused_hints(_: *const XpAppText) {
    panic!("directional hints");
}
unsafe extern "C" fn unused_layout(_: *mut xp_ui::dictionary::popup::Layout) {
    panic!("popup layout");
}
pub(in crate::app::epub::children) fn selection() -> Selection {
    Selection {
        lookup_init: unused_init,
        lookup_drop: unused_drop,
        lookup_open: unused_result,
        lookup_index: unused_result,
        lookup_find: unused_result,
        lookup_discard: unused_drop,
        definition_lookup: unused_lookup,
        definition_plain: unused_plain,
        definition_drop: unused_definition_drop,
        directional_hints: unused_hints,
        dictionary_popup_layout: unused_layout,
        wait_paint: super::lock,
    }
}
