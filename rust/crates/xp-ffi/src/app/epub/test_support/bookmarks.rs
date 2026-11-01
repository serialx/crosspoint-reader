extern crate alloc;

use super::super::{abi, app_abi, state::Position, EpubSession};
use super::{BookmarkOverrides, EstimateObserver};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};
use xp_ui::reader::bookmark::{self, BookmarkPosition, ProgressRange};

#[repr(C)]
pub struct BookmarkSeed {
    pub book: *mut c_void,
    pub section: *mut c_void,
    pub bookmarks: *mut c_void,
    pub spine: i32,
    pub page: i32,
    pub visible_offset: u32,
    pub history: [i32; 6],
    pub has_visible_offset: u8,
    pub removed: u8,
    pub bookmarked: u8,
    pub history_depth: u8,
    pub context: *mut c_void,
    pub position: unsafe extern "C" fn(*mut c_void, *mut abi::XpEpubPosition),
    pub sanitize: unsafe extern "C" fn(*mut c_void, app_abi::Text),
    pub estimate: unsafe extern "C" fn(*mut c_void, u16),
}
#[repr(C)]
pub struct BookmarkObservation {
    pub history: [i32; 6],
    pub removed: u8,
    pub bookmarked: u8,
    pub history_depth: u8,
    pub has_section: u8,
}
#[repr(C)]
pub struct BookmarkRange {
    pub start: f32,
    pub end: f32,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_create(
    seed: *const BookmarkSeed,
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
    state.visible_offset = (seed.has_visible_offset != 0).then_some(seed.visible_offset);
    state.bookmark_removed = seed.removed != 0;
    state.bookmarked = seed.bookmarked != 0;
    state.facts.book = !seed.book.is_null();
    assert!(seed.history_depth <= 3);
    state.history.depth = seed.history_depth;
    for (position, pair) in state
        .history
        .positions
        .iter_mut()
        .zip(seed.history.chunks_exact(2))
    {
        *position = Position {
            spine: pair[0],
            page: pair[1],
        };
    }
    let guarded = owner.guarded.get_mut();
    guarded.book = NonNull::new(seed.book);
    guarded.section = NonNull::new(seed.section);
    guarded.bookmarks = Some(NonNull::new(seed.bookmarks).expect("fixture bookmarks"));
    owner.bookmark_overrides = Some(BookmarkOverrides {
        context: seed.context,
        position: seed.position,
        sanitize: seed.sanitize,
    });
    owner.estimate_observer = Some(EstimateObserver {
        context: seed.context,
        read: seed.estimate,
    });
    {
        let access = owner.borrowed_resources();
        owner.publish_section(&access);
    }
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_destroy(owner: *mut c_void) {
    let owner = Box::from_raw(owner.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        owner.drop_section(&access);
    }
    // Book and bookmark vector remain fixture-owned.
    drop(owner);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_observe(
    owner: *mut c_void,
    out: *mut BookmarkObservation,
) {
    *out = (&*owner.cast::<EpubSession>()).model(|state| {
        let mut history = [0; 6];
        for (pair, position) in history.chunks_exact_mut(2).zip(state.history.positions) {
            pair[0] = position.spine;
            pair[1] = position.page;
        }
        BookmarkObservation {
            history,
            removed: u8::from(state.bookmark_removed),
            bookmarked: u8::from(state.bookmarked),
            history_depth: state.history.depth,
            has_section: u8::from(state.facts.section.is_some()),
        }
    });
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_flag(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).loop_update_bookmark_flag();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_add(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).add_bookmark();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_load(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).loop_load_cached_bookmarks();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_save_links(owner: *mut c_void) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.save_links(&access);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_load_links(owner: *mut c_void) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.load_links(&access);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_seed_history(
    owner: *mut c_void,
    depth: u8,
    values: *const i32,
) {
    assert!(depth <= 3);
    let values = core::slice::from_raw_parts(values, 6);
    (&*owner.cast::<EpubSession>()).model(|state| {
        state.history.depth = depth;
        for (position, pair) in state
            .history
            .positions
            .iter_mut()
            .zip(values.chunks_exact(2))
        {
            *position = Position {
                spine: pair[0],
                page: pair[1],
            };
        }
    });
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_drop_section(owner: *mut c_void) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.lock_resources();
    owner.drop_section(&access);
}
#[no_mangle]
pub extern "C" fn xp_epub_test_bookmark_range(page: i32, count: i32) -> BookmarkRange {
    let range = bookmark::range(page, count);
    BookmarkRange {
        start: range.start,
        end: range.end,
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_bookmark_matches(
    row: *const abi::XpEpubBookmark,
    spine: i32,
    page: i32,
    count: i32,
    range: BookmarkRange,
) -> u8 {
    let row = &*row;
    u8::from(bookmark::matches(
        BookmarkPosition {
            percentage: row.percentage,
            spine: i32::from(row.spine),
            page: i32::from(row.chapter_page),
            count: i32::from(row.page_count),
        },
        spine,
        page,
        count,
        ProgressRange {
            start: range.start,
            end: range.end,
        },
    ))
}
