// Host-only EPUB table: keyboard must never invoke these unrelated services.
use super::super::abi::{self as app_abi, Text as XpAppText, TextSink as XpAppTextSink};
use super::super::epub::abi::*;
use xp_ui::reader::input::Services as XpUiReaderInputServices;
macro_rules! unused_leaves {
    ($( $name:ident ($($arg:ident : $ty:ty),* $(,)?) $(-> $ret:ty)?; )*) => {
        $(unsafe extern "C" fn $name($($arg: $ty),*) $(-> $ret)? {
            let _ = ($($arg,)*);
            panic!(concat!("unexpected EPUB leaf: ", stringify!($name)))
        })*
// Destination types stay private. These source signatures mirror their C ABI.
#[allow(clippy::missing_transmute_annotations)]
pub(super) fn leaves() -> XpEpubLeaves { XpEpubLeaves { $($name,)*
fonts_prewarm_begin: unsafe { core::mem::transmute::<unsafe extern "C" fn(*mut core::ffi::c_void) -> u8, _>(fonts_prewarm_begin) },
fonts_prewarm_finish_scan: unsafe { core::mem::transmute::<unsafe extern "C" fn(*mut core::ffi::c_void), _>(fonts_prewarm_finish_scan) },
fonts_prewarm_drop: unsafe { core::mem::transmute::<unsafe extern "C" fn(*mut core::ffi::c_void), _>(fonts_prewarm_drop) },
log: super::log,
render_physical_operation: unsafe { core::mem::transmute::<unsafe extern "C" fn(*const core::ffi::c_void) -> u32, _>(render_physical_operation) },
clipping_physical: core::ptr::null(),
child_selection_physical: unsafe { core::mem::transmute::<SelectionWire, _>(selection()) },
child_drawing: unsafe { core::mem::transmute::<PhysicalWire, _>(PhysicalWire { theme,layout,header,footer,qr,icon,clear,prewarm }) },
} }
    };
}
unused_leaves! {
    local_calendar(out: *mut crate::app::bookmarks::export::Calendar) -> u8;
    storage_read_file(path: XpAppText, storage: *mut core::ffi::c_void) -> XpAppText;
    storage_write_file_atomic(path: XpAppText, bytes: *const u8, size: u32) -> u8;

    render_margins(_a0: *mut i32) ;
    render_geometry(_a0: *mut XpEpubGeometry) ;
    render_restore_bw(_a0: u8) ;
    render_discard_bw() ;
    render_orientation(_a0: u8) ;
    loan_count() -> u32;
    protection_open(_a0: XpAppText, _a1: XpAppTextSink, _a2: *mut core::ffi::c_void) -> *mut core::ffi::c_void;
    protection_close(_a0: *mut core::ffi::c_void) ;
    protection_encrypted(_a0: *mut core::ffi::c_void, _a1: XpAppText) -> u8;
    protection_size(_a0: *mut core::ffi::c_void, _a1: XpAppText) -> u32;
    protection_stream(_a0: *mut core::ffi::c_void, _a1: XpAppText, _a2: xp_core::book::io::Sink, _a3: *mut core::ffi::c_void) -> u8;
    file_seek(_a0: *mut core::ffi::c_void, _a1: u32) -> u8;
    file_size(_a0: *mut core::ffi::c_void) -> u32;
    path_remove_tree(_a0: XpAppText) -> u8;
    storage_read_release(storage: *mut core::ffi::c_void);
    paint_dither(_a0: i32, _a1: i32, _a2: i32, _a3: i32) ;
    paint_pixel(_a0: i32, _a1: i32) ;
    paint_status(_a0: *const XpEpubStatus) ;
    fonts_ensure_loaded() ;
    fonts_release_caches() ;

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
    input_screen_point(_kind: u8, _a0: *mut i32, _a1: *mut i32) -> u8;
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
    child_list_help(_a0: xp_ui::scene::Rect, _a1: XpAppText);
    child_list_icon(kind: u8, _a0: xp_ui::scene::Rect, _a1: xp_ui::scene::Rect, _a2: u8);
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
    recents_remove(_a0: XpAppText) -> u8;
    recents_add(_a0: *const app_abi::Book) ;
    recents_update_path(_a0: XpAppText, _a1: XpAppText, _a2: XpAppText, _a3: XpAppText) ;
    state_open_path() -> XpAppText;
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
    framebuffer_lend() -> u8;
    framebuffer_restore(_a0: u8) ;
    font_ascent(_a0: i32) -> i32;
    render_physical_query(_a0: u8) -> u32;

    image_read_window(_a0: *mut u8, _a1: u32) ;
    display_source(_a0: *mut crate::display_source::DisplaySource) -> u8;
    framebuffer_view(_a0: *mut u32) -> *mut u8;
}

