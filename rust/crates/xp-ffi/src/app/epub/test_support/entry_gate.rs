extern crate alloc;

use super::super::{abi, app_abi, EpubSession};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct Observation {
    anchor_ms: u32,
    duration: u32,
    refresh_counter: i32,
    ready: u8,
    frame_present: u8,
    rendered: u8,
    scene_failed: u8,
    refresh_requests: u8,
    pending_forward: u8,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_entry_gate_create(
    native: *mut c_void,
    popup: *mut c_void,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
) -> *mut c_void {
    let Some(mut owner) =
        EpubSession::try_new(b"/gate.epub", false, 7, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    let state = owner.state.get_mut();
    state.base.refresh_counter = 7;
    state.base.session.anchor_ms = 123;
    state.base.session.duration = 456;
    state.base.session.pending_forward = true;
    state.base.refresh_requests = 1;
    state.scene_gate.failed = true;
    let guarded = owner.guarded.get_mut();
    guarded.native = Some(NonNull::new(native).expect("fixture native"));
    guarded.failure_popup = Some(NonNull::new(popup).expect("fixture popup"));
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_entry_gate_destroy(owner: *mut c_void) {
    let mut owner = Box::from_raw(owner.cast::<EpubSession>());
    let guarded = owner.guarded.get_mut();
    assert!(guarded.active_frame.is_none());
    assert!(guarded.book.is_none() && guarded.section.is_none());
    // The host owns both physical handles.
    guarded.native = None;
    guarded.failure_popup = None;
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_entry_gate_load(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).entry_load_book())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_entry_gate_render(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).render())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_entry_gate_observe(
    owner: *mut c_void,
    out: *mut Observation,
) {
    let owner = &*owner.cast::<EpubSession>();
    *out = owner.model(|state| Observation {
        anchor_ms: state.base.session.anchor_ms,
        duration: state.base.session.duration,
        refresh_counter: state.base.refresh_counter,
        ready: u8::from(state.ready),
        frame_present: u8::from(owner.guarded.fixture().active_frame.is_some()),
        rendered: u8::from(state.base.rendered),
        scene_failed: u8::from(state.scene_gate.failed),
        refresh_requests: state.base.refresh_requests,
        pending_forward: u8::from(state.base.session.pending_forward),
    });
}
