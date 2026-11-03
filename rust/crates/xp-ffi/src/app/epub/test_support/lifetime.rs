extern crate alloc;

use super::super::{abi, app_abi, state::LoadFailure, EpubSession};
use super::LifetimeOverrides;
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct LifetimeSeed {
    pub native: *mut c_void,
    pub book: *mut c_void,
    pub section: *mut c_void,
    pub path: app_abi::Text,
    pub spine: i32,
    pub next_page: i32,
    pub cached_spine: i32,
    pub cached_count: i32,
    pub cached_offset: u32,
    pub has_cached_offset: u8,
    pub radio_released: u8,
    pub history_depth: u8,
    pub move_to_read: u8,
    pub context: *mut c_void,
    pub settle_overlay: unsafe extern "C" fn(*mut c_void),
    pub discard_overlay: unsafe extern "C" fn(*mut c_void),
    pub save_links: unsafe extern "C" fn(*mut c_void),
    pub load_links: unsafe extern "C" fn(*mut c_void),
    pub load_bookmarks: unsafe extern "C" fn(*mut c_void),
    pub disable_fast: unsafe extern "C" fn(*mut c_void),
    pub destination:
        unsafe extern "C" fn(*mut c_void, app_abi::Text, app_abi::TextSink, *mut c_void) -> u8,
    pub move_book: unsafe extern "C" fn(*mut c_void, app_abi::Text, app_abi::Text, app_abi::Text),
}

#[repr(C)]
pub struct LifetimeObservation {
    pub spine: i32,
    pub next_page: i32,
    pub cached_spine: i32,
    pub cached_count: i32,
    pub cached_offset: u32,
    pub has_book: u8,
    pub has_cached_offset: u8,
    pub failure: u8,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifetime_create_legacy_book(
    seed: *const LifetimeSeed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
    legacy_book: &'static super::legacy_book::LegacyBookServices,
) -> *mut c_void {
    let seed = &*seed;
    let Some(path) = seed.path.bytes() else {
        return core::ptr::null_mut();
    };
    let Some(mut owner) = EpubSession::try_new(path, false, 0, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    owner.book_factory = super::super::session_book::Factory::Legacy(legacy_book);
    let state = owner.state.get_mut();
    state.current.spine = seed.spine;
    state.next_page = seed.next_page;
    state.cached.spine = seed.cached_spine;
    state.cached.pages = seed.cached_count;
    state.cached.offset = (seed.has_cached_offset != 0).then_some(seed.cached_offset);
    state.facts.radio_released = seed.radio_released != 0;
    state.facts.book = !seed.book.is_null();
    state.facts.section = (!seed.section.is_null()).then_some(Default::default());
    state.history.depth = seed.history_depth;
    state.move_to_read = seed.move_to_read != 0;
    let guarded = owner.guarded.get_mut();
    guarded.native = Some(NonNull::new(seed.native).expect("fixture resource root"));
    guarded.section = NonNull::new(seed.section);
    owner.lifetime_overrides = Some(LifetimeOverrides {
        context: seed.context,
        settle_overlay: seed.settle_overlay,
        discard_overlay: seed.discard_overlay,
        save_links: seed.save_links,
        load_links: seed.load_links,
        load_bookmarks: seed.load_bookmarks,
        disable_fast: seed.disable_fast,
        destination: seed.destination,
        move_book: seed.move_book,
    });
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifetime_destroy(owner: *mut c_void) {
    let mut owner = Box::from_raw(owner.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        owner.teardown_book(&access);
    }
    assert!(owner.guarded.get_mut().book.is_none());
    assert!(owner.guarded.get_mut().section.is_none());
    // The resource root borrows the fixture, while book and Section were transferred.
    owner.guarded.get_mut().native = None;
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifetime_load(owner: *mut c_void) -> u8 {
    let owner = &*owner.cast::<EpubSession>();
    let loaded = owner.entry_load_book();
    assert!(owner.model(|state| state.ready));
    u8::from(loaded)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifetime_observe(
    owner: *mut c_void,
    out: *mut LifetimeObservation,
) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    let has_book = u8::from(owner.book(&access).is_some());
    *out = owner.model(|state| LifetimeObservation {
        spine: state.current.spine,
        next_page: state.next_page,
        cached_spine: state.cached.spine,
        cached_count: state.cached.pages,
        cached_offset: state.cached.offset.unwrap_or(0),
        has_book,
        has_cached_offset: u8::from(state.cached.offset.is_some()),
        failure: match state.failure {
            LoadFailure::None => 0,
            LoadFailure::Protected => 1,
            LoadFailure::Expired => 2,
            LoadFailure::Unverified => 3,
        },
    });
}
