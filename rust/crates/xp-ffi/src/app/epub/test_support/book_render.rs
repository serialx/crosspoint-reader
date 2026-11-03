extern crate alloc;
use super::super::{
    abi, app_abi,
    state::{Notice, Overlay, Position, TimedNotice},
    EpubSession, Text,
};
use super::NavigationHooks;
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct BookRenderHooks {
    pub context: *mut c_void,
    pub settle: unsafe extern "C" fn(*mut c_void),
    pub room: unsafe extern "C" fn(*mut c_void) -> u8,
    pub deferred: unsafe extern "C" fn(*mut c_void) -> u8,
    pub status: unsafe extern "C" fn(*mut c_void),
    pub bookmark: unsafe extern "C" fn(*mut c_void),
    pub contents: unsafe extern "C" fn(*mut c_void, *mut c_void, *const i32),
    pub save: unsafe extern "C" fn(*mut c_void, i32, i32, i32) -> u8,
    pub toolbar: unsafe extern "C" fn(*mut c_void) -> u8,
    pub overlay: unsafe extern "C" fn(*mut c_void),
    pub push: unsafe extern "C" fn(*mut c_void),
    pub clear: unsafe extern "C" fn(*mut c_void),
    pub discard: unsafe extern "C" fn(*mut c_void),
    pub mark: unsafe extern "C" fn(*mut c_void),
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct BookValues {
    pub spine: i32,
    pub page: i32,
    pub next_page: i32,
    pub cached_spine: i32,
    pub cached_count: i32,
    pub last_spine: i32,
    pub last_page: i32,
    pub last_count: i32,
    pub pending_page: i32,
    pub has_pending_page: i32,
    pub pending_offset: u32,
    pub has_pending_offset: i32,
    pub cached_offset: u32,
    pub has_cached_offset: i32,
    pub visible_offset: u32,
    pub has_visible_offset: i32,
    pub partial_failed: i32,
    pub percent: i32,
    pub popup: i32,
    pub stale: i32,
    pub automatic: i32,
    pub sync_error: i32,
    pub screenshot: i32,
    pub bookmark_notice: i32,
    pub bookmark_removed: i32,
    pub dictionary_notice: i32,
    pub font_notice: i32,
    pub clipping_notice: i32,
    pub stored: i32,
    pub overlay: i32,
    pub retry: i32,
    pub refresh: i32,
    pub width: i32,
    pub height: i32,
    pub left: i32,
    pub top: i32,
    pub last_render: u32,
    pub font_since: u32,
    pub rendered: i32,
    pub spine_progress: f32,
}
#[repr(C)]
pub struct BookSeed {
    pub native: *mut c_void,
    pub book: *mut c_void,
    pub section: *mut c_void,
    pub popup: *mut c_void,
    pub anchor: app_abi::Text,
    pub values: BookValues,
    pub hooks: BookRenderHooks,
    pub update: unsafe extern "C" fn(*mut c_void),
}
#[repr(C)]
pub struct BookObservation {
    pub values: BookValues,
    pub section: *mut c_void,
    pub anchor: app_abi::Text,
    pub published_pages: i32,
    pub published_estimate: i32,
    pub published_building: u8,
    pub published_complete: u8,
    pub published_partial: u8,
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_book_create_legacy_book(
    seed: *const BookSeed,
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
    let anchor = Text::copy(seed.anchor.bytes().expect("fixture anchor"))
        .expect("fixture anchor allocation");
    let v = seed.values;
    owner.book_factory = super::super::session_book::Factory::Legacy(legacy_book);
    let state = owner.state.get_mut();
    // Standalone renderBook composition; this seed does not prove entry gating.
    state.ready = true;
    state.current = Position {
        spine: v.spine,
        page: v.page,
    };
    state.next_page = v.next_page;
    state.cached.spine = v.cached_spine;
    state.cached.pages = v.cached_count;
    state.cached.offset = (v.has_cached_offset != 0).then_some(v.cached_offset);
    state.visible_offset = (v.has_visible_offset != 0).then_some(v.visible_offset);
    state.deferred.page = (v.has_pending_page != 0).then_some(v.pending_page as u16);
    state.deferred.offset = (v.has_pending_offset != 0).then_some(v.pending_offset);
    state.deferred.percent = v.percent != 0;
    state.deferred.spine_progress = v.spine_progress;
    state.last_saved = (v.last_spine, v.last_page, v.last_count);
    state.partial_start_failed = v.partial_failed != 0;
    state.build_popup = v.popup != 0;
    state.facts.frame_stale = v.stale != 0;
    state.auto_turn = v.automatic != 0;
    state.sync_save_error = v.sync_error != 0;
    state.screenshot = v.screenshot != 0;
    state.bookmark_removed = v.bookmark_removed != 0;
    state.bookmark_notice = (v.bookmark_notice != 0).then_some(TimedNotice {
        message: if v.bookmark_removed != 0 {
            Notice::BookmarkRemoved
        } else {
            Notice::BookmarkAdded
        },
        since: 0,
    });
    state.dictionary_notice = (v.dictionary_notice != 0).then_some(TimedNotice {
        message: Notice::Dictionary,
        since: 0,
    });
    state.font_notice = (v.font_notice != 0).then_some(TimedNotice {
        message: Notice::FontDamaged,
        since: v.font_since,
    });
    state.clipping_notice = (v.clipping_notice != 0).then_some(TimedNotice {
        message: Notice::ClipSaved,
        since: 0,
    });
    state.page_stored = v.stored != 0;
    state.overlay = match v.overlay {
        0 => Overlay::Page,
        1 => Overlay::Toolbar,
        2 => Overlay::Contents,
        3 => Overlay::Text,
        4 => Overlay::More,
        _ => panic!("fixture overlay"),
    };
    state.page_retries = v.retry as u8;
    state.base.refresh_counter = v.refresh;
    state.viewport = (v.width as u16, v.height as u16);
    state.link_margins = (v.left, v.top);
    state.last_render_ms = v.last_render;
    state.facts.book = !seed.book.is_null();
    let guard = owner.guarded.get_mut();
    guard.native = NonNull::new(seed.native);
    guard.section = NonNull::new(seed.section);
    guard.overlay_popup = NonNull::new(seed.popup);
    guard.anchor = anchor;
    owner.book_render_hooks = Some(seed.hooks);
    unsafe extern "C" fn unexpected_finish(_: *mut c_void) {
        panic!("unexpected book-render finish");
    }
    unsafe extern "C" fn unexpected_push(_: *mut c_void, _: *mut c_void) {
        panic!("unexpected book-render child push");
    }
    unsafe extern "C" fn unexpected_reopen(_: *mut c_void, _: app_abi::Text) {
        panic!("unexpected book-render reopen");
    }
    owner.navigation_hooks = Some(NavigationHooks {
        context: seed.hooks.context,
        finish: unexpected_finish,
        push: unexpected_push,
        reopen: unexpected_reopen,
        update: seed.update,
    });
    let access = owner.borrowed_resources();
    owner.publish_section(&access);
    drop(access);
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_book_render(raw: *mut c_void) {
    let owner = &*raw.cast::<EpubSession>();
    assert!(owner.render_with(|access| owner.render_book(access)));
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_book_observe(raw: *mut c_void, out: *mut BookObservation) {
    let owner = &*raw.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    *out = owner.model(|s| BookObservation {
        section: owner
            .section(&access)
            .map_or(core::ptr::null_mut(), NonNull::as_ptr),
        anchor: owner.anchor(&access),
        published_pages: s.facts.section.map_or(-1, |f| i32::from(f.pages)),
        published_estimate: s.facts.section.map_or(-1, |f| f.estimated_pages),
        published_building: u8::from(s.facts.section.is_some_and(|f| f.building)),
        published_complete: u8::from(s.facts.section.is_some_and(|f| f.complete)),
        published_partial: u8::from(s.facts.section.is_some_and(|f| f.partial)),
        values: BookValues {
            spine: s.current.spine,
            page: s.current.page,
            next_page: s.next_page,
            cached_spine: s.cached.spine,
            cached_count: s.cached.pages,
            last_spine: s.last_saved.0,
            last_page: s.last_saved.1,
            last_count: s.last_saved.2,
            pending_page: i32::from(s.deferred.page.unwrap_or(0)),
            has_pending_page: i32::from(s.deferred.page.is_some()),
            pending_offset: s.deferred.offset.unwrap_or(0),
            has_pending_offset: i32::from(s.deferred.offset.is_some()),
            cached_offset: s.cached.offset.unwrap_or(0),
            has_cached_offset: i32::from(s.cached.offset.is_some()),
            visible_offset: s.visible_offset.unwrap_or(0),
            has_visible_offset: i32::from(s.visible_offset.is_some()),
            partial_failed: i32::from(s.partial_start_failed),
            percent: i32::from(s.deferred.percent),
            popup: i32::from(s.build_popup),
            stale: i32::from(s.facts.frame_stale),
            automatic: i32::from(s.auto_turn),
            sync_error: i32::from(s.sync_save_error),
            screenshot: i32::from(s.screenshot),
            bookmark_notice: i32::from(s.bookmark_notice.is_some()),
            bookmark_removed: i32::from(s.bookmark_removed),
            dictionary_notice: i32::from(s.dictionary_notice.is_some()),
            font_notice: i32::from(s.font_notice.is_some()),
            clipping_notice: i32::from(s.clipping_notice.is_some()),
            stored: i32::from(s.page_stored),
            overlay: match s.overlay {
                Overlay::Page => 0,
                Overlay::Toolbar => 1,
                Overlay::Contents => 2,
                Overlay::Text => 3,
                Overlay::More => 4,
            },
            retry: i32::from(s.page_retries),
            refresh: s.base.refresh_counter,
            width: i32::from(s.viewport.0),
            height: i32::from(s.viewport.1),
            left: s.link_margins.0,
            top: s.link_margins.1,
            last_render: s.last_render_ms,
            font_since: s.font_notice.map_or(0, |n| n.since),
            rendered: i32::from(s.base.rendered),
            spine_progress: s.deferred.spine_progress,
        },
    });
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_book_destroy(raw: *mut c_void) {
    let mut owner = Box::from_raw(raw.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        owner.drop_section(&access);
    }
    assert!(owner.guarded.get_mut().active_frame.is_none());
    // Native root, book and popup are fixture-owned; Section is owned by Rust.
    owner.guarded.get_mut().native = None;
    owner.discard_legacy_fixture();
    owner.guarded.get_mut().overlay_popup = None;
    drop(owner);
}
