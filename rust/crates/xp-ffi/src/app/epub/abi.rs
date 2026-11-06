// Evidence draft: import existing ABI types at the platform boundary.
use super::super::abi::{Text as XpAppText, TextSink as XpAppTextSink, XpAppNativeText};
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
pub struct XpEpubClip {
    pub spine: u16,
    pub start_page: u16,
    pub end_page: u16,
    pub page_count: u16,
    pub start_word: u16,
    pub end_word: u16,
    pub signature: u32,
    pub start_offset: u32,
    pub end_offset: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubClipInsert {
    pub range: XpEpubClip,
    pub word_count: u16,
    pub paragraph: u16,
    pub chapter: XpAppText,
    pub text: XpAppNativeText,
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
pub type XpEpubSavedProgressSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubSavedProgress) -> u8;
pub type XpEpubBookmarkSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubBookmark) -> u8;
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
    pub progress_to_reader: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        saved: *const XpEpubSavedProgress,
        spine: i32,
        count: i32,
        out: *mut XpEpubPosition,
    ),
    pub progress_to_saved: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        position: *const XpEpubPosition,
        sink: XpEpubSavedProgressSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub progress_save:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, position: *const XpEpubPosition) -> u8,
    pub paint_dither: unsafe extern "C" fn(x: i32, y: i32, width: i32, height: i32),
    pub paint_pixel: unsafe extern "C" fn(x: i32, y: i32),
    pub paint_status: unsafe extern "C" fn(status: *const XpEpubStatus),
    pub image_clear_failures: unsafe extern "C" fn(),
    pub image_release_cache: unsafe extern "C" fn(),
    pub fonts_ensure_loaded: unsafe extern "C" fn(),
    pub fonts_release_caches: unsafe extern "C" fn(),
    pub fonts_prewarm_begin:
        unsafe extern "C" fn(storage: *mut super::render_resources::PrewarmStorage) -> u8,
    pub fonts_prewarm_finish_scan:
        unsafe extern "C" fn(storage: *mut super::render_resources::PrewarmStorage),
    pub fonts_prewarm_drop:
        unsafe extern "C" fn(storage: *mut super::render_resources::PrewarmStorage),
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
    pub input_screen_long: unsafe extern "C" fn(x: *mut i32, y: *mut i32) -> u8,
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
    pub child_bookmarks: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        path: XpAppText,
    ) -> *mut core::ffi::c_void,
    pub child_clippings: unsafe extern "C" fn() -> *mut core::ffi::c_void,
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
    pub bookmarks_open: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub bookmarks_close: unsafe extern "C" fn(bookmarks: *mut core::ffi::c_void),
    pub bookmarks_clear: unsafe extern "C" fn(bookmarks: *mut core::ffi::c_void),
    pub bookmarks_load:
        unsafe extern "C" fn(bookmarks: *mut core::ffi::c_void, path: XpAppText) -> u8,
    pub bookmarks_save:
        unsafe extern "C" fn(bookmarks: *mut core::ffi::c_void, path: XpAppText) -> u8,
    pub bookmarks_count: unsafe extern "C" fn(bookmarks: *mut core::ffi::c_void) -> u32,
    pub bookmarks_row: unsafe extern "C" fn(
        bookmarks: *mut core::ffi::c_void,
        index: u32,
        sink: XpEpubBookmarkSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub bookmarks_retain: unsafe extern "C" fn(
        bookmarks: *mut core::ffi::c_void,
        keep: unsafe extern "C" fn(*mut core::ffi::c_void, *const XpEpubBookmark) -> u8,
        context: *mut core::ffi::c_void,
    ) -> u32,
    pub bookmarks_insert_front:
        unsafe extern "C" fn(bookmarks: *mut core::ffi::c_void, row: *const XpEpubBookmark) -> u8,
    pub clippings_load: unsafe extern "C" fn(book: *const app_abi::Book) -> u8,
    pub clippings_unload: unsafe extern "C" fn(),
    pub clippings_count: unsafe extern "C" fn() -> u32,
    pub clippings_row: unsafe extern "C" fn(index: u32, out: *mut XpEpubClip),
    pub clippings_remove: unsafe extern "C" fn(index: u32) -> u8,
    pub clippings_insert: unsafe extern "C" fn(clipping: *const XpEpubClipInsert) -> u8,
    pub clippings_export: unsafe extern "C" fn(
        title: XpAppText,
        author: XpAppText,
        chapter: XpAppText,
        percent: i32,
        text: XpAppNativeText,
    ) -> u8,
    pub clippings_move_book: unsafe extern "C" fn(r#from: XpAppText, to: XpAppText) -> u8,
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
    pub paint_arena: unsafe extern "C" fn(*const XpEpubPageView, i32, i32, i32, u8),
    pub image_needs_decode: unsafe extern "C" fn(XpAppText, XpAppText, i16, i16) -> u8,
    pub font_ascent: unsafe extern "C" fn(i32) -> i32,
    pub render_physical_query: unsafe extern "C" fn(u8) -> u32,
    pub render_physical_operation:
        unsafe extern "C" fn(*const super::render_resources::physical::Op) -> u32,
    pub render_plan: unsafe extern "C" fn(*mut crate::page_render::Plan, *mut u8, u16, u8),
    pub image_read_window: unsafe extern "C" fn(*mut u8, u32),
    pub display_source: unsafe extern "C" fn(*mut crate::display_source::DisplaySource) -> u8,
    pub framebuffer_view: unsafe extern "C" fn(*mut u32) -> *mut u8,
    pub child_drawing: super::children::Physical,
    pub child_selection_physical: super::children::selection_physical::Physical,
    pub clipping_physical: *const crate::app::clippings_native::Physical,
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
            == 174 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, log)
            == 122 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, font_text_advance)
            == 123 * core::mem::size_of::<unsafe extern "C" fn()>()
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
            == 124 * core::mem::size_of::<unsafe extern "C" fn()>()
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
            == 59 * core::mem::size_of::<unsafe extern "C" fn()>()
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
            == 125 * core::mem::size_of::<unsafe extern "C" fn()>()
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
        core::mem::offset_of!(XpEpubLeaves, protection_open) == 9 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_close) == 10 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_encrypted)
            == 11 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_size) == 12 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_stream)
            == 13 * core::mem::size_of::<usize>()
    );
    assert!(core::mem::offset_of!(XpEpubLeaves, file_seek) == 14 * core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(XpEpubLeaves, file_size) == 15 * core::mem::size_of::<usize>());
    assert!(
        core::mem::offset_of!(XpEpubLeaves, path_remove_tree) == 16 * core::mem::size_of::<usize>()
    );
    assert!(core::mem::offset_of!(XpEpubLeaves, cache_lock) == 126 * core::mem::size_of::<usize>());
    assert!(
        core::mem::offset_of!(XpEpubLeaves, cache_unlock) == 127 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, scratch_claim) == 128 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, scratch_release) == 129 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, inflate_oom_log) == 130 * core::mem::size_of::<usize>()
    );
};

