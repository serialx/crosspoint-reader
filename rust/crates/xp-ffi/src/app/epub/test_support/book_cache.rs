extern crate alloc;

use super::{BookCache, Metadata, Text, XpEpubLeaves};
use alloc::{alloc::alloc, boxed::Box};
use core::{alloc::Layout, ffi::c_void, ptr, slice};

type Sink = unsafe extern "C" fn(*mut c_void, u32, *const u8, usize, i64);
struct Cache {
    cache: BookCache,
    metadata: Metadata,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_cache_create(leaves: &'static XpEpubLeaves) -> *mut c_void {
    let raw = unsafe { alloc(Layout::new::<Cache>()).cast::<Cache>() };
    if raw.is_null() {
        return ptr::null_mut();
    }
    unsafe {
        raw.write(Cache {
            cache: BookCache::new(leaves),
            metadata: Metadata::default(),
        })
    };
    raw.cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_cache_destroy(raw: *mut c_void) {
    unsafe { drop(Box::from_raw(raw.cast::<Cache>())) };
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_cache_set(
    raw: *mut c_void,
    field: u32,
    bytes: *const u8,
    length: usize,
) -> u8 {
    let cache = unsafe { &mut *raw.cast::<Cache>() };
    let metadata = &mut cache.metadata;
    let target = match field {
        0 => &mut metadata.title,
        1 => &mut metadata.author,
        2 => &mut metadata.language,
        3 => &mut metadata.cover,
        4 => &mut metadata.text_reference,
        _ => return 0,
    };
    if target
        .try_reserve_exact(length.saturating_sub(target.len()))
        .is_err()
    {
        return 0;
    }
    target.clear();
    if length != 0 {
        target.extend_from_slice(unsafe { slice::from_raw_parts(bytes, length) });
    }
    1
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_cache_load(
    raw: *mut c_void,
    path: *const u8,
    length: usize,
) -> u8 {
    let Ok(path) = Text::copy(unsafe { slice::from_raw_parts(path, length) }) else {
        return 0;
    };
    let cache = unsafe { &mut *raw.cast::<Cache>() };
    u8::from(cache.cache.load(&mut cache.metadata, &path).is_ok())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_cache_query(
    raw: *const c_void,
    kind: u32,
    index: i32,
    sink: Sink,
    context: *mut c_void,
) -> i64 {
    let owner = unsafe { &*raw.cast::<Cache>() };
    let cache = &owner.cache;
    let text =
        |field, bytes: &[u8]| unsafe { sink(context, field, bytes.as_ptr(), bytes.len(), 0) };
    let number = |field, value| unsafe { sink(context, field, ptr::null(), 0, value) };
    match kind {
        0 => {
            for (field, bytes) in [
                &owner.metadata.title,
                &owner.metadata.author,
                &owner.metadata.language,
                &owner.metadata.cover,
                &owner.metadata.text_reference,
            ]
            .iter()
            .enumerate()
            {
                text(field as u32, bytes);
            }
            number(5, cache.spine_count.into());
            number(6, cache.toc_count.into());
            i64::from(cache.loaded)
        }
        1 => {
            let Ok(row) = cache.spine(index) else {
                return 0;
            };
            text(0, &row.href);
            number(1, row.cumulative.into());
            number(2, row.toc_index.into());
            1
        }
        2 => {
            let Ok(row) = cache.toc(index) else { return 0 };
            text(0, row.title.bytes());
            text(1, &row.href);
            text(2, &row.anchor);
            number(3, row.level.into());
            number(4, row.spine_index.into());
            1
        }
        3 => cache.cumulative(index).into(),
        4 => cache.spine_for_size(index as u32).into(),
        _ => -1,
    }
}
