extern crate alloc;

use super::super::{abi, app_abi, state::Position, EpubSession};
use super::EstimateObserver;
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct PositionSeed {
    pub native: *mut c_void,
    pub book: *mut c_void,
    pub section: *mut c_void,
    pub page: i32,
    pub spine: i32,
    pub estimate_context: *mut c_void,
    pub estimate_read: unsafe extern "C" fn(*mut c_void, i32),
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_position_create(
    seed: *const PositionSeed,
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
    state.ready = true;
    state.current = Position {
        spine: seed.spine,
        page: seed.page,
    };
    state.next_page = 2;
    state.cached.pages = 8;
    state.facts.book = !seed.book.is_null();
    let guarded = owner.guarded.get_mut();
    guarded.native = Some(NonNull::new(seed.native).expect("fixture root"));
    guarded.book = NonNull::new(seed.book);
    guarded.section = NonNull::new(seed.section);
    owner.estimate_observer = Some(EstimateObserver {
        context: seed.estimate_context,
        read: seed.estimate_read,
    });
    {
        let access = owner.borrowed_resources();
        owner.publish_section(&access);
    }
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_position_destroy(owner: *mut c_void) {
    let mut owner = Box::from_raw(owner.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        owner.drop_section(&access);
    }
    // The root and book borrow native fixture objects.
    owner.guarded.get_mut().native = None;
    owner.guarded.get_mut().book = None;
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_position_loop(
    owner: *mut c_void,
    out: *mut abi::XpEpubPosition,
) {
    *out = (&*owner.cast::<EpubSession>()).loop_current_position();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_position_borrowed(
    owner: *mut c_void,
    out: *mut abi::XpEpubPosition,
) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    *out = owner.current_position(&access);
}
