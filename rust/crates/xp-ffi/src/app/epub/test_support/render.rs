extern crate alloc;

use super::super::{abi, app_abi, page::Page, EpubSession};
use super::RenderHooks;
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};
use xp_ui::reader::base::REFRESH_MANUAL;

#[repr(C)]
pub struct RenderSeed {
    pub native: *mut c_void,
    pub context: *mut c_void,
    pub counter: i32,
    pub forced: u8,
    pub status: unsafe extern "C" fn(*mut c_void),
    pub highlights: unsafe extern "C" fn(*mut c_void, *mut c_void, i32, i32, i32),
}
#[repr(C)]
pub struct RenderObservation {
    pub counter: i32,
    pub forced: u8,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_render_create(
    seed: *const RenderSeed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
) -> *mut c_void {
    let seed = &*seed;
    let Some(mut owner) = EpubSession::try_new(b"", false, 3, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    let state = owner.state.get_mut();
    // Standalone renderContents composition; entry/load gates are covered separately.
    state.ready = true;
    state.base.refresh_counter = seed.counter;
    state.base.refresh_requests = if seed.forced != 0 { REFRESH_MANUAL } else { 0 };
    owner.guarded.get_mut().native = Some(NonNull::new(seed.native).expect("physical render root"));
    owner.render_hooks = Some(RenderHooks {
        context: seed.context,
        status: seed.status,
        highlights: seed.highlights,
    });
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_render_contents(
    raw: *mut c_void,
    page: *mut c_void,
    font: i32,
    margins: *const i32,
    strip_enabled: u8,
) {
    let owner = &*raw.cast::<EpubSession>();
    let margins = [*margins, *margins.add(1), *margins.add(2), *margins.add(3)];
    let page = Page::from_raw(page, owner.leaves).expect("owned fixture Page");
    assert!(owner.render_with(|access| {
        owner.render_contents(access, page, font, margins, strip_enabled != 0);
    }));
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_render_observe(
    raw: *mut c_void,
    out: *mut RenderObservation,
) {
    let owner = &*raw.cast::<EpubSession>();
    *out = owner.model(|state| RenderObservation {
        counter: state.base.refresh_counter,
        forced: u8::from(state.base.refresh_requests & REFRESH_MANUAL != 0),
    });
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_render_destroy(raw: *mut c_void) {
    let mut owner = Box::from_raw(raw.cast::<EpubSession>());
    assert!(owner.guarded.get_mut().active_frame.is_none());
    // Persistent native strip storage belongs to the enclosing fixture and drops
    // after the historical end line, matching the original Activity lifetime.
    owner.guarded.get_mut().native = None;
    drop(owner);
}
