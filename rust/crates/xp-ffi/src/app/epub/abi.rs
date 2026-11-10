// Evidence draft: import existing ABI types at the platform boundary.
use super::super::abi::{Text as XpAppText, TextSink as XpAppTextSink};
use super::app_abi;
use xp_ui::reader::input::Services as XpUiReaderInputServices;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubPosition {
    pub spine: i32,
    pub page: i32,
    pub count: i32,
    pub visible_offset: u32,
    pub paragraph: u16,
    pub list_item: u16,
    pub has_visible_offset: u8,
    pub has_paragraph: u8,
    pub has_list_item: u8,
    pub resolved_spine: u8,
    pub mapped_page: u8,
    pub anchor: [u8; 64],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubSavedProgress {
    pub xpath: XpAppText,
    pub percentage: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubSpine {
    pub href: XpAppText,
    pub bytes: u32,
    pub toc_index: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubToc {
    pub title: XpAppText,
    pub anchor: XpAppText,
    pub spine: i32,
    pub level: i32,
    pub href: XpAppText,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubRenderSpec {
    pub font: i32,
    pub compression: f32,
    pub width: u16,
    pub height: u16,
    pub paragraph_spacing: u8,
    pub paragraph_indent: u8,
    pub character_spacing: i8,
    pub word_spacing_percent: u8,
    pub alignment: u8,
    pub hyphenation: u8,
    pub embedded_style: u8,
    pub images: u8,
    pub focus_reading: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubSettings {
    pub font: i32,
    pub orientation: u8,
    pub font_size: u8,
    pub line_spacing: u8,
    pub alignment: u8,
    pub focus_reading: u8,
    pub screen_margin: u8,
    pub text_aa: u8,
    pub embedded_style: u8,
    pub remove_read_recents: u8,
    pub move_finished: u8,
    pub menu_style: u8,
    pub long_menu: u8,
    pub long_button: u8,
    pub short_power: u8,
    pub tilt: u8,
    pub touch_controls: u8,
    pub page_gesture: u8,
    pub previous_gesture: u8,
    pub show_menu: u8,
    pub back_to_browser: u8,
    pub screen_inverted: u8,
    pub ble_enabled: u8,
    pub dictionary_available: u8,
    pub refresh_frequency: u8,
    pub font_family: XpAppText,
}

pub(super) use super::page_records::*;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubHeap {
    pub free_bytes: u32,
    pub largest_block: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubRadio {
    pub compiled_in: u8,
    pub running: u8,
    pub starting: u8,
    pub stopping: u8,
    pub note: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubPeer {
    pub name: [u8; 64],
    pub addr: [u8; 18],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubGeometry {
    pub width: i32,
    pub height: i32,
    pub status_height: i32,
    pub progress_height: i32,
    pub status_margin: i32,
    pub ui_font_12: i32,
    pub title_mode: u8,
    pub has_framebuffer: u8,
    pub async_refresh: u8,
    pub xteink_panel: u8,
    pub a4: u8,
    pub has_ttf: u8,
    pub strip_enabled: u8,
    pub has_font_cache: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubInputFacts {
    pub held_ms: u32,
    pub raw_held: u32,
    pub raw_edges: u8,
    pub remote_click: u8,
    pub touch: u8,
    pub board_touch: u8,
    pub home_action: u8,
    pub slide_owns_light: u8,
}

const _: () = {
    assert!(core::mem::size_of::<XpEpubInputFacts>() == 16);
    assert!(core::mem::align_of::<XpEpubInputFacts>() == 4);
    assert!(core::mem::offset_of!(XpEpubInputFacts, touch) == 10);
    assert!(core::mem::offset_of!(XpEpubInputFacts, board_touch) == 11);
    assert!(core::mem::offset_of!(XpEpubInputFacts, home_action) == 12);
    assert!(core::mem::offset_of!(XpEpubInputFacts, slide_owns_light) == 13);
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubMenuArgs {
    pub title: XpAppText,
    pub display_page: i32,
    pub pages: i32,
    pub percent: i32,
    pub orientation: u8,
    pub footnotes: u8,
    pub bookmarks: u8,
    pub clippings: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubSelectionArgs {
    pub left: i32,
    pub top: i32,
    pub x: i32,
    pub y: i32,
    pub mode: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubToolbarModel {
    pub chapter: XpAppText,
    pub page_info: XpAppText,
    pub panel_title: XpAppText,
    pub progress_permille: i32,
    pub item_count: i32,
    pub selected: i32,
    pub active_tool: i32,
    pub panel: u8,
    pub dense: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubToolbarRow {
    pub label: XpAppText,
    pub value: XpAppText,
    pub label_spaces: u32,
    pub toggle: u8,
    pub checked: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubRouted {
    pub value: i32,
    pub permille: i32,
    pub x: i32,
    pub y: i32,
    pub event: u8,
    pub routed: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubPopupInput {
    pub active: u8,
    pub changed: u8,
    pub consumed: u8,
}

const _: () = {
    assert!(core::mem::size_of::<XpEpubPopupInput>() == 3);
    assert!(core::mem::offset_of!(XpEpubPopupInput, active) == 0);
    assert!(core::mem::offset_of!(XpEpubPopupInput, changed) == 1);
    assert!(core::mem::offset_of!(XpEpubPopupInput, consumed) == 2);
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubStatus {
    pub title: XpAppText,
    pub book_progress: f32,
    pub page: i32,
    pub pages: i32,
    pub y_offset: i32,
    pub bookmarked: u8,
    pub building: u8,
}

#[cfg(feature = "app-test")]
#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubBookmark {
    pub xpath: XpAppText,
    pub summary: XpAppText,
    pub name: XpAppText,
    pub percentage: f32,
    pub spine: u16,
    pub page_count: u16,
    pub chapter_page: u16,
    pub has_offset: u8,
    pub offset: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubSessionEvent {
    pub start_time: i64,
    pub end_time: i64,
    pub duration_seconds: u32,
    pub start_progress: u16,
    pub end_progress: u16,
}

pub type XpEpubSpineSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubSpine) -> u8;
pub type XpEpubTocSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubToc) -> u8;
#[cfg(feature = "app-test")]
pub type XpEpubToolbarRowSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubToolbarRow) -> u8;
pub type XpEpubBuildPopup = unsafe extern "C" fn(context: *mut core::ffi::c_void);

#[cfg(feature = "app-test")]
pub type XpEpubRowSource = unsafe extern "C" fn(
    context: *mut core::ffi::c_void,
    index: i32,
    sink: XpEpubToolbarRowSink,
    sink_context: *mut core::ffi::c_void,
) -> u8;

#[repr(C)]
pub struct XpEpubBookView {
    pub cache_path: XpAppText,
    pub language: XpAppText,
    pub spine_count: i32,
    pub toc_count: i32,
}

#[repr(C)]
pub struct XpEpubBookBorrow {
    pub spine: unsafe extern "C" fn(
        *mut core::ffi::c_void,
        i32,
        XpEpubSpineSink,
        *mut core::ffi::c_void,
    ) -> u8,
    pub toc: unsafe extern "C" fn(
        *mut core::ffi::c_void,
        i32,
        XpEpubTocSink,
        *mut core::ffi::c_void,
    ) -> u8,
    pub cumulative: unsafe extern "C" fn(*mut core::ffi::c_void, i32) -> u32,
    pub spine_for_size: unsafe extern "C" fn(*mut core::ffi::c_void, u32) -> i32,
    pub stream: unsafe extern "C" fn(
        *mut core::ffi::c_void,
        XpAppText,
        u32,
        u8,
        xp_core::book::io::Sink,
        *mut core::ffi::c_void,
    ) -> u8,
    pub item_size: unsafe extern "C" fn(*mut core::ffi::c_void, XpAppText, *mut u32) -> u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubPageView {
    pub arena: *const u8,
    pub size: usize,
    pub visible_offset: u32,
}
#[repr(C)]
pub struct XpEpubOwnedPage {
    pub view: XpEpubPageView,
    pub owner: *mut core::ffi::c_void,
    pub destroy: unsafe extern "C" fn(*mut core::ffi::c_void),
}

#[repr(C)]
pub struct XpEpubLeaves {
    pub render_margins: unsafe extern "C" fn(margins: *mut i32),
    pub render_geometry: unsafe extern "C" fn(out: *mut XpEpubGeometry),
    pub render_restore_bw: unsafe extern "C" fn(resync: u8),
    pub render_discard_bw: unsafe extern "C" fn(),
    pub render_orientation: unsafe extern "C" fn(orientation: u8),
    pub loan_count: unsafe extern "C" fn() -> u32,
    pub book_facade_open: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        view: *const XpEpubBookView,
        borrow: *const XpEpubBookBorrow,
    ) -> *mut core::ffi::c_void,
    pub book_facade_close: unsafe extern "C" fn(facade: *mut core::ffi::c_void),
    pub book_set_extractor: unsafe extern "C" fn(facade: *mut core::ffi::c_void),
    pub protection_open: unsafe extern "C" fn(
        path: XpAppText,
        error: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> *mut core::ffi::c_void,
    pub protection_close: unsafe extern "C" fn(protection: *mut core::ffi::c_void),
    pub protection_encrypted:
        unsafe extern "C" fn(protection: *mut core::ffi::c_void, path: XpAppText) -> u8,
    pub protection_size:
        unsafe extern "C" fn(protection: *mut core::ffi::c_void, path: XpAppText) -> u32,
    pub protection_stream: unsafe extern "C" fn(
        protection: *mut core::ffi::c_void,
        path: XpAppText,
        sink: xp_core::book::io::Sink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub file_seek: unsafe extern "C" fn(file: *mut core::ffi::c_void, offset: u32) -> u8,
    pub file_size: unsafe extern "C" fn(file: *mut core::ffi::c_void) -> u32,
    pub path_remove_tree: unsafe extern "C" fn(path: XpAppText) -> u8,

    pub progress_save: unsafe extern "C" fn(XpAppText, *const u8, u32) -> u8,
    pub paint_dither: unsafe extern "C" fn(x: i32, y: i32, width: i32, height: i32),
    pub paint_pixel: unsafe extern "C" fn(x: i32, y: i32),
    pub paint_status: unsafe extern "C" fn(status: *const XpEpubStatus),
    pub image_clear_failures: unsafe extern "C" fn(),
    pub image_release_cache: unsafe extern "C" fn(),
    pub fonts_ensure_loaded: unsafe extern "C" fn(),
    #[cfg(feature = "app-test")]
    pub fonts_release_caches: unsafe extern "C" fn(),
    #[cfg(feature = "app-test")]
    pub fonts_prewarm_begin:
        unsafe extern "C" fn(storage: *mut super::render_resources::FontScan) -> u8,
    #[cfg(feature = "app-test")]
    pub fonts_prewarm_finish_scan:
        unsafe extern "C" fn(storage: *mut super::render_resources::FontScan),
    #[cfg(feature = "app-test")]
    pub fonts_prewarm_drop: unsafe extern "C" fn(storage: *mut super::render_resources::FontScan),
    pub fonts_take_corrupt_notice: unsafe extern "C" fn() -> u8,
    pub font_sizes: unsafe extern "C" fn(
        family: XpAppText,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub settings_snapshot: unsafe extern "C" fn(resolve_font: u8, out: *mut XpEpubSettings),
    pub settings_render_spec:
        unsafe extern "C" fn(width: u16, height: u16, out: *mut XpEpubRenderSpec),
    pub settings_orientation: unsafe extern "C" fn(value: u8),
    pub settings_font_size: unsafe extern "C" fn(value: u8),
    pub settings_line_spacing: unsafe extern "C" fn(value: u8),
    pub settings_alignment: unsafe extern "C" fn(value: u8),
    pub settings_focus_reading: unsafe extern "C" fn(value: u8),
    pub settings_inversion: unsafe extern "C" fn(value: u8),
    pub settings_ble_enabled: unsafe extern "C" fn(value: u8),
    pub settings_save: unsafe extern "C" fn(),
    pub input_services: unsafe extern "C" fn() -> *const XpUiReaderInputServices,
    pub input_facts: unsafe extern "C" fn(out: *mut XpEpubInputFacts),
    pub input_pressed: unsafe extern "C" fn(button: u32) -> u8,
    pub input_screen_point: unsafe extern "C" fn(kind: u8, x: *mut i32, y: *mut i32) -> u8,
    pub input_screen_tap: unsafe extern "C" fn(x: *mut i32, y: *mut i32) -> u8,
    pub input_raw_contact: unsafe extern "C" fn() -> u8,
    pub render_busy: unsafe extern "C" fn() -> u8,
    pub file_open_read:
        unsafe extern "C" fn(path: XpAppText, origin: XpAppText) -> *mut core::ffi::c_void,
    pub file_open_write:
        unsafe extern "C" fn(path: XpAppText, origin: XpAppText) -> *mut core::ffi::c_void,
    pub file_read:
        unsafe extern "C" fn(file: *mut core::ffi::c_void, bytes: *mut u8, length: u32) -> i32,
    pub file_write:
        unsafe extern "C" fn(file: *mut core::ffi::c_void, bytes: *const u8, length: u32) -> u32,
    pub file_close: unsafe extern "C" fn(file: *mut core::ffi::c_void) -> u8,
    pub path_remove: unsafe extern "C" fn(path: XpAppText) -> u8,
    pub path_rename: unsafe extern "C" fn(r#from: XpAppText, to: XpAppText) -> u8,
    pub path_mkdir: unsafe extern "C" fn(path: XpAppText) -> u8,
    pub native_path_hash: unsafe extern "C" fn(path: XpAppText) -> u64,
    pub heap_snapshot: unsafe extern "C" fn(out: *mut XpEpubHeap),
    pub radio_status: unsafe extern "C" fn(out: *mut XpEpubRadio, peer_or_null: *mut XpEpubPeer),
    pub radio_before_build: unsafe extern "C" fn() -> u8,
    pub radio_settle_start: unsafe extern "C" fn(),
    pub radio_holds_heap: unsafe extern "C" fn() -> u8,
    pub radio_after_paint: unsafe extern "C" fn(),
    pub radio_switch_off: unsafe extern "C" fn(),
    pub radio_request_connect: unsafe extern "C" fn(),
    pub trusted_now: unsafe extern "C" fn() -> i64,
    pub trusted_sync: unsafe extern "C" fn(timeout_ms: u32) -> u8,
    pub wifi_connected: unsafe extern "C" fn() -> u8,
    pub wifi_disconnect: unsafe extern "C" fn(off: u8),
    pub restart_reader: unsafe extern "C" fn(),
    pub child_text_settings: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub child_selection: unsafe extern "C" fn(
        pages: *mut XpEpubOwnedPage,
        count: u8,
        args: *const XpEpubSelectionArgs,
    ) -> *mut core::ffi::c_void,

    pub child_wifi: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub child_sync: unsafe extern "C" fn(
        path: XpAppText,
        position: *const XpEpubPosition,
        saved: *const XpEpubSavedProgress,
        chapter: XpAppText,
        events_only: u8,
    ) -> *mut core::ffi::c_void,
    pub popup_open: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub popup_close: unsafe extern "C" fn(popup: *mut core::ffi::c_void),
    pub popup_active: unsafe extern "C" fn(popup: *mut core::ffi::c_void) -> u8,
    // Labels are borrowed synchronously; choice and its context remain valid for the popup callback.
    pub popup_show: unsafe extern "C" fn(
        popup: *mut core::ffi::c_void,
        title: XpAppText,
        message: XpAppText,
        count: u32,
        selected: i32,
        label: unsafe extern "C" fn(
            label_context: *mut core::ffi::c_void,
            index: u32,
            sink: XpAppTextSink,
            sink_context: *mut core::ffi::c_void,
        ) -> u8,
        label_context: *mut core::ffi::c_void,
        choose: unsafe extern "C" fn(choice_context: *mut core::ffi::c_void, index: i32),
        choice_context: *mut core::ffi::c_void,
    ),
    pub popup_dismiss: unsafe extern "C" fn(popup: *mut core::ffi::c_void),
    pub popup_input:
        unsafe extern "C" fn(popup: *mut core::ffi::c_void, out: *mut XpEpubPopupInput),
    pub popup_paint: unsafe extern "C" fn(popup: *mut core::ffi::c_void),
    pub popup_process_render: unsafe extern "C" fn(popup: *mut core::ffi::c_void),
    pub end_open: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub end_close: unsafe extern "C" fn(r#end: *mut core::ffi::c_void),
    pub end_load: unsafe extern "C" fn(r#end: *mut core::ffi::c_void, path: XpAppText),
    pub end_menu_active: unsafe extern "C" fn(r#end: *mut core::ffi::c_void) -> u8,
    pub end_input: unsafe extern "C" fn(
        r#end: *mut core::ffi::c_void,
        result: unsafe extern "C" fn(*mut core::ffi::c_void, u8, XpAppText),
        context: *mut core::ffi::c_void,
    ),
    pub end_paint: unsafe extern "C" fn(r#end: *mut core::ffi::c_void),

    pub recents_remove: unsafe extern "C" fn(path: XpAppText) -> u8,
    pub recents_add: unsafe extern "C" fn(book: *const app_abi::Book),
    pub recents_update_path: unsafe extern "C" fn(
        r#from: XpAppText,
        to: XpAppText,
        old_cache: XpAppText,
        new_cache: XpAppText,
    ),
    pub state_open_path:
        unsafe extern "C" fn(sink: XpAppTextSink, context: *mut core::ffi::c_void) -> u8,
    pub state_set_open_path: unsafe extern "C" fn(path: XpAppText),
    pub state_reset_reader_count: unsafe extern "C" fn(),
    pub state_save: unsafe extern "C" fn(),
    pub has_sync_credentials: unsafe extern "C" fn() -> u8,
    pub has_exit_subscriber: unsafe extern "C" fn() -> u8,
    pub has_session_subscriber: unsafe extern "C" fn() -> u8,
    pub document_id: unsafe extern "C" fn(
        path: XpAppText,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub emit_open: unsafe extern "C" fn(path: XpAppText),
    pub emit_exit: unsafe extern "C" fn(path: XpAppText, percent: i32),
    pub emit_session: unsafe extern "C" fn(
        path: XpAppText,
        document: XpAppText,
        event: *const XpEpubSessionEvent,
    ),
    pub log: unsafe extern "C" fn(severity: u8, origin: XpAppText, message: XpAppText),
    pub font_text_advance:
        unsafe extern "C" fn(text: XpAppText, font: i32, style: u8, character_spacing: i8) -> i32,
    pub log_enabled: unsafe extern "C" fn(severity: u8) -> u8,
    pub input_snapshot: unsafe extern "C" fn(out: *mut xp_ui::model::Input),
    pub cache_lock: unsafe extern "C" fn(),
    pub cache_unlock: unsafe extern "C" fn(),
    pub scratch_claim: unsafe extern "C" fn(min_len: u32) -> *mut u8,
    pub scratch_release: unsafe extern "C" fn(bytes: *const u8),
    pub inflate_oom_log: unsafe extern "C" fn(allocation: XpAppText, bytes: u32),
    pub build_metrics: unsafe extern "C" fn() -> *const xp_core::para::paragraph::Metrics,
    pub build_allocator: unsafe extern "C" fn() -> *const xp_core::para::Allocator,
    pub font_variant: unsafe extern "C" fn(i32, f32) -> i32,
    pub font_line_height: unsafe extern "C" fn(i32, f32) -> i32,
    pub font_ready: unsafe extern "C" fn(i32, i32) -> i32,
    pub font_codepoint: unsafe extern "C" fn(i32, u32, u8, *mut i32, *mut i32) -> u8,
    pub hyphen_language: unsafe extern "C" fn(XpAppText) -> i32,
    pub file_position: unsafe extern "C" fn(*mut core::ffi::c_void) -> u32,
    pub file_flush: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub image_supported: unsafe extern "C" fn(XpAppText) -> u8,
    pub image_dimensions: unsafe extern "C" fn(XpAppText, *mut u16, *mut u16) -> u8,
    pub framebuffer_lend: unsafe extern "C" fn() -> u8,
    pub framebuffer_restore: unsafe extern "C" fn(u8),
    pub image_needs_decode: unsafe extern "C" fn(XpAppText, XpAppText, i16, i16) -> u8,
    pub font_ascent: unsafe extern "C" fn(i32) -> i32,
    pub render_physical_query: unsafe extern "C" fn(u8) -> u32,
    pub render_physical_operation:
        unsafe extern "C" fn(*const super::render_resources::physical::Op) -> u32,
    pub image_read_window: unsafe extern "C" fn(*mut u8, u32),
    pub display_source: unsafe extern "C" fn(*mut crate::display_source::DisplaySource) -> u8,
    pub framebuffer_view: unsafe extern "C" fn(*mut u32) -> *mut u8,
    pub child_drawing: super::children::Physical,
    pub child_selection_physical: super::children::selection_physical::Physical,
    pub clipping_physical: *const crate::app::clippings_native::Physical,
    pub child_list_help: unsafe extern "C" fn(xp_ui::scene::Rect, XpAppText),
    pub child_list_icon: unsafe extern "C" fn(u8, xp_ui::scene::Rect, xp_ui::scene::Rect, u8),
    pub local_calendar: unsafe extern "C" fn(*mut crate::app::bookmarks::export::Calendar) -> u8,
    pub storage_read_file:
        unsafe extern "C" fn(XpAppText, super::app_abi::TextSink, *mut core::ffi::c_void) -> u8,
    pub storage_write_file_atomic: unsafe extern "C" fn(XpAppText, *const u8, u32) -> u8,
}

const _: () = {
    assert!(core::mem::size_of::<XpEpubElement>() == 12);
    assert!(core::mem::align_of::<XpEpubElement>() == 4);
    assert!(core::mem::offset_of!(XpEpubElement, words) == 8);
    assert!(core::mem::offset_of!(XpEpubElement, tag) == 10);
    assert!(core::mem::offset_of!(XpEpubElement, valid) == 11);
    assert!(core::mem::size_of::<XpEpubWord>() == core::mem::size_of::<XpAppText>() + 16);
    assert!(core::mem::offset_of!(XpEpubWord, x) == core::mem::size_of::<XpAppText>());
    assert!(
        core::mem::offset_of!(XpEpubWord, source_start) == core::mem::size_of::<XpAppText>() + 4
    );
    assert!(core::mem::offset_of!(XpEpubWord, source_end) == core::mem::size_of::<XpAppText>() + 8);
    assert!(core::mem::offset_of!(XpEpubWord, style) == core::mem::size_of::<XpAppText>() + 12);
    assert!(
        core::mem::offset_of!(XpEpubWord, character_spacing)
            == core::mem::size_of::<XpAppText>() + 13
    );
    assert!(
        core::mem::size_of::<XpEpubLeaves>()
            == leaf_index(168) * core::mem::size_of::<unsafe extern "C" fn()>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, log)
            == leaf_index(101) * core::mem::size_of::<unsafe extern "C" fn()>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, font_text_advance)
            == leaf_index(102) * core::mem::size_of::<unsafe extern "C" fn()>()
    );
};

const _: fn(&XpEpubLeaves) = |leaves| {
    let _: unsafe extern "C" fn(
        *mut core::ffi::c_void,
        unsafe extern "C" fn(*mut core::ffi::c_void, u8, XpAppText),
        *mut core::ffi::c_void,
    ) = leaves.end_input;
    let _: unsafe extern "C" fn(*const app_abi::Book) = leaves.recents_add;
    let _: unsafe extern "C" fn(XpAppText, i32, u8, i8) -> i32 = leaves.font_text_advance;
};

const _: () = {
    assert!(
        core::mem::offset_of!(XpEpubLeaves, log_enabled)
            == leaf_index(103) * core::mem::size_of::<unsafe extern "C" fn()>()
    );
};
const _: fn(&XpEpubLeaves) = |leaves| {
    let _: unsafe extern "C" fn(u8) -> u8 = leaves.log_enabled;
};

const _: () = {
    assert!(core::mem::offset_of!(XpEpubSettings, orientation) == 4);
    assert!(core::mem::offset_of!(XpEpubSettings, dictionary_available) == 26);
    assert!(core::mem::offset_of!(XpEpubSettings, refresh_frequency) == 27);
    assert!(
        core::mem::offset_of!(XpEpubSettings, font_family)
            == if core::mem::size_of::<usize>() == 8 {
                32
            } else {
                28
            }
    );
    assert!(
        core::mem::size_of::<XpEpubSettings>()
            == if core::mem::size_of::<usize>() == 8 {
                48
            } else {
                36
            }
    );
};

const _: () = {
    assert!(core::mem::size_of::<XpEpubRadio>() == 5);
    assert!(core::mem::align_of::<XpEpubRadio>() == 1);
    assert!(core::mem::offset_of!(XpEpubRadio, note) == 4);
    assert!(core::mem::size_of::<XpEpubPeer>() == 82);
    assert!(core::mem::align_of::<XpEpubPeer>() == 1);
    assert!(core::mem::offset_of!(XpEpubPeer, name) == 0);
    assert!(core::mem::offset_of!(XpEpubPeer, addr) == 64);
    assert!(
        core::mem::offset_of!(XpEpubLeaves, radio_status)
            == leaf_index(57) * core::mem::size_of::<unsafe extern "C" fn()>()
    );
};
const _: fn(&XpEpubLeaves) = |leaves| {
    let _: unsafe extern "C" fn(*mut XpEpubRadio, *mut XpEpubPeer) = leaves.radio_status;
};

const _: () = {
    assert!(core::mem::size_of::<XpEpubToolbarRow>() == 2 * core::mem::size_of::<XpAppText>() + 8);
    assert!(
        core::mem::offset_of!(XpEpubToolbarRow, label_spaces)
            == 2 * core::mem::size_of::<XpAppText>()
    );
    assert!(
        core::mem::offset_of!(XpEpubToolbarRow, toggle)
            == 2 * core::mem::size_of::<XpAppText>() + 4
    );
    assert!(
        core::mem::offset_of!(XpEpubToolbarRow, checked)
            == 2 * core::mem::size_of::<XpAppText>() + 5
    );
};

const _: () = {
    assert!(
        core::mem::offset_of!(XpEpubLeaves, input_snapshot)
            == leaf_index(104) * core::mem::size_of::<unsafe extern "C" fn()>()
    );
};
const _: fn(&XpEpubLeaves) = |leaves| {
    let _: unsafe extern "C" fn(*mut xp_ui::model::Input) = leaves.input_snapshot;
};

const _: () = {
    assert!(core::mem::size_of::<XpEpubGeometry>() == 32);
    assert!(core::mem::align_of::<XpEpubGeometry>() == 4);
    assert!(core::mem::offset_of!(XpEpubGeometry, has_font_cache) == 31);
};

const _: () = {
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_open)
            == leaf_index(9) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_close)
            == leaf_index(10) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_encrypted)
            == leaf_index(11) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_size)
            == leaf_index(12) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_stream)
            == leaf_index(13) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, file_seek)
            == leaf_index(14) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, file_size)
            == leaf_index(15) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, path_remove_tree)
            == leaf_index(16) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, cache_lock)
            == leaf_index(105) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, cache_unlock)
            == leaf_index(106) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, scratch_claim)
            == leaf_index(107) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, scratch_release)
            == leaf_index(108) * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, inflate_oom_log)
            == leaf_index(109) * core::mem::size_of::<usize>()
    );
};

const _: () = {
    let pointer = core::mem::size_of::<unsafe extern "C" fn()>();
    assert!(core::mem::offset_of!(XpEpubLeaves, render_margins) == 0);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_geometry) == pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_restore_bw) == leaf_index(2) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_discard_bw) == leaf_index(3) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_orientation) == leaf_index(4) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, loan_count) == leaf_index(5) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, book_facade_open) == leaf_index(6) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, book_facade_close) == leaf_index(7) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, book_set_extractor) == leaf_index(8) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, protection_open) == leaf_index(9) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, protection_close) == leaf_index(10) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, protection_encrypted) == leaf_index(11) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, protection_size) == leaf_index(12) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, protection_stream) == leaf_index(13) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_seek) == leaf_index(14) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_size) == leaf_index(15) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, path_remove_tree) == leaf_index(16) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, progress_save) == leaf_index(17) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, paint_dither) == leaf_index(18) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, paint_pixel) == leaf_index(19) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, paint_status) == leaf_index(20) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_clear_failures) == leaf_index(21) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_release_cache) == leaf_index(22) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, fonts_ensure_loaded) == leaf_index(23) * pointer);
    #[cfg(feature = "app-test")]
    assert!(core::mem::offset_of!(XpEpubLeaves, fonts_release_caches) == leaf_index(24) * pointer);
    #[cfg(feature = "app-test")]
    assert!(core::mem::offset_of!(XpEpubLeaves, fonts_prewarm_begin) == leaf_index(25) * pointer);
    #[cfg(feature = "app-test")]
    assert!(
        core::mem::offset_of!(XpEpubLeaves, fonts_prewarm_finish_scan) == leaf_index(26) * pointer
    );
    #[cfg(feature = "app-test")]
    assert!(core::mem::offset_of!(XpEpubLeaves, fonts_prewarm_drop) == leaf_index(27) * pointer);
    assert!(
        core::mem::offset_of!(XpEpubLeaves, fonts_take_corrupt_notice) == leaf_index(28) * pointer
    );
    assert!(core::mem::offset_of!(XpEpubLeaves, font_sizes) == leaf_index(29) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_snapshot) == leaf_index(30) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_render_spec) == leaf_index(31) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_orientation) == leaf_index(32) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_font_size) == leaf_index(33) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_line_spacing) == leaf_index(34) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_alignment) == leaf_index(35) * pointer);
    assert!(
        core::mem::offset_of!(XpEpubLeaves, settings_focus_reading) == leaf_index(36) * pointer
    );
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_inversion) == leaf_index(37) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_ble_enabled) == leaf_index(38) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_save) == leaf_index(39) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_services) == leaf_index(40) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_facts) == leaf_index(41) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_pressed) == leaf_index(42) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_screen_point) == leaf_index(43) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_screen_tap) == leaf_index(44) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_raw_contact) == leaf_index(45) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_busy) == leaf_index(46) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_open_read) == leaf_index(47) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_open_write) == leaf_index(48) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_read) == leaf_index(49) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_write) == leaf_index(50) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_close) == leaf_index(51) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, path_remove) == leaf_index(52) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, path_rename) == leaf_index(53) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, path_mkdir) == leaf_index(54) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, native_path_hash) == leaf_index(55) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, heap_snapshot) == leaf_index(56) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_status) == leaf_index(57) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_before_build) == leaf_index(58) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_settle_start) == leaf_index(59) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_holds_heap) == leaf_index(60) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_after_paint) == leaf_index(61) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_switch_off) == leaf_index(62) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_request_connect) == leaf_index(63) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, trusted_now) == leaf_index(64) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, trusted_sync) == leaf_index(65) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, wifi_connected) == leaf_index(66) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, wifi_disconnect) == leaf_index(67) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, restart_reader) == leaf_index(68) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_text_settings) == leaf_index(69) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_selection) == leaf_index(70) * pointer);

    assert!(core::mem::offset_of!(XpEpubLeaves, child_wifi) == leaf_index(71) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_sync) == leaf_index(72) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_open) == leaf_index(73) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_close) == leaf_index(74) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_active) == leaf_index(75) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_show) == leaf_index(76) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_dismiss) == leaf_index(77) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_input) == leaf_index(78) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_paint) == leaf_index(79) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_process_render) == leaf_index(80) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_open) == leaf_index(81) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_close) == leaf_index(82) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_load) == leaf_index(83) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_menu_active) == leaf_index(84) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_input) == leaf_index(85) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_paint) == leaf_index(86) * pointer);

    assert!(core::mem::offset_of!(XpEpubLeaves, recents_remove) == leaf_index(87) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, recents_add) == leaf_index(88) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, recents_update_path) == leaf_index(89) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, state_open_path) == leaf_index(90) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, state_set_open_path) == leaf_index(91) * pointer);
    assert!(
        core::mem::offset_of!(XpEpubLeaves, state_reset_reader_count) == leaf_index(92) * pointer
    );
    assert!(core::mem::offset_of!(XpEpubLeaves, state_save) == leaf_index(93) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, has_sync_credentials) == leaf_index(94) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, has_exit_subscriber) == leaf_index(95) * pointer);
    assert!(
        core::mem::offset_of!(XpEpubLeaves, has_session_subscriber) == leaf_index(96) * pointer
    );
    assert!(core::mem::offset_of!(XpEpubLeaves, document_id) == leaf_index(97) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, emit_open) == leaf_index(98) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, emit_exit) == leaf_index(99) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, emit_session) == leaf_index(100) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, log) == leaf_index(101) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_text_advance) == leaf_index(102) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, log_enabled) == leaf_index(103) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_snapshot) == leaf_index(104) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, cache_lock) == leaf_index(105) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, cache_unlock) == leaf_index(106) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, scratch_claim) == leaf_index(107) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, scratch_release) == leaf_index(108) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, inflate_oom_log) == leaf_index(109) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, build_metrics) == leaf_index(110) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, build_allocator) == leaf_index(111) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_variant) == leaf_index(112) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_line_height) == leaf_index(113) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_ready) == leaf_index(114) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_codepoint) == leaf_index(115) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, hyphen_language) == leaf_index(116) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_position) == leaf_index(117) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_flush) == leaf_index(118) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_supported) == leaf_index(119) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_dimensions) == leaf_index(120) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, framebuffer_lend) == leaf_index(121) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, framebuffer_restore) == leaf_index(122) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_needs_decode) == leaf_index(123) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_ascent) == leaf_index(124) * pointer);
    assert!(
        core::mem::offset_of!(XpEpubLeaves, render_physical_query) == leaf_index(125) * pointer
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, render_physical_operation) == leaf_index(126) * pointer
    );
    assert!(core::mem::offset_of!(XpEpubLeaves, image_read_window) == leaf_index(127) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, display_source) == leaf_index(128) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, framebuffer_view) == leaf_index(129) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_drawing) == leaf_index(130) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, clipping_physical) == leaf_index(162) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_list_help) == leaf_index(163) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_list_icon) == leaf_index(164) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, local_calendar) == leaf_index(165) * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, storage_read_file) == leaf_index(166) * pointer);
    assert!(
        core::mem::offset_of!(XpEpubLeaves, storage_write_file_atomic) == leaf_index(167) * pointer
    );

    assert!(
        core::mem::align_of::<XpEpubLeaves>() == core::mem::align_of::<unsafe extern "C" fn()>()
    );
};

const _: () = assert!(
    core::mem::offset_of!(XpEpubLeaves, child_selection_physical)
        == leaf_index(138) * core::mem::size_of::<usize>()
);

// The five native policy slots exist only for frozen host oracle fixtures.
const fn leaf_index(index: usize) -> usize {
    if cfg!(feature = "app-test") {
        index
    } else {
        index - if index >= 28 { 4 } else { 0 } - if index >= 137 { 1 } else { 0 }
    }
}
