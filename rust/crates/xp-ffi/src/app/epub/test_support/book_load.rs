use super::*;
use core::ffi::c_void;
type Output = unsafe extern "C" fn(
    *mut c_void,
    u32,
    i32,
    app_abi::Text,
    app_abi::Text,
    app_abi::Text,
    u32,
    i32,
);
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_load(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: app_abi::Text,
    cache_path: app_abi::Text,
    build: u8,
    skip_css: u8,
    query_cap: u32,
    output: Output,
    context: *mut c_void,
) -> u8 {
    let (Some(path), Some(cache_path)) = (unsafe { path.bytes() }, unsafe { cache_path.bytes() })
    else {
        return 0;
    };
    let (Ok(mut path), Ok(cache_path)) = (Text::copy(path), Text::copy(cache_path)) else {
        return 0;
    };
    unsafe extern "C" fn now() -> u32 {
        0
    }
    let result = ReaderBook::load(
        &mut path,
        cache_path,
        resources,
        leaves,
        now,
        build != 0,
        skip_css != 0,
    );
    let Ok(book) = result else {
        assert!(!path.is_empty());
        return 0;
    };
    assert!(path.is_empty());
    assert!(query_cap > 0);
    let empty = app_abi::Text::from_bytes(&[]);
    // Separates allowed retained-cursor query costs from the load transaction trace.
    unsafe {
        output(context, 9, 0, empty, empty, empty, 0, 0);
    }
    for (index, bytes) in [
        &book.metadata.title,
        &book.metadata.author,
        &book.metadata.language,
        &book.metadata.cover,
        &book.metadata.text_reference,
    ]
    .iter()
    .enumerate()
    {
        unsafe {
            output(
                context,
                0,
                index as i32,
                app_abi::Text::from_bytes(bytes),
                empty,
                empty,
                0,
                0,
            );
        }
    }
    unsafe {
        output(
            context,
            1,
            0,
            empty,
            empty,
            empty,
            u32::from(book.cache.spine_count),
            i32::from(book.cache.toc_count),
        );
    }
    let spine_limit = u32::from(book.cache.spine_count).min(query_cap) as i32;
    let toc_limit = u32::from(book.cache.toc_count).min(query_cap) as i32;
    if spine_limit != i32::from(book.cache.spine_count)
        || toc_limit != i32::from(book.cache.toc_count)
    {
        unsafe {
            output(
                context,
                8,
                0,
                empty,
                empty,
                empty,
                spine_limit as u32,
                toc_limit,
            );
        }
    }
    for index in 0..spine_limit {
        let Ok(row) = book.cache.spine(index) else {
            return 0;
        };
        unsafe {
            output(
                context,
                2,
                index,
                app_abi::Text::from_bytes(&row.href),
                empty,
                empty,
                row.cumulative,
                i32::from(row.toc_index),
            );
        }
    }
    for index in 0..toc_limit {
        let Ok(row) = book.cache.toc(index) else {
            return 0;
        };
        unsafe {
            output(
                context,
                3,
                index,
                app_abi::Text::from_bytes(&row.title),
                app_abi::Text::from_bytes(&row.href),
                app_abi::Text::from_bytes(&row.anchor),
                u32::from(row.level),
                i32::from(row.spine_index),
            );
        }
    }
    for index in -1..=spine_limit {
        let size = book.cache.cumulative(index);
        unsafe {
            output(
                context,
                4,
                index,
                empty,
                empty,
                empty,
                size,
                book.cache.spine_for_size(size),
            );
        }
    }
    1
}
