// Evidence draft: import existing ABI types at the platform boundary.
use super::super::abi::{Text as XpAppText, TextSink as XpAppTextSink, XpAppNativeText};
use xp_ui::reader::{
    input::Services as XpUiReaderInputServices, render::Services as XpUiReaderRenderServices,
};

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
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubToc {
    pub title: XpAppText,
    pub anchor: XpAppText,
    pub spine: i32,
    pub level: i32,
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
    pub font_family: XpAppText,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubPageInfo {
    pub visible_offset: u32,
    pub elements: u16,
    pub footnotes: u16,
    pub links: u16,
    pub images: u8,
    pub images_need_decode: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubElement {
    pub x: i32,
    pub y: i32,
    pub words: u16,
    pub tag: u8,
    pub valid: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubWord {
    pub text: XpAppText,
    pub x: i32,
    pub source_start: u32,
    pub source_end: u32,
    pub style: u8,
    pub character_spacing: i8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubFootnote {
    pub text: XpAppText,
    pub href: XpAppText,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubLink {
    pub href: XpAppText,
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
}

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
pub type XpEpubFootnoteSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubFootnote) -> u8;
pub type XpEpubLinkSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubLink) -> u8;
pub type XpEpubBookmarkSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubBookmark) -> u8;
pub type XpEpubToolbarRowSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubToolbarRow) -> u8;
pub type XpEpubBuildPopup = unsafe extern "C" fn(context: *mut core::ffi::c_void);
pub type XpEpubHighlight = unsafe extern "C" fn(
    context: *mut core::ffi::c_void,
    page: *mut core::ffi::c_void,
    font: i32,
    left: i32,
    top: i32,
);
pub type XpEpubRowSource = unsafe extern "C" fn(
    context: *mut core::ffi::c_void,
    index: i32,
    sink: XpEpubToolbarRowSink,
    sink_context: *mut core::ffi::c_void,
) -> u8;

#[repr(C)]
pub struct XpEpubLeaves {
    pub resources_open: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub resources_close: unsafe extern "C" fn(resources: *mut core::ffi::c_void),
    pub render_services:
        unsafe extern "C" fn(resources: *mut core::ffi::c_void) -> *const XpUiReaderRenderServices,
    pub render_bind_page: unsafe extern "C" fn(
        resources: *mut core::ffi::c_void,
        page: *mut core::ffi::c_void,
        font: i32,
        left: i32,
        top: i32,
        bottom: i32,
        highlight: XpEpubHighlight,
        context: *mut core::ffi::c_void,
    ),
    pub render_finish_page: unsafe extern "C" fn(resources: *mut core::ffi::c_void),
    pub render_margins: unsafe extern "C" fn(margins: *mut i32),
    pub render_geometry: unsafe extern "C" fn(out: *mut XpEpubGeometry),
    pub render_restore_bw: unsafe extern "C" fn(resync: u8),
    pub render_discard_bw: unsafe extern "C" fn(),
    pub render_orientation: unsafe extern "C" fn(orientation: u8),
    pub loan_begin: unsafe extern "C" fn(resources: *mut core::ffi::c_void) -> u8,
    pub loan_end: unsafe extern "C" fn(resources: *mut core::ffi::c_void),
    pub loan_drop: unsafe extern "C" fn(resources: *mut core::ffi::c_void),
    pub loan_count: unsafe extern "C" fn() -> u32,
    pub strip_release_idle: unsafe extern "C" fn(resources: *mut core::ffi::c_void) -> u8,
    pub book_open: unsafe extern "C" fn(path: XpAppText) -> *mut core::ffi::c_void,
    pub book_load:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, build_missing: u8, skip_css: u8) -> u8,
    pub book_close: unsafe extern "C" fn(book: *mut core::ffi::c_void),
    pub book_size: unsafe extern "C" fn(book: *mut core::ffi::c_void) -> u32,
    pub book_spine_count: unsafe extern "C" fn(book: *mut core::ffi::c_void) -> i32,
    pub book_toc_count: unsafe extern "C" fn(book: *mut core::ffi::c_void) -> i32,
    pub book_path: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_cache_path: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_title: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_author: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_language: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_spine: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        index: i32,
        sink: XpEpubSpineSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_toc: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        index: i32,
        sink: XpEpubTocSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_toc_for_spine: unsafe extern "C" fn(book: *mut core::ffi::c_void, index: i32) -> i32,
    pub book_cumulative_size: unsafe extern "C" fn(book: *mut core::ffi::c_void, spine: i32) -> u32,
    pub book_spine_for_size: unsafe extern "C" fn(book: *mut core::ffi::c_void, bytes: u32) -> i32,
    pub book_text_reference: unsafe extern "C" fn(book: *mut core::ffi::c_void) -> i32,
    pub book_progress:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, spine: i32, fraction: f32) -> f32,
    pub book_resolve_href:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, href: XpAppText) -> i32,
    pub book_setup_cache: unsafe extern "C" fn(book: *mut core::ffi::c_void),
    pub book_clear_cache: unsafe extern "C" fn(book: *mut core::ffi::c_void),
    pub book_install_extractor: unsafe extern "C" fn(book: *mut core::ffi::c_void),
    pub book_clear_extractor: unsafe extern "C" fn(),
    pub book_protection_error: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
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
    pub section_open:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, spine: i32) -> *mut core::ffi::c_void,
    pub section_close: unsafe extern "C" fn(section: *mut core::ffi::c_void),
    pub section_page_count: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u16,
    pub section_estimated_pages: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> i32,
    pub section_building: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u8,
    pub section_complete: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u8,
    pub section_partial: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u8,
    pub section_load_cache:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, spec: *const XpEpubRenderSpec) -> u8,
    pub section_create_cache: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        spec: *const XpEpubRenderSpec,
        popup: XpEpubBuildPopup,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub section_start_build: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        spec: *const XpEpubRenderSpec,
        popup: Option<XpEpubBuildPopup>,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub section_build_more: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        max_pages: i32,
        yield_on_input: u8,
    ) -> u8,
    pub section_abandon_build: unsafe extern "C" fn(section: *mut core::ffi::c_void),
    pub section_clear_cache: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u8,
    pub section_html_cached: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u8,
    pub section_find_anchor: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        anchor: XpAppText,
        page: *mut u16,
    ) -> u8,
    pub section_cached_anchor: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        anchor: XpAppText,
        page: *mut u16,
    ) -> u8,
    pub section_toc_for_page: unsafe extern "C" fn(
        section_or_null: *mut core::ffi::c_void,
        book: *mut core::ffi::c_void,
        spine: i32,
        page: i32,
    ) -> i32,
    pub section_page_for_offset: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        offset: u32,
        prefer_first: u8,
        page: *mut u16,
    ) -> u8,
    pub section_offset_for_page:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, page: u16, offset: *mut u32) -> u8,
    pub section_paragraph_for_page:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, page: u16, paragraph: *mut u16) -> u8,
    pub section_reached_offset:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, offset: u32) -> u8,
    pub section_load_page:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, page: i32) -> *mut core::ffi::c_void,
    pub section_page_text: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        page: i32,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub page_close: unsafe extern "C" fn(page: *mut core::ffi::c_void),
    pub page_info: unsafe extern "C" fn(page: *mut core::ffi::c_void, out: *mut XpEpubPageInfo),
    pub page_element:
        unsafe extern "C" fn(page: *mut core::ffi::c_void, index: u16, out: *mut XpEpubElement),
    pub page_word: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        element: u16,
        word: u16,
        out: *mut XpEpubWord,
    ),
    pub metadata_assign:
        unsafe extern "C" fn(resources: *mut core::ffi::c_void, page: *mut core::ffi::c_void) -> u8,
    pub metadata_clear: unsafe extern "C" fn(resources: *mut core::ffi::c_void),
    pub metadata_footnotes: unsafe extern "C" fn(resources: *mut core::ffi::c_void) -> u16,
    pub metadata_links: unsafe extern "C" fn(resources: *mut core::ffi::c_void) -> u16,
    pub metadata_footnote: unsafe extern "C" fn(
        resources: *mut core::ffi::c_void,
        index: u16,
        sink: XpEpubFootnoteSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub metadata_link: unsafe extern "C" fn(
        resources: *mut core::ffi::c_void,
        index: u16,
        sink: XpEpubLinkSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub paint_dither: unsafe extern "C" fn(x: i32, y: i32, width: i32, height: i32),
    pub paint_pixel: unsafe extern "C" fn(x: i32, y: i32),
    pub paint_status: unsafe extern "C" fn(status: *const XpEpubStatus),
    pub image_clear_failures: unsafe extern "C" fn(),
    pub image_release_cache: unsafe extern "C" fn(),
    pub fonts_ensure_loaded: unsafe extern "C" fn(),
    pub fonts_release_caches: unsafe extern "C" fn(),
    pub fonts_prewarm_begin: unsafe extern "C" fn(resources: *mut core::ffi::c_void) -> u8,
    pub fonts_prewarm_finish_scan: unsafe extern "C" fn(resources: *mut core::ffi::c_void),
    pub fonts_prewarm_drop: unsafe extern "C" fn(resources: *mut core::ffi::c_void),
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
    pub child_menu: unsafe extern "C" fn(args: *const XpEpubMenuArgs) -> *mut core::ffi::c_void,
    pub child_chapter:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, toc: i32) -> *mut core::ffi::c_void,
    pub child_percent: unsafe extern "C" fn(percent: i32) -> *mut core::ffi::c_void,
    pub child_text_settings: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub child_footnote: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        left: i32,
        top: i32,
    ) -> *mut core::ffi::c_void,
    pub child_dictionary: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        left: i32,
        top: i32,
        initial_text: XpAppText,
    ) -> *mut core::ffi::c_void,
    pub child_selection: unsafe extern "C" fn(
        pages: *mut *mut core::ffi::c_void,
        count: u8,
        args: *const XpEpubSelectionArgs,
    ) -> *mut core::ffi::c_void,
    pub child_bookmarks: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        path: XpAppText,
    ) -> *mut core::ffi::c_void,
    pub child_clippings: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub child_qr: unsafe extern "C" fn(text: XpAppText) -> *mut core::ffi::c_void,
    pub child_wifi: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub child_sync: unsafe extern "C" fn(
        path: XpAppText,
        position: *const XpEpubPosition,
        saved: *const XpEpubSavedProgress,
        chapter: XpAppText,
        events_only: u8,
    ) -> *mut core::ffi::c_void,
    pub toolbar_open: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub toolbar_close: unsafe extern "C" fn(toolbar: *mut core::ffi::c_void),
    pub toolbar_begin: unsafe extern "C" fn(toolbar: *mut core::ffi::c_void),
    pub toolbar_paint: unsafe extern "C" fn(
        toolbar: *mut core::ffi::c_void,
        model: *const XpEpubToolbarModel,
        rows: XpEpubRowSource,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub toolbar_route:
        unsafe extern "C" fn(toolbar: *mut core::ffi::c_void, out: *mut XpEpubRouted),
    pub toolbar_scroll:
        unsafe extern "C" fn(toolbar: *mut core::ffi::c_void, delta: i32, count: i32) -> u8,
    pub toolbar_visible_rows: unsafe extern "C" fn(toolbar: *mut core::ffi::c_void) -> i32,
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
        unsafe extern "C" fn(bookmarks: *mut core::ffi::c_void, book: *mut core::ffi::c_void) -> u8,
    pub bookmarks_save:
        unsafe extern "C" fn(bookmarks: *mut core::ffi::c_void, book: *mut core::ffi::c_void) -> u8,
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
    pub clippings_load: unsafe extern "C" fn(book: *mut core::ffi::c_void) -> u8,
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
    pub recents_add: unsafe extern "C" fn(book: *mut core::ffi::c_void),
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
    pub page_word_font: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        element: u16,
        word: u16,
        reader_font: i32,
    ) -> i32,
    pub page_line_font:
        unsafe extern "C" fn(page: *mut core::ffi::c_void, element: u16, reader_font: i32) -> i32,
    pub page_word_y_offset: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        element: u16,
        word: u16,
        reader_font: i32,
    ) -> i32,
    pub page_line_ruby_shift:
        unsafe extern "C" fn(page: *mut core::ffi::c_void, element: u16, line_font: i32) -> i32,
    pub font_text_advance:
        unsafe extern "C" fn(text: XpAppText, font: i32, style: u8, character_spacing: i8) -> i32,
    pub log_enabled: unsafe extern "C" fn(severity: u8) -> u8,
    pub toolbar_reset_navigation:
        unsafe extern "C" fn(toolbar: *mut core::ffi::c_void, selected: i32, top: i32),
    pub input_snapshot: unsafe extern "C" fn(out: *mut xp_ui::model::Input),
    pub page_render:
        unsafe extern "C" fn(page: *mut core::ffi::c_void, font: i32, left: i32, top: i32),
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
    pub cache_lock: unsafe extern "C" fn(),
    pub cache_unlock: unsafe extern "C" fn(),
    pub scratch_claim: unsafe extern "C" fn(min_len: u32) -> *mut u8,
    pub scratch_release: unsafe extern "C" fn(bytes: *const u8),
    pub inflate_oom_log: unsafe extern "C" fn(allocation: XpAppText, bytes: u32),
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
            == 213 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, log)
            == 190 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, page_word_font)
            == 191 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, page_line_font)
            == 192 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, page_word_y_offset)
            == 193 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, page_line_ruby_shift)
            == 194 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, font_text_advance)
            == 195 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
};