// The C-layout physical table contains these exact signatures. Its private
// constructor is intentionally not exposed to unrelated production callers.
#[repr(C)]
struct PhysicalWire {
    theme: unsafe extern "C" fn(
        *mut xp_ui::reader_chrome::list::Theme,
        *mut xp_ui::reader_chrome::runtime::ThemeFacts,
        u8,
    ),
    layout: unsafe extern "C" fn(*mut xp_ui::utility::Layout) -> u8,
    header: unsafe extern "C" fn(xp_ui::scene::Rect, XpAppText, u8),
    footer: unsafe extern "C" fn(xp_ui::scene::Rect, *const XpAppText),
    qr: unsafe extern "C" fn(xp_ui::scene::Rect, XpAppText),
    icon: unsafe extern "C" fn(u32, xp_ui::scene::Rect, xp_ui::scene::Rect),
    clear: unsafe extern "C" fn(),
    prewarm: unsafe extern "C" fn(
        i32,
        *const core::ffi::c_void,
        unsafe extern "C" fn(*const core::ffi::c_void, u32) -> XpAppText,
        u32,
    ),
}
unsafe extern "C" fn theme(
    _: *mut xp_ui::reader_chrome::list::Theme,
    _: *mut xp_ui::reader_chrome::runtime::ThemeFacts,
    _: u8,
) {
    panic!("theme")
}
unsafe extern "C" fn layout(_: *mut xp_ui::utility::Layout) -> u8 {
    panic!("layout")
}
unsafe extern "C" fn header(_: xp_ui::scene::Rect, _: XpAppText, _: u8) {
    panic!("header")
}
unsafe extern "C" fn footer(_: xp_ui::scene::Rect, _: *const XpAppText) {
    panic!("footer")
}
unsafe extern "C" fn qr(_: xp_ui::scene::Rect, _: XpAppText) {
    panic!("qr")
}
unsafe extern "C" fn icon(_: u32, _: xp_ui::scene::Rect, _: xp_ui::scene::Rect) {
    panic!("icon")
}
unsafe extern "C" fn clear() {
    panic!("clear")
}
unsafe extern "C" fn prewarm(
    _: i32,
    _: *const core::ffi::c_void,
    _: unsafe extern "C" fn(*const core::ffi::c_void, u32) -> XpAppText,
    _: u32,
) {
    panic!("prewarm")
}

unsafe extern "C" fn fonts_prewarm_begin(_a0: *mut core::ffi::c_void) -> u8 {
    panic!("opaque EPUB leaf");
}
unsafe extern "C" fn fonts_prewarm_finish_scan(_a0: *mut core::ffi::c_void) {
    panic!("opaque EPUB leaf");
}
unsafe extern "C" fn fonts_prewarm_drop(_a0: *mut core::ffi::c_void) {
    panic!("opaque EPUB leaf");
}
unsafe extern "C" fn render_physical_operation(_a0: *const core::ffi::c_void) -> u32 {
    panic!("opaque EPUB leaf");
}

// Mirrors the private selection C ABI; every callback rejects unexpected use.
type DefinitionFill = unsafe extern "C" fn(*mut core::ffi::c_void, *mut u8, u32) -> u8;
#[repr(C)]
struct SelectionWire {
    lookup_init: unsafe extern "C" fn(*mut core::ffi::c_void, XpAppText),
    lookup_drop: unsafe extern "C" fn(*mut core::ffi::c_void),
    lookup_open: unsafe extern "C" fn(*mut core::ffi::c_void, *mut u8) -> u8,
    lookup_index: unsafe extern "C" fn(*mut core::ffi::c_void, *mut u8) -> u8,
    lookup_find: unsafe extern "C" fn(*mut core::ffi::c_void, *mut u8) -> u8,
    lookup_discard: unsafe extern "C" fn(*mut core::ffi::c_void),
    definition: super::epub::children::definition_physical::DefinitionPhysical,
    directional_hints: unsafe extern "C" fn(*const XpAppText),
    dictionary_popup_layout: unsafe extern "C" fn(*mut xp_ui::dictionary::popup::Layout),
    wait_paint: unsafe extern "C" fn(),
}
unsafe extern "C" fn unused_init(_: *mut core::ffi::c_void, _: XpAppText) {
    panic!("lookup init");
}
unsafe extern "C" fn unused_drop(_: *mut core::ffi::c_void) {
    panic!("lookup drop");
}
unsafe extern "C" fn unused_result(_: *mut core::ffi::c_void, _: *mut u8) -> u8 {
    panic!("lookup operation");
}
unsafe extern "C" fn unused_hints(_: *const XpAppText) {
    panic!("directional hints");
}
unsafe extern "C" fn unused_layout(_: *mut xp_ui::dictionary::popup::Layout) {
    panic!("popup layout");
}
const fn selection() -> SelectionWire {
    SelectionWire {
        lookup_init: unused_init,
        lookup_drop: unused_drop,
        lookup_open: unused_result,
        lookup_index: unused_result,
        lookup_find: unused_result,
        lookup_discard: unused_drop,
        definition: super::epub::children::definition_physical::DefinitionPhysical::unused(),
        directional_hints: unused_hints,
        dictionary_popup_layout: unused_layout,
        wait_paint: clear,
    }
}
