use xp_core::book::{self, Services};

#[no_mangle]
pub unsafe extern "C" fn xp_book_load(
    services: *const Services,
    build_if_missing: u8,
    skip_css: u8,
) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    book::load(services, build_if_missing != 0, skip_css != 0) as u8
}

#[no_mangle]
pub extern "C" fn xp_book_progress(
    book_size: usize,
    previous: usize,
    current: usize,
    fraction: f32,
) -> f32 {
    book::progress(book_size, previous, current, fraction)
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_css_observe(
    entries: *mut book::css::CssEntry,
    count: usize,
    path: book::css::Span,
    crc: u32,
    compressed: u32,
    ctx: *mut core::ffi::c_void,
    get_path: Option<book::css::CssPath>,
) {
    let Some(get_path) = get_path else {
        return;
    };
    if entries.is_null()
        || count > isize::MAX as usize / core::mem::size_of::<book::css::CssEntry>()
        || path.data.is_null()
        || path.len > isize::MAX as usize
    {
        return;
    }
    book::css::css_observe(
        core::slice::from_raw_parts_mut(entries, count),
        core::slice::from_raw_parts(path.data, path.len),
        crc,
        compressed,
        ctx,
        get_path,
    );
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_css_duplicate(
    entries: *const book::css::CssEntry,
    count: usize,
    index: usize,
) -> u8 {
    if entries.is_null()
        || count > isize::MAX as usize / core::mem::size_of::<book::css::CssEntry>()
    {
        return 0;
    }
    book::css::css_duplicate(core::slice::from_raw_parts(entries, count), index) as u8
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_read_bytes(
    services: *const book::io::Services,
    empty: u8,
    size: *mut usize,
    trailing: u8,
) -> *mut u8 {
    let Some(services) = services.as_ref() else {
        return core::ptr::null_mut();
    };
    book::io::bytes(services, empty != 0, size, trailing != 0)
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_read_stream(
    services: *const book::io::Services,
    empty: u8,
    chunk: usize,
    early: u8,
) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    book::io::stream(services, empty != 0, chunk, early != 0) as u8
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_item_size(
    services: *const book::io::Services,
    size: *mut usize,
) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    book::io::size(services, size) as u8
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_extract(services: *const book::io::Services) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    book::io::extract(services) as u8
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_cover(
    services: *const book::cover::Services,
    mode: u32,
    height: i32,
) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    book::cover::generate(services, mode, height) as u8
}
#[no_mangle]
pub unsafe extern "C" fn xp_book_gif_extract(
    services: *const book::cover::GifExtractServices,
) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    book::cover::extract_gif(services) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_metadata(services: *const book::metadata::Services) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    book::metadata::metadata(services) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_metadata_source(services: *const book::metadata::Services) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    book::metadata::source(services) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_sync_metadata(services: *const book::metadata::Services) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    book::metadata::sync(services) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_clear_cache(services: *const book::metadata::Services) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    book::metadata::clear_cache(services) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_book_setup_cache(services: *const book::metadata::Services) {
    if let Some(services) = services.as_ref() {
        book::metadata::setup_cache(services);
    }
}
