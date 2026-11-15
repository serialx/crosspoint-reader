extern crate alloc;

use super::super::{abi, app_abi, EpubSession};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct HelpersSeed {
    pub native: *mut c_void,
    pub popup: *mut c_void,
    pub refresh: i32,
    pub stored: u8,
    pub pending: u8,
}

#[repr(C)]
pub struct HelpersObservation {
    pub refresh: i32,
    pub stored: u8,
    pub pending: u8,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_helpers_create_legacy_render(
    seed: *const HelpersSeed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
    legacy: Option<&'static super::legacy_page::LegacyPageServices>,
) -> *mut c_void {
    let seed = &*seed;
    let Some(mut owner) = EpubSession::try_new(b"", false, 0, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    owner.legacy_pages.set(legacy);
    let state = owner.state.get_mut();
    state.ready = true;
    state.base.refresh_counter = seed.refresh;
    state.page_stored = seed.stored != 0;
    state.refresh_pending = seed.pending != 0;
    let guarded = owner.guarded.get_mut();
    guarded.native = Some(NonNull::new(seed.native).expect("fixture resource root"));
    guarded.overlay_popup = NonNull::new(seed.popup);
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_helpers_destroy(owner: *mut c_void) {
    let mut owner = Box::from_raw(owner.cast::<EpubSession>());
    // These handles borrow native fixture objects, not transferred allocations.
    owner.guarded.get_mut().native = None;
    owner.guarded.get_mut().overlay_popup = None;
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_helpers_observe(
    owner: *mut c_void,
    out: *mut HelpersObservation,
) {
    let owner = &*owner.cast::<EpubSession>();
    *out = owner.model(|state| HelpersObservation {
        refresh: state.base.refresh_counter,
        stored: u8::from(state.page_stored),
        pending: u8::from(state.refresh_pending),
    });
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_helpers_discard(owner: *mut c_void) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.discard_overlay_page(&access);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_helpers_apply_text_live(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).apply_text_setting_live();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_helpers_push(owner: *mut c_void) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.push_overlay_refresh(&access);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_helpers_settle(owner: *mut c_void) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.settle_overlay_refresh(&access);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_helpers_paint(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).paint_overlay_popup();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_helpers_disable_fast(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).disable_fast_initial_refresh();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_helpers_orientation(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).apply_initial_orientation();
}
