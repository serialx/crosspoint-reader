//! Real session construction and live frequency reads; no legacy fixture.
extern crate alloc;
use super::super::{abi, app_abi, EpubSession};
use alloc::boxed::Box;
use core::ffi::c_void;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_frequency_create(
    leaves: &'static abi::XpEpubLeaves,
    runtime: &'static app_abi::Runtime,
    resources: &'static app_abi::Resources,
    fast: u8,
) -> *mut c_void {
    let Some(owner) = EpubSession::try_owned(b"", fast != 0, leaves, runtime, resources) else {
        return core::ptr::null_mut();
    };
    assert!(owner.legacy_pages.get().is_none() && owner.render_hooks.is_none());
    Box::into_raw(owner).cast()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_frequency_read(raw: *mut c_void, counter: *mut i32) -> i32 {
    let owner = unsafe { &*raw.cast::<EpubSession>() };
    assert!(owner.legacy_pages.get().is_none() && owner.render_hooks.is_none());
    owner.model(|state| state.ready = true);
    let mut frequency = 0;
    assert!(owner.render_with(|access| {
        counter.write(owner.render_refresh_counter(access));
        frequency = owner.refresh_frequency(access);
    }));
    owner.model(|state| state.ready = false);
    frequency
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_frequency_destroy(raw: *mut c_void) {
    let owner = unsafe { Box::from_raw(raw.cast::<EpubSession>()) };
    owner.destroy_owned();
    drop(owner);
}
