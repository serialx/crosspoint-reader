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
pub unsafe extern "C" fn xp_epub_test_position_create_legacy_book(
    seed: *const PositionSeed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
    legacy_book: &'static super::legacy_book::LegacyBookServices,
) -> *mut c_void {
    let seed = &*seed;
    let Some(mut owner) = EpubSession::try_new(b"", false, 3, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    owner.book_factory = super::super::session_book::Factory::Legacy(legacy_book);
    owner.legacy_pages.set(Some(&legacy_book.section.page));
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
    guarded.section =
        super::legacy_section::SectionSlot::legacy(seed.section, &legacy_book.section);
    owner.estimate_observer = Some(EstimateObserver {
        context: seed.estimate_context,
        read: seed.estimate_read,
    });
    {
        let access = owner.borrowed_resources();
        owner.publish_section(&access);
    }
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
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
    owner.discard_legacy_fixture();
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
