extern crate alloc;

use super::super::{abi, app_abi, text, EpubSession};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct LinkSeed {
    pub native: *mut c_void,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_link_create(
    seed: *const LinkSeed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
    legacy_page: &'static super::legacy_page::LegacyPageServices,
) -> *mut c_void {
    let Some(mut owner) = EpubSession::try_new(b"", false, 3, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    owner.legacy_pages.set(Some(legacy_page));
    owner.state.get_mut().ready = true;
    owner.guarded.get_mut().native =
        Some(NonNull::new((*seed).native).expect("fixture metadata root"));
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_link_destroy(owner: *mut c_void) {
    // The native metadata root remains owned by the fixture.
    drop(Box::from_raw(owner.cast::<EpubSession>()));
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_link_at_point(
    owner: *mut c_void,
    x: i32,
    y: i32,
    left: i32,
    top: i32,
    index: *mut u16,
) -> u8 {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    match owner.link_at_point(&access, x, y, left, top) {
        Some(found) => {
            *index = found;
            1
        }
        None => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_sanitize_summary(
    input: app_abi::Text,
    output: *mut [u8; 72],
) -> usize {
    text::sanitize_summary(input.bytes().expect("fixture summary"), &mut *output)
}
