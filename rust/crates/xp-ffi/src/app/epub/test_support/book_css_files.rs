use super::{
    super::{items::Items, Protection, Vec},
    *,
};
use xp_core::{book::css::CssEntry, epub::cache::sort_legacy_slice};

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_css_sort(entries: *mut CssEntry, count: usize) -> u8 {
    if count == 0 {
        return 1;
    }
    let entries = unsafe { core::slice::from_raw_parts_mut(entries, count) };
    u8::from(sort_legacy_slice(entries, |a, b| a.path_hash < b.path_hash).is_ok())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_css_files(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: abi::Text,
    cache_path: abi::Text,
    paths: *const abi::Text,
    count: usize,
    base: abi::Text,
    discover_files: u8,
    partial_cache: u8,
) -> u32 {
    unsafe extern "C" fn now() -> u32 {
        0
    }
    let (Some(path), Some(cache_path), Some(base)) = (
        unsafe { path.bytes() },
        unsafe { cache_path.bytes() },
        unsafe { base.bytes() },
    ) else {
        return 2;
    };
    let (Ok(path), Ok(cache_path)) = (Text::copy(path), Text::copy(cache_path)) else {
        return 2;
    };
    let mut owned_paths = Vec::new();
    if owned_paths.try_reserve_exact(count).is_err() {
        return 2;
    }
    let paths = if count == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(paths, count) }
    };
    for path in paths {
        let Some(bytes) = (unsafe { path.bytes() }) else {
            return 2;
        };
        let Ok(path) = Text::copy(bytes) else {
            return 2;
        };
        owned_paths.push(path);
    }
    let Ok(protection) = Protection::open(leaves, &path) else {
        return 2;
    };
    let items = Items {
        path: &path,
        protection: &protection,
        resources,
        leaves,
        now,
    };
    if discover_files != 0 && discover(&items, base, &mut owned_paths).is_err() {
        return 2;
    }
    let mut owner = None;
    let result = parse_files(
        &items,
        &owned_paths,
        &cache_path,
        &mut owner,
        partial_cache != 0,
    );
    assert!(owner.is_none());
    result
}