const _: fn(&XpEpubLeaves) = |leaves| {
    let _: unsafe extern "C" fn(*mut core::ffi::c_void, u16, *mut XpEpubElement) =
        leaves.page_element;
    let _: unsafe extern "C" fn(*mut core::ffi::c_void, u16, u16, *mut XpEpubWord) =
        leaves.page_word;
    let _: unsafe extern "C" fn(
        *mut core::ffi::c_void,
        unsafe extern "C" fn(*mut core::ffi::c_void, u8, XpAppText),
        *mut core::ffi::c_void,
    ) = leaves.end_input;
    let _: unsafe extern "C" fn(*mut core::ffi::c_void) = leaves.recents_add;
    let _: unsafe extern "C" fn(*mut core::ffi::c_void, u16, u16, i32) -> i32 =
        leaves.page_word_font;
    let _: unsafe extern "C" fn(*mut core::ffi::c_void, u16, i32) -> i32 = leaves.page_line_font;
    let _: unsafe extern "C" fn(*mut core::ffi::c_void, u16, u16, i32) -> i32 =
        leaves.page_word_y_offset;
    let _: unsafe extern "C" fn(*mut core::ffi::c_void, u16, i32) -> i32 =
        leaves.page_line_ruby_shift;
    let _: unsafe extern "C" fn(XpAppText, i32, u8, i8) -> i32 = leaves.font_text_advance;
};

