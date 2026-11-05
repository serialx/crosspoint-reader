extern crate alloc;

use super::super::{abi, app_abi, EpubSession};
use alloc::boxed::Box;
use core::ffi::c_void;
use xp_ui::reader::input::Turn;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_input_create_legacy_book(
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
    legacy_book: &'static super::legacy_book::LegacyBookServices,
) -> *mut c_void {
    EpubSession::try_new(b"/book.epub", false, 3, &*leaves, &*runtime, &*resources).map_or(
        core::ptr::null_mut(),
        |mut owner| {
            owner.book_factory = super::super::session_book::Factory::Legacy(legacy_book);
            owner.legacy_pages.set(Some(&legacy_book.section.page));
            Box::into_raw(owner).cast()
        },
    )
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

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_input_book_touch(
    raw: *mut c_void,
    book: *mut c_void,
    out: *mut Turn,
) {
    let owner = &*raw.cast::<EpubSession>();
    assert!(owner.loop_book().is_none());
    let super::super::session_book::Factory::Legacy(services) = owner.book_factory else {
        panic!("legacy input fixture")
    };
    super::super::session_book::bind_legacy_fixture(raw.cast(), book, services);
    *out = owner.book_touch_turn();
    owner.discard_legacy_fixture();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_input_menu(raw: *mut c_void) -> u8 {
    u8::from((&*raw.cast::<EpubSession>()).touch_menu_gesture())
}
