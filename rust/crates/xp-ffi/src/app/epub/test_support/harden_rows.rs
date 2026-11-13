//! Strict HAL query regressions for failed metadata rows.
use super::*;
use core::{ffi::c_void, ptr};
unsafe extern "C" fn now() -> u32 {
    0
}
#[no_mangle]
#[allow(clippy::useless_conversion)]
pub unsafe extern "C" fn xp_epub_test_harden_rows(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    cache: app_abi::Text,
    fault: unsafe extern "C" fn(),
    kind: u8,
) -> i32 {
    let cache_path = Text::copy(unsafe { cache.bytes() }.unwrap()).unwrap();
    let mut path = Text::default();
    path.assign_parts(&[cache_path.bytes(), b"/book.bin"])
        .unwrap();
    let mut cache = BookCache::new(leaves);
    let mut metadata = Metadata::default();
    cache.load(&mut metadata, &path).unwrap();
    let mut book = ReaderBook {
        resources,
        now,
        path,
        cache_path,
        metadata,
        cache,
        css: None,
        protection: Protection::Plain,
    };
    let raw = ptr::addr_of_mut!(book).cast();

    book.metadata.text_reference.extend_from_slice(&[b'x'; 40]);
    unsafe { fault() };
    let mut calls = 0i32;
    unsafe extern "C" fn spine(
        raw: *mut c_void,
        _: *const super::super::super::abi::XpEpubSpine,
    ) -> u8 {
        unsafe { *raw.cast::<i32>() += 1 };
        1
    }
    unsafe extern "C" fn toc(
        raw: *mut c_void,
        _: *const super::super::super::abi::XpEpubToc,
    ) -> u8 {
        unsafe { *raw.cast::<i32>() += 1 };
        1
    }
    match kind {
        0 => {
            i32::from(unsafe { queries::spine(raw, 0, spine, ptr::addr_of_mut!(calls).cast()) })
                + calls * 10
        }
        1 => {
            i32::from(unsafe { queries::toc(raw, 0, toc, ptr::addr_of_mut!(calls).cast()) })
                + calls * 10
        }
        2 => {
            let value: Option<i32> = unsafe { queries::toc_for_spine(raw, 0) }.into();
            value.unwrap_or(-2)
        }
        3 => unsafe { queries::resolve_href(raw, app_abi::Text::from_bytes(&[b'x'; 40])) },
        4 => {
            let value: Option<i32> = unsafe { queries::text_reference(raw) }.into();
            value.unwrap_or(-2)
        }
        _ => panic!("query selector"),
    }
}
