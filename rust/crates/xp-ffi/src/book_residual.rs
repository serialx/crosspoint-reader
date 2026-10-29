//! Residual facade policy. All spans and callbacks borrow synchronously.
use core::{
    ffi::{c_char, c_void},
    slice,
};
use xp_core::book::residual::{self as policy, Span};
use xp_core::book::residual_abi::*;
#[no_mangle]
pub unsafe extern "C" fn xp_book_bidi_should_shape(data: *const u8) -> u8 {
    policy::should_shape(data) as u8
}

#[no_mangle]
pub extern "C" fn xp_book_bidi_word_count(count: usize) -> u8 {
    policy::word_count(count) as u8
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_bidi_word_spans(
    data: *const u8,
    len: usize,
    offsets: *const u16,
    count: usize,
    output: usize,
) -> u8 {
    let offsets = if count == 0 {
        &[]
    } else {
        slice::from_raw_parts(offsets, count)
    };
    policy::word_spans(Span { data, len }.bytes(), offsets, output) as u8
}
#[no_mangle]
pub extern "C" fn xp_book_xtc_progress(loaded: u8, page: u32, pages: u32) -> u8 {
    policy::xtc_progress(loaded != 0, page, pages)
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_xtc_title(path: Span, metadata: Span) -> Span {
    if metadata.len == 0 {
        Span::new(policy::xtc_title(path.bytes()))
    } else {
        metadata
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_image_kind(path: Span) -> u32 {
    policy::txt_kind(path.bytes())
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_cache_tag(path: Span) -> *const c_char {
    if policy::extension(path.bytes(), b".md") {
        c"<!-- MD_CACHE_VERSION: 1 -->".as_ptr()
    } else {
        c"<!-- TXT_CACHE_VERSION: 1 -->".as_ptr()
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_discover_css(
    base: Span,
    path: Span,
    count: usize,
    ctx: *mut c_void,
    get: policy::GetPath,
) -> u8 {
    policy::discover_css(base.bytes(), path.bytes(), count, ctx, get) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_document(
    s: *const xp_core::book::document::Services,
    kind: u32,
    metadata: u8,
) -> u8 {
    let Some(s) = s.as_ref() else { return 0 };
    (match kind {
        XP_BOOK_DOCUMENT_CONTAINER => xp_core::book::document::container(s),
        XP_BOOK_DOCUMENT_NCX => xp_core::book::document::toc(s, false),
        XP_BOOK_DOCUMENT_NAV => xp_core::book::document::toc(s, true),
        XP_BOOK_DOCUMENT_OPF => xp_core::book::document::opf(s, metadata != 0),
        _ => false,
    }) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_css_parse_files(
    s: *const xp_core::book::css::ParseServices,
    count: usize,
    partial: u8,
) -> u32 {
    let Some(s) = s.as_ref() else {
        return XP_BOOK_CSS_PARSE_ERROR;
    };
    xp_core::book::css::parse_files(s, count, partial != 0)
}

use xp_core::book_io::zip_index;
#[no_mangle]
pub unsafe extern "C" fn xp_book_zip_index(
    s: *const zip_index::Services,
    operation: u32,
    target: Span,
    last: Span,
) -> u8 {
    let Some(s) = s.as_ref() else { return 0 };
    (match operation {
        XP_BOOK_ZIP_LOOKUP => zip_index::lookup(s, target, last),
        XP_BOOK_ZIP_PATHS => zip_index::enumerate(s, true),
        XP_BOOK_ZIP_ENTRIES => zip_index::enumerate(s, false),
        _ => false,
    }) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_zip_batch(
    s: *const zip_index::Services,
    count: usize,
    ctx: *mut c_void,
    get: unsafe extern "C" fn(*mut c_void, usize) -> zip_index::Target,
    set: unsafe extern "C" fn(*mut c_void, u16, u32) -> bool,
) -> usize {
    let Some(s) = s.as_ref() else { return 0 };
    zip_index::batch(s, count, ctx, get, set)
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_zip_details(
    s: *const zip_index::DetailServices,
    cached: bool,
) -> bool {
    let Some(s) = s.as_ref() else { return false };
    zip_index::details(s, cached)
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_zip_close(cursor: *mut u32, valid: *mut bool, last: *mut bool) {
    cursor.write(0);
    valid.write(false);
    last.write(false);
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_find_spine(
    target: Span,
    count: usize,
    filename: bool,
    ctx: *mut c_void,
    get: policy::GetPath,
) -> i32 {
    policy::find_spine(target.bytes(), count, filename, ctx, get)
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_xtc_generate(
    s: *const xp_core::book_io::xtc_cover::Services,
    thumb: bool,
    height: i32,
) -> bool {
    let Some(s) = s.as_ref() else { return false };
    xp_core::book_io::xtc_cover::generate(s, thumb, height)
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_directories(
    path: Span,
    ctx: *mut c_void,
    control: unsafe extern "C" fn(*mut c_void, u32, Span) -> bool,
) {
    policy::directories(path.bytes(), ctx, control)
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_title(path: Span) -> Span {
    Span::new(policy::txt_title(path.bytes()))
}
#[no_mangle]
pub extern "C" fn xp_book_path(kind: u32) -> *const c_char {
    match kind {
        XP_BOOK_PATH_HTML_DIRECTORY => c"/html",
        XP_BOOK_PATH_SECTIONS_DIRECTORY => c"/sections",
        XP_BOOK_PATH_HTML_DOCUMENT => c"/html/0.html",
        XP_BOOK_PATH_TEXT_HREF => c"content.html",
        XP_BOOK_PATH_COVER => c"/cover.bmp",
        XP_BOOK_PATH_THUMB_PATTERN => c"/thumb_[HEIGHT].bmp",
        XP_BOOK_PATH_THUMB_PREFIX => c"/thumb_",
        _ => c"",
    }
    .as_ptr()
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_metadata(
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, u32, Span) -> Span,
) {
    policy::txt_metadata(ctx, operation)
}

#[no_mangle]
pub extern "C" fn xp_book_lookup(
    loaded: bool,
    index: i32,
    count: i32,
    toc: bool,
) -> policy::Lookup {
    policy::lookup(loaded, index, count, toc)
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_reclaim_content(
    before: usize,
    clear: unsafe extern "C" fn(),
    diagnostic: Option<unsafe extern "C" fn(usize)>,
) {
    policy::reclaim_content(before, clear, diagnostic)
}
#[no_mangle]
pub extern "C" fn xp_book_toc_spine(index: i32) -> policy::Lookup {
    policy::toc_spine(index)
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_href_path(path: Span) -> Span {
    let bytes = path.bytes();
    Span::new(&bytes[..bytes.iter().position(|&b| b == b'#').unwrap_or(bytes.len())])
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_zip_populate(s: *const zip_index::Services) -> bool {
    let Some(s) = s.as_ref() else { return false };
    zip_index::populate(s)
}
