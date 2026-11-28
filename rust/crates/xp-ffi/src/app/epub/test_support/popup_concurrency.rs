use super::{
    super::{abi, app_abi, EpubSession},
    entry_gate,
};
use core::{ffi::c_void, ptr::NonNull};

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_popup_concurrency_create_legacy_book(
    native: *mut c_void,
    popup: *mut c_void,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
    legacy_book: &'static super::legacy_book::LegacyBookServices,
) -> *mut c_void {
    let raw = entry_gate::xp_epub_test_entry_gate_create_legacy_book(
        native,
        popup,
        leaves,
        runtime,
        resources,
        legacy_book,
    );
    if let Some(mut owner) = NonNull::new(raw.cast::<EpubSession>()) {
        owner.as_mut().guarded.get_mut().overlay_popup = NonNull::new(popup);
    }
    raw
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_popup_concurrency_failure(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).handle_load_failure())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_popup_concurrency_text(owner: *mut c_void, row: i32) {
    (&*owner.cast::<EpubSession>()).show_text_row_popup(row);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_popup_concurrency_destroy(owner: *mut c_void) {
    (&mut *owner.cast::<EpubSession>())
        .guarded
        .get_mut()
        .overlay_popup = None;
    entry_gate::xp_epub_test_entry_gate_destroy(owner);
}