const _: () = {
    assert!(
        core::mem::offset_of!(XpEpubLeaves, log_enabled)
            == 196 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
};
const _: fn(&XpEpubLeaves) = |leaves| {
    let _: unsafe extern "C" fn(u8) -> u8 = leaves.log_enabled;
};

const _: () = {
    assert!(
        core::mem::offset_of!(XpEpubLeaves, toolbar_reset_navigation)
            == 197 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
};
const _: fn(&XpEpubLeaves) = |leaves| {
    let _: unsafe extern "C" fn(*mut core::ffi::c_void, i32, i32) = leaves.toolbar_reset_navigation;
};

const _: () = {
    assert!(core::mem::offset_of!(XpEpubSettings, orientation) == 4);
    assert!(core::mem::offset_of!(XpEpubSettings, dictionary_available) == 26);
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
            == 114 * core::mem::size_of::<unsafe extern "C" fn()>()
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
            == 198 * core::mem::size_of::<unsafe extern "C" fn()>()
    );
};
const _: fn(&XpEpubLeaves) = |leaves| {
    let _: unsafe extern "C" fn(*mut core::ffi::c_void, i32, i32, i32) = leaves.page_render;
    let _: unsafe extern "C" fn(*mut xp_ui::model::Input) = leaves.input_snapshot;
};

const _: () = {
    assert!(core::mem::size_of::<XpEpubGeometry>() == 32);
    assert!(core::mem::align_of::<XpEpubGeometry>() == 4);
    assert!(core::mem::offset_of!(XpEpubGeometry, has_font_cache) == 31);
    assert!(
        core::mem::offset_of!(XpEpubLeaves, page_render) == 199 * core::mem::size_of::<usize>()
    );
};

const _: () = {
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_open) == 200 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_close)
            == 201 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_encrypted)
            == 202 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_size) == 203 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, protection_stream)
            == 204 * core::mem::size_of::<usize>()
    );
    assert!(core::mem::offset_of!(XpEpubLeaves, file_seek) == 205 * core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(XpEpubLeaves, file_size) == 206 * core::mem::size_of::<usize>());
    assert!(
        core::mem::offset_of!(XpEpubLeaves, path_remove_tree)
            == 207 * core::mem::size_of::<usize>()
    );
    assert!(core::mem::offset_of!(XpEpubLeaves, cache_lock) == 208 * core::mem::size_of::<usize>());
    assert!(
        core::mem::offset_of!(XpEpubLeaves, cache_unlock) == 209 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, scratch_claim) == 210 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, scratch_release) == 211 * core::mem::size_of::<usize>()
    );
    assert!(
        core::mem::offset_of!(XpEpubLeaves, inflate_oom_log) == 212 * core::mem::size_of::<usize>()
    );
};
