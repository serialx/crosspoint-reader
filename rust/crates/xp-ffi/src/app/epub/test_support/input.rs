extern crate alloc;

use super::super::{abi, app_abi, EpubSession};
use alloc::boxed::Box;
use core::ffi::c_void;
use xp_ui::reader::input::Turn;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_input_create(
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
) -> *mut c_void {
    EpubSession::try_new(b"/book.epub", false, 3, &*leaves, &*runtime, &*resources)
        .map_or(core::ptr::null_mut(), |owner| Box::into_raw(owner).cast())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_input_destroy(raw: *mut c_void) {
    drop(Box::from_raw(raw.cast::<EpubSession>()));
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_input_buttons(raw: *mut c_void, out: *mut Turn) {
    *out = (&*raw.cast::<EpubSession>()).button_turn();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_input_touch(raw: *mut c_void, out: *mut Turn) {
    *out = (&*raw.cast::<EpubSession>()).touch_turn();
}