const _: () = {
    let pointer = core::mem::size_of::<unsafe extern "C" fn()>();
    assert!(core::mem::offset_of!(XpEpubLeaves, render_margins) == 0);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_geometry) == pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_restore_bw) == 2 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_discard_bw) == 3 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_orientation) == 4 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, loan_count) == 5 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, book_facade_open) == 6 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, book_facade_close) == 7 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, book_set_extractor) == 8 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, protection_open) == 9 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, protection_close) == 10 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, protection_encrypted) == 11 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, protection_size) == 12 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, protection_stream) == 13 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_seek) == 14 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_size) == 15 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, path_remove_tree) == 16 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, progress_to_reader) == 17 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, progress_to_saved) == 18 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, progress_save) == 19 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, paint_dither) == 20 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, paint_pixel) == 21 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, paint_status) == 22 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_clear_failures) == 23 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_release_cache) == 24 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, fonts_ensure_loaded) == 25 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, fonts_release_caches) == 26 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, fonts_prewarm_begin) == 27 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, fonts_prewarm_finish_scan) == 28 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, fonts_prewarm_drop) == 29 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, fonts_take_corrupt_notice) == 30 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_sizes) == 31 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_snapshot) == 32 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_render_spec) == 33 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_orientation) == 34 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_font_size) == 35 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_line_spacing) == 36 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_alignment) == 37 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_focus_reading) == 38 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_inversion) == 39 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_ble_enabled) == 40 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, settings_save) == 41 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_services) == 42 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_facts) == 43 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_pressed) == 44 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_screen_long) == 45 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_screen_tap) == 46 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_raw_contact) == 47 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_busy) == 48 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_open_read) == 49 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_open_write) == 50 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_read) == 51 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_write) == 52 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_close) == 53 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, path_remove) == 54 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, path_rename) == 55 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, path_mkdir) == 56 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, native_path_hash) == 57 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, heap_snapshot) == 58 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_status) == 59 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_before_build) == 60 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_settle_start) == 61 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_holds_heap) == 62 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_after_paint) == 63 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_switch_off) == 64 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, radio_request_connect) == 65 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, trusted_now) == 66 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, trusted_sync) == 67 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, wifi_connected) == 68 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, wifi_disconnect) == 69 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, restart_reader) == 70 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_text_settings) == 71 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_selection) == 72 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_bookmarks) == 73 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_clippings) == 74 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_wifi) == 75 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_sync) == 76 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_open) == 77 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_close) == 78 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_active) == 79 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_show) == 80 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_dismiss) == 81 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_input) == 82 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_paint) == 83 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, popup_process_render) == 84 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_open) == 85 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_close) == 86 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_load) == 87 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_menu_active) == 88 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_input) == 89 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, end_paint) == 90 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, bookmarks_open) == 91 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, bookmarks_close) == 92 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, bookmarks_clear) == 93 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, bookmarks_load) == 94 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, bookmarks_save) == 95 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, bookmarks_count) == 96 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, bookmarks_row) == 97 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, bookmarks_retain) == 98 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, bookmarks_insert_front) == 99 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, clippings_load) == 100 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, clippings_unload) == 101 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, clippings_count) == 102 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, clippings_row) == 103 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, clippings_remove) == 104 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, clippings_insert) == 105 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, clippings_export) == 106 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, clippings_move_book) == 107 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, recents_remove) == 108 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, recents_add) == 109 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, recents_update_path) == 110 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, state_open_path) == 111 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, state_set_open_path) == 112 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, state_reset_reader_count) == 113 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, state_save) == 114 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, has_sync_credentials) == 115 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, has_exit_subscriber) == 116 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, has_session_subscriber) == 117 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, document_id) == 118 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, emit_open) == 119 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, emit_exit) == 120 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, emit_session) == 121 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, log) == 122 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_text_advance) == 123 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, log_enabled) == 124 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, input_snapshot) == 125 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, cache_lock) == 126 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, cache_unlock) == 127 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, scratch_claim) == 128 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, scratch_release) == 129 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, inflate_oom_log) == 130 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, build_metrics) == 131 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, build_allocator) == 132 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_variant) == 133 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_line_height) == 134 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_ready) == 135 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_codepoint) == 136 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, hyphen_language) == 137 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_position) == 138 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, file_flush) == 139 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_supported) == 140 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_dimensions) == 141 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, framebuffer_lend) == 142 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, framebuffer_restore) == 143 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, paint_arena) == 144 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_needs_decode) == 145 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, font_ascent) == 146 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_physical_query) == 147 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_physical_operation) == 148 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, render_plan) == 149 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, image_read_window) == 150 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, display_source) == 151 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, framebuffer_view) == 152 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, child_drawing) == 153 * pointer);
    assert!(core::mem::offset_of!(XpEpubLeaves, clipping_physical) == 173 * pointer);
    assert!(
        core::mem::align_of::<XpEpubLeaves>() == core::mem::align_of::<unsafe extern "C" fn()>()
    );
};

const _: () = assert!(
    core::mem::offset_of!(XpEpubLeaves, child_selection_physical)
        == 161 * core::mem::size_of::<usize>()
);
