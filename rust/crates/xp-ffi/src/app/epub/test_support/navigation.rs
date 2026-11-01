extern crate alloc;

use super::super::{abi, app_abi, state::Position, EpubSession, Text};
use super::{EstimateObserver, NavigationObservation, NavigationSeed};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_navigation_create(
    seed: *const NavigationSeed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
) -> *mut c_void {
    let seed = &*seed;
    let Some(mut owner) = EpubSession::try_new(b"", false, 3, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    let Some(anchor) = seed.anchor.bytes().and_then(|bytes| Text::copy(bytes).ok()) else {
        return core::ptr::null_mut();
    };
    // The fixture has not published this owner or transferred its handles yet.
    let state = owner.state.get_mut();
    state.ready = true;
    state.current = Position {
        spine: seed.spine,
        page: seed.active_page,
    };
    state.next_page = seed.next_page;
    state.cached.spine = seed.cached_spine;
    state.cached.pages = seed.cached_count;
    state.cached.offset = (seed.has_cached_offset != 0).then_some(seed.cached_offset);
    state.visible_offset = (seed.has_visible_offset != 0).then_some(seed.visible_offset);
    assert!(seed.history_depth <= 3);
    state.history.depth = seed.history_depth;
    for (slot, pair) in state
        .history
        .positions
        .iter_mut()
        .zip(seed.history.chunks_exact(2))
    {
        *slot = Position {
            spine: pair[0],
            page: pair[1],
        };
    }
    state.facts.book = !seed.book.is_null();
    let guarded = owner.guarded.get_mut();
    guarded.book = NonNull::new(seed.book);
    guarded.section = NonNull::new(seed.section);
    guarded.anchor = anchor;
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
pub unsafe extern "C" fn xp_epub_test_navigation_destroy(owner: *mut c_void) {
    let owner = Box::from_raw(owner.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        owner.drop_section(&access);
    }
    // The book belongs to the native fixture; this harness never runs exit.
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_navigation_observe(
    owner: *mut c_void,
    out: *mut NavigationObservation,
) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    let anchor = (*owner.guarded(&access)).anchor.wire();
    *out = owner.model(|state| {
        let mut history = [0; 6];
        for (pair, position) in history.chunks_exact_mut(2).zip(state.history.positions) {
            pair[0] = position.spine;
            pair[1] = position.page;
        }
        NavigationObservation {
            spine: state.current.spine,
            next_page: state.next_page,
            active_page: state.current.page,
            cached_count: state.cached.pages,
            cached_offset: state.cached.offset.unwrap_or(0),
            pending_offset: state.deferred.offset.unwrap_or(0),
            spine_progress: state.deferred.spine_progress,
            anchor,
            history,
            history_depth: state.history.depth,
            has_section: u8::from(state.facts.section.is_some()),
            has_cached_offset: u8::from(state.cached.offset.is_some()),
            has_pending_offset: u8::from(state.deferred.offset.is_some()),
            pending_percent: u8::from(state.deferred.percent),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_navigation_percent(owner: *mut c_void, percent: i32) {
    (&*owner.cast::<EpubSession>()).jump_to_percent(percent);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_navigation_progress(
    owner: *mut c_void,
    progress: *const app_abi::XpAppProgressResult,
) {
    (&*owner.cast::<EpubSession>()).apply_progress(&*progress);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_navigation_deferred(owner: *mut c_void) -> u8 {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    u8::from(owner.apply_deferred(&access))
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_navigation_clear(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).clear_deferred();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_navigation_save(
    owner: *mut c_void,
    spine: i32,
    page: i32,
    count: i32,
) -> u8 {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    u8::from(owner.save_progress(&access, spine, page, count))
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_navigation_remember(owner: *mut c_void) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.remember_offset(&access);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_navigation_href(
    owner: *mut c_void,
    href: app_abi::Text,
    save: u8,
) {
    (&*owner.cast::<EpubSession>()).navigate_href(href.bytes().expect("fixture href"), save != 0);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_navigation_restore(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).restore_position();
}
