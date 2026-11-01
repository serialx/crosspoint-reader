// Evidence draft: import existing ABI types at the platform boundary.
use super::super::abi::{
    Book as XpAppBook, Text as XpAppText, TextSink as XpAppTextSink, XpAppNativeText,
};
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
    pub refresh_frequency: i32,
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
    pub font: i32,
    pub ruby_shift: i32,
    pub words: u16,
    pub tag: u8,
    pub valid: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubWord {
    pub text: XpAppText,
    pub x: i32,
    pub y_offset: i32,
    pub font: i32,
    pub advance: i32,
    pub em_advance: i32,
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
    pub peer: [u8; 64],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubGeometry {
    pub width: i32,
    pub height: i32,
    pub margins: [i32; 4],
    pub status_height: i32,
    pub progress_height: i32,
    pub status_margin: i32,
    pub title_mode: u8,
    pub has_framebuffer: u8,
    pub async_refresh: u8,
    pub xteink_panel: u8,
    pub a4: u8,
    pub has_ttf: u8,
    pub strip_enabled: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubInputFacts {
    pub held_ms: u32,
    pub raw_held: u32,
    pub raw_edges: u8,
    pub remote_click: u8,
    pub touch: u8,
    pub home_action: u8,
    pub slide_owns_light: u8,
}

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
    pub selected: i32,
    pub chosen: u8,
    pub active: u8,
    pub changed: u8,
    pub consumed: u8,
}

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
    pub book_thumbnail: unsafe extern "C" fn(
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
    pub book_spine_for_toc: unsafe extern "C" fn(book: *mut core::ffi::c_void, index: i32) -> i32,
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
    pub section_estimated_pages: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u16,
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
        popup: XpEpubBuildPopup,
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
    pub section_toc_for_page:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, page: i32) -> i32,
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
    pub page_element: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        index: u16,
        reader_font: i32,
        out: *mut XpEpubElement,
    ),
    pub page_word: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        element: u16,
        word: u16,
        reader_font: i32,
        out: *mut XpEpubWord,
    ),
    pub page_footnote: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        index: u16,
        sink: XpEpubFootnoteSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub page_link: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        index: u16,
        sink: XpEpubLinkSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
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
    pub settings_snapshot: unsafe extern "C" fn(out: *mut XpEpubSettings),
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
    pub input_long_press: unsafe extern "C" fn(button: u32, threshold: u32) -> u8,
    pub input_released: unsafe extern "C" fn(button: u32) -> u8,
    pub input_screen_long: unsafe extern "C" fn(x: *mut i32, y: *mut i32) -> u8,
    pub input_screen_tap: unsafe extern "C" fn(x: *mut i32, y: *mut i32) -> u8,
    pub input_raw_contact: unsafe extern "C" fn() -> u8,
    pub render_busy: unsafe extern "C" fn() -> u8,
    pub file_open_read: unsafe extern "C" fn(path: XpAppText) -> *mut core::ffi::c_void,
    pub file_open_write: unsafe extern "C" fn(path: XpAppText) -> *mut core::ffi::c_void,
    pub file_read:
        unsafe extern "C" fn(file: *mut core::ffi::c_void, bytes: *mut u8, length: u32) -> i32,
    pub file_write:
        unsafe extern "C" fn(file: *mut core::ffi::c_void, bytes: *const u8, length: u32) -> u32,
    pub file_flush: unsafe extern "C" fn(file: *mut core::ffi::c_void),
    pub file_close: unsafe extern "C" fn(file: *mut core::ffi::c_void),
    pub path_remove: unsafe extern "C" fn(path: XpAppText) -> u8,
    pub path_rename: unsafe extern "C" fn(r#from: XpAppText, to: XpAppText) -> u8,
    pub path_mkdir: unsafe extern "C" fn(path: XpAppText) -> u8,
    pub native_path_hash: unsafe extern "C" fn(path: XpAppText) -> u64,
    pub heap_snapshot: unsafe extern "C" fn(out: *mut XpEpubHeap),
    pub radio_status: unsafe extern "C" fn(out: *mut XpEpubRadio),
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
    pub popup_show: unsafe extern "C" fn(
        popup: *mut core::ffi::c_void,
        title: XpAppText,
        message: XpAppText,
        labels: *const XpAppText,
        count: u32,
        selected: i32,
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
        selected_path: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
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
    pub recents_add: unsafe extern "C" fn(book: *const XpAppBook),
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
}
