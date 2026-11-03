extern crate alloc;

use super::{
    super::{abi, app_abi, state::Notice, EpubSession},
    BookmarkOverrides, ClippingHooks, EstimateObserver, LifetimeOverrides, NavigationHooks,
};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_has_child(raw: *const c_void) -> u8 {
    let owner = &*raw.cast::<EpubSession>();
    u8::from(owner.model(|state| !matches!(state.child, super::super::state::ChildIntent::None)))
}

#[derive(Clone, Copy)]
pub(in crate::app::epub) struct ChildHooks {
    pub context: *mut c_void,
    pub bookmark: unsafe extern "C" fn(*mut c_void),
    pub menu: unsafe extern "C" fn(*mut c_void),
    pub progress: unsafe extern "C" fn(*mut c_void, *const app_abi::XpAppProgressResult),
    pub flush: unsafe extern "C" fn(*mut c_void),
    pub save_progress: unsafe extern "C" fn(*mut c_void, i32, i32, i32) -> u8,
    pub navigate: unsafe extern "C" fn(*mut c_void, app_abi::Text, u8),
    pub hit: unsafe extern "C" fn(*mut c_void, *mut c_void, i32, i32) -> i32,
    pub replace: unsafe extern "C" fn(*mut c_void, *mut c_void),
}

#[repr(C)]
pub struct Seed {
    native: *mut c_void,
    book: *mut c_void,
    section: *mut c_void,
    spine: i32,
    page: i32,
    next_page: i32,
    cached_count: i32,
    width: u16,
    height: u16,
    history_depth: u8,
    context: *mut c_void,
    bookmark: unsafe extern "C" fn(*mut c_void),
    menu: unsafe extern "C" fn(*mut c_void),
    progress: unsafe extern "C" fn(*mut c_void, *const app_abi::XpAppProgressResult),
    flush: unsafe extern "C" fn(*mut c_void),
    save_progress: unsafe extern "C" fn(*mut c_void, i32, i32, i32) -> u8,
    navigate: unsafe extern "C" fn(*mut c_void, app_abi::Text, u8),
    hit: unsafe extern "C" fn(*mut c_void, *mut c_void, i32, i32) -> i32,
    replace: unsafe extern "C" fn(*mut c_void, *mut c_void),
    position: unsafe extern "C" fn(*mut c_void, *mut abi::XpEpubPosition),
    signature: unsafe extern "C" fn(*mut c_void, u16, u16) -> u32,
    estimate: unsafe extern "C" fn(*mut c_void, i32),
    discard: unsafe extern "C" fn(*mut c_void),
    save_links: unsafe extern "C" fn(*mut c_void),
    update: unsafe extern "C" fn(*mut c_void),
    push: unsafe extern "C" fn(*mut c_void, *mut c_void),
    finish: unsafe extern "C" fn(*mut c_void),
}

#[repr(C)]
pub struct Observation {
    page: i32,
    next_page: i32,
    spine: i32,
    visible_offset: u32,
    dictionary_time: u32,
    bookmark_time: u32,
    clipping_time: u32,
    sync_error: u8,
    dictionary: u8,
    bookmark: u8,
    clipping: u8,
    clipping_message: u8,
    has_offset: u8,
    has_book: u8,
    has_section: u8,
}

