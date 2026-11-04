extern crate alloc;
use super::super::{
    abi, app_abi,
    state::{Notice, Overlay, TimedNotice},
    EpubSession,
};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};
use xp_ui::reader::idle::Snapshot;

#[repr(C)]
pub struct Seed {
    native: *mut c_void,
    book: *mut c_void,
    popup: *mut c_void,
    sample: Snapshot,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_idle_create_legacy_book(
    seed: *const Seed,
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
    let s = &seed.sample;
    owner.book_factory = super::super::session_book::Factory::Legacy(legacy_book);
    owner.legacy_pages.set(Some(&legacy_book.section.page));
    let state = owner.state.get_mut();
    state.ready = true;
    state.current.spine = s.spine;
    state.base.remembered = s.remembered != 0;
    state.base.end_ready = s.end_options != 0;
    state.overlay = match s.overlay {
        0 => Overlay::Page,
        1 => Overlay::Toolbar,
        2 => Overlay::Contents,
        3 => Overlay::Text,
        4 => Overlay::More,
        _ => panic!("fixture overlay"),
    };
    state.auto_turn = s.automatic != 0;
    state.manual_turn = s.pending_turn;
    let notice = |present| {
        (present != 0).then_some(TimedNotice {
            message: Notice::BookmarkAdded,
            since: 0,
        })
    };
    state.bookmark_notice = notice(s.bookmark_notice);
    state.dictionary_notice = notice(s.dictionary_notice);
    state.font_notice = notice(s.font_notice);
    state.clipping_notice = notice(s.clipping_notice);
    state.move_to_read = s.read_move != 0;
    state.recents_removed = s.recents_removed != 0;
    state.orientation = s.applied_orientation;
    state.history.depth = s.footnote_depth;
    let guarded = owner.guarded.get_mut();
    guarded.native = NonNull::new(seed.native);
    guarded.failure_popup = NonNull::new(seed.popup);
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_idle(raw: *mut c_void) -> u8 {
    u8::from((&*raw.cast::<EpubSession>()).try_idle_loop())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_idle_destroy(raw: *mut c_void) {
    let mut owner = Box::from_raw(raw.cast::<EpubSession>());
    owner.discard_legacy_fixture();
    let guarded = owner.guarded.get_mut();
    guarded.native = None;
    guarded.failure_popup = None;
    drop(owner);
}
