extern crate alloc;
use super::super::{
    abi, app_abi,
    projection::{ChapterPosition, RemoteShortcut},
    state::{Overlay, Position},
    EpubSession,
};
use super::{EstimateObserver, ProjectionHooks};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct ProjectionSeed {
    pub native: *mut c_void,
    pub book: *mut c_void,
    pub section: *mut c_void,
    pub spine: i32,
    pub page: i32,
    pub next_page: i32,
    pub cached_count: i32,
    pub last_turn: u32,
    pub pending_page: u16,
    pub automatic: u8,
    pub sync_error: u8,
    pub overlay: u8,
    pub context: *mut c_void,
    pub toolbar_menu: unsafe extern "C" fn(*mut c_void) -> u8,
    pub base_input: unsafe extern "C" fn(*mut c_void) -> u8,
    pub acknowledge: unsafe extern "C" fn(*mut c_void),
    pub clear: unsafe extern "C" fn(*mut c_void),
    pub fallback: unsafe extern "C" fn(*mut c_void),
    pub estimate: unsafe extern "C" fn(*mut c_void, i32),
}
#[repr(C)]
pub struct ProjectionObservation {
    pub spine: i32,
    pub page: i32,
    pub next_page: i32,
    pub last_turn: u32,
    pub pending_page: u16,
    pub automatic: u8,
    pub sync_error: u8,
    pub has_section: u8,
}
#[repr(C)]
pub struct TestChapterPosition {
    pub page: i32,
    pub count: i32,
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_create_legacy_book(
    seed: *const ProjectionSeed,
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
    state.current = Position {
        spine: seed.spine,
        page: seed.page,
    };
    state.next_page = seed.next_page;
    state.cached.pages = seed.cached_count;
    state.last_turn_ms = seed.last_turn;
    state.deferred.page = Some(seed.pending_page);
    state.auto_turn = seed.automatic != 0;
    state.sync_save_error = seed.sync_error != 0;
    state.overlay = if seed.overlay != 0 {
        Overlay::Toolbar
    } else {
        Overlay::Page
    };
    state.facts.book = !seed.book.is_null();
    let guarded = owner.guarded.get_mut();
    guarded.native = Some(NonNull::new(seed.native).expect("fixture root"));
    guarded.section = NonNull::new(seed.section);
    owner.projection_hooks = Some(ProjectionHooks {
        context: seed.context,
        toolbar_menu: seed.toolbar_menu,
        base_input: seed.base_input,
        acknowledge: seed.acknowledge,
        clear: seed.clear,
        fallback: seed.fallback,
    });
    owner.estimate_observer = Some(EstimateObserver {
        context: seed.context,
        read: seed.estimate,
    });
    {
        let access = owner.borrowed_resources();
        owner.publish_section(&access);
    }
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_destroy(owner: *mut c_void) {
    let mut owner = Box::from_raw(owner.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        owner.drop_section(&access);
    }
    owner.discard_legacy_fixture();
    owner.guarded.get_mut().native = None;
    drop(owner);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_observe(
    owner: *mut c_void,
    out: *mut ProjectionObservation,
) {
    *out = (&*owner.cast::<EpubSession>()).model(|state| ProjectionObservation {
        spine: state.current.spine,
        page: state.current.page,
        next_page: state.next_page,
        last_turn: state.last_turn_ms,
        pending_page: state.deferred.page.unwrap_or(0),
        automatic: u8::from(state.auto_turn),
        sync_error: u8::from(state.sync_save_error),
        has_section: u8::from(state.facts.section.is_some()),
    });
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_chapter(
    owner: *mut c_void,
    out: *mut TestChapterPosition,
) {
    let value = (&*owner.cast::<EpubSession>()).chapter_position();
    *out = TestChapterPosition {
        page: value.page,
        count: value.count,
    };
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_percent(
    owner: *mut c_void,
    value: TestChapterPosition,
) -> i32 {
    (&*owner.cast::<EpubSession>()).book_percent_for(ChapterPosition {
        page: value.page,
        count: value.count,
    })
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_toc(owner: *mut c_void) -> i32 {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.current_toc_index(&access)
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_screenshot(
    owner: *mut c_void,
    out: *mut app_abi::Screenshot,
) {
    *out = (&*owner.cast::<EpubSession>()).screenshot_info();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_position(
    owner: *mut c_void,
    out: *mut abi::XpEpubPosition,
) {
    *out = (&*owner.cast::<EpubSession>()).loop_current_position();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_remote(owner: *mut c_void, menu: u8) -> u8 {
    match (&*owner.cast::<EpubSession>()).remote_shortcut(menu != 0) {
        None => 0,
        Some(RemoteShortcut::Selection) => 1,
        Some(RemoteShortcut::Toolbar) => 2,
        Some(RemoteShortcut::Menu) => 3,
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_move(
    owner: *mut c_void,
    value: *const xp_ui::reader::PageMove,
    record: u8,
) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).apply_page_move(*value, record != 0))
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_basis(owner: *mut c_void) -> i32 {
    (&*owner.cast::<EpubSession>()).progress_basis_points()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_threshold(owner: *mut c_void) -> u32 {
    (&*owner.cast::<EpubSession>()).confirm_long_press_threshold()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_at_end(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).is_at_end())
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_return_end(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).return_from_end()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_rendered_end(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).end_rendered()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_input_over(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).input_over_page())
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_turn(owner: *mut c_void, value: u8) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).page_turn(value != 0))
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_skip(owner: *mut c_void, value: i32) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).skip_pages(value))
}

#[repr(C)]
pub struct TestChapterDisplay {
    pub display_page: i32,
    pub fraction: f32,
    pub has_total: u8,
    pub reserved: [u8; 3],
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_projection_display(
    raw: *mut c_void,
    out: *mut TestChapterDisplay,
) {
    let position = (&*raw.cast::<EpubSession>()).chapter_position();
    *out = TestChapterDisplay {
        display_page: position.display_page(),
        fraction: position.fraction(),
        has_total: u8::from(position.has_total()),
        reserved: [0; 3],
    };
}