unsafe extern "C" fn unexpected(_: *mut c_void) {
    panic!("unexpected child fixture method");
}
unsafe extern "C" fn sanitize(_: *mut c_void, _: app_abi::Text) {
    panic!("unexpected child fixture sanitize");
}
unsafe extern "C" fn destination(
    _: *mut c_void,
    _: app_abi::Text,
    _: app_abi::TextSink,
    _: *mut c_void,
) -> u8 {
    panic!("unexpected child fixture destination");
}
unsafe extern "C" fn move_book(
    _: *mut c_void,
    _: app_abi::Text,
    _: app_abi::Text,
    _: app_abi::Text,
) {
    panic!("unexpected child fixture move");
}
unsafe extern "C" fn nonempty(_: *mut c_void, _: u8) {}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_create_legacy_book(
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
    owner.book_factory = super::super::session_book::Factory::Legacy(legacy_book);
    let state = owner.state.get_mut();
    state.ready = true;
    state.current.spine = seed.spine;
    state.current.page = seed.page;
    state.next_page = seed.next_page;
    state.cached.pages = seed.cached_count;
    state.viewport = (seed.width, seed.height);
    state.history.depth = seed.history_depth;
    state.facts.book = !seed.book.is_null();
    let guarded = owner.guarded.get_mut();
    guarded.native = NonNull::new(seed.native);
    guarded.section = NonNull::new(seed.section);
    owner.child_hooks = Some(ChildHooks {
        context: seed.context,
        bookmark: seed.bookmark,
        menu: seed.menu,
        progress: seed.progress,
        flush: seed.flush,
        save_progress: seed.save_progress,
        navigate: seed.navigate,
        hit: seed.hit,
        replace: seed.replace,
    });
    owner.bookmark_overrides = Some(BookmarkOverrides {
        context: seed.context,
        position: seed.position,
        sanitize,
    });
    owner.clipping_hooks = Some(ClippingHooks {
        context: seed.context,
        signature: seed.signature,
        nonempty,
    });
    owner.estimate_observer = Some(EstimateObserver {
        context: seed.context,
        read: seed.estimate,
    });
    owner.lifetime_overrides = Some(LifetimeOverrides {
        context: seed.context,
        settle_overlay: unexpected,
        discard_overlay: seed.discard,
        save_links: seed.save_links,
        load_links: unexpected,
        load_bookmarks: unexpected,
        disable_fast: unexpected,
        destination,
        move_book,
    });
    owner.navigation_hooks = Some(NavigationHooks {
        context: seed.context,
        finish: seed.finish,
        push: seed.push,
        reopen: sanitize,
        update: seed.update,
    });
    {
        let access = owner.borrowed_resources();
        owner.publish_section(&access);
    }
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_destroy(raw: *mut c_void) {
    // The historical fixture retains its physical owner handles until scope exit.
    drop(Box::from_raw(raw.cast::<EpubSession>()));
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_observe(raw: *mut c_void, out: *mut Observation) {
    let owner = &*raw.cast::<EpubSession>();
    *out = owner.model(|state| Observation {
        page: state.current.page,
        next_page: state.next_page,
        spine: state.current.spine,
        visible_offset: state.visible_offset.unwrap_or(0),
        dictionary_time: state.dictionary_notice.map_or(0, |notice| notice.since),
        bookmark_time: state.bookmark_notice.map_or(0, |notice| notice.since),
        clipping_time: state.clipping_notice.map_or(0, |notice| notice.since),
        sync_error: u8::from(state.sync_save_error),
        dictionary: u8::from(state.dictionary_notice.is_some()),
        bookmark: u8::from(state.bookmark_notice.is_some()),
        clipping: u8::from(state.clipping_notice.is_some()),
        clipping_message: state
            .clipping_notice
            .map_or(5, |notice| match notice.message {
                Notice::ClipRemoved => 0,
                Notice::ClipDeleteFailed => 1,
                Notice::ClipLimit => 2,
                Notice::ClipSaved => 3,
                Notice::ClipExportFailed => 4,
                _ => 5,
            }),
        has_offset: u8::from(state.visible_offset.is_some()),
        has_book: u8::from(state.facts.book),
        has_section: u8::from(state.facts.section.is_some()),
    });
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_spine(raw: *mut c_void, spine: i32) {
    (&*raw.cast::<EpubSession>()).model(|state| state.current.spine = spine);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_drop_section(raw: *mut c_void) {
    let owner = &*raw.cast::<EpubSession>();
    // The interleaving fixture holds the physical render lock on this task.
    let access = owner.borrowed_resources();
    owner.drop_section(&access);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_dictionary(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).open_dictionary_word_select();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_selection(raw: *mut c_void, x: i32, y: i32, mode: u8) {
    (&*raw.cast::<EpubSession>()).start_clip_selection(x, y, mode);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_footnotes(raw: *mut c_void, reopen: u8) {
    (&*raw.cast::<EpubSession>()).open_footnotes(reopen != 0);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_clippings(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).open_clippings();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_sync(raw: *mut c_void) -> u8 {
    (&*raw.cast::<EpubSession>()).open_sync();
    1
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_result(
    raw: *mut c_void,
    result: *const app_abi::XpAppResultView,
) {
    (&*raw.cast::<EpubSession>()).child_result(result.as_ref());
}
