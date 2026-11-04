extern crate alloc;

use super::{
    super::{abi, app_abi, state::Overlay, EpubSession},
    LifetimeOverrides, NavigationHooks, OverlayHooks, ProjectionHooks,
};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
#[derive(Clone, Copy)]
pub(in crate::app::epub) struct MenuHooks {
    pub context: *mut c_void,
    pub chapter: unsafe extern "C" fn(*mut c_void, *mut i32),
    pub percent: unsafe extern "C" fn(*mut c_void, i32, i32) -> i32,
    pub orientation: unsafe extern "C" fn(*mut c_void, u8),
    pub automatic: unsafe extern "C" fn(*mut c_void, u8),
    pub progress: unsafe extern "C" fn(*mut c_void, *const app_abi::XpAppProgressResult),
    pub clear: unsafe extern "C" fn(*mut c_void),
    pub footnotes: unsafe extern "C" fn(*mut c_void, u8),
    pub jump: unsafe extern "C" fn(*mut c_void, i32),
    pub dictionary: unsafe extern "C" fn(*mut c_void),
    pub home: unsafe extern "C" fn(*mut c_void),
    pub save: unsafe extern "C" fn(*mut c_void, i32, i32, i32) -> u8,
    pub sync: unsafe extern "C" fn(*mut c_void),
    pub selection: unsafe extern "C" fn(*mut c_void, i32, i32, u8),
    pub clippings: unsafe extern "C" fn(*mut c_void),
    pub bookmark: unsafe extern "C" fn(*mut c_void),
    pub bluetooth: unsafe extern "C" fn(*mut c_void),
    pub build_items: unsafe extern "C" fn(*mut c_void, u8, u8, u8, *mut u8) -> u8,
    pub remember: unsafe extern "C" fn(*mut c_void),
    pub popup: unsafe extern "C" fn(*mut c_void),
}

#[repr(C)]
pub struct Seed {
    native: *mut c_void,
    book: *mut c_void,
    section: *mut c_void,
    toolbar: *mut c_void,
    popup: *mut c_void,
    bookmarks: *mut c_void,
    spine: i32,
    page: i32,
    cached_spine: i32,
    cached_count: i32,
    next_page: i32,
    focus: i32,
    manual_turn: i8,
    automatic: u8,
    overlay: u8,
    stored: u8,
    hold: u8,
    cursor: u8,
    more_actions: [u8; 19],
    more_count: u8,
    hooks: MenuHooks,
    toolbar_menu: unsafe extern "C" fn(*mut c_void) -> u8,
    toc: unsafe extern "C" fn(*mut c_void) -> i32,
    discard: unsafe extern "C" fn(*mut c_void),
    settle: unsafe extern "C" fn(*mut c_void),
    load_bookmarks: unsafe extern "C" fn(*mut c_void),
    render: unsafe extern "C" fn(*mut c_void),
    push_overlay: unsafe extern "C" fn(*mut c_void),
    update: unsafe extern "C" fn(*mut c_void),
    push_child: unsafe extern "C" fn(*mut c_void, *mut c_void),
}

#[repr(C)]
pub struct Observation {
    cached_spine: i32,
    cached_count: i32,
    next_page: i32,
    spine: i32,
    focus: i32,
    bookmark_time: u32,
    manual_turn: i8,
    automatic: u8,
    overlay: u8,
    stored: u8,
    hold: u8,
    cursor: u8,
    bookmarked: u8,
    screenshot: u8,
    has_section: u8,
    has_toolbar: u8,
    more_actions: [u8; 19],
    more_count: u8,
}

unsafe extern "C" fn unexpected(_: *mut c_void) {
    panic!("unexpected menu fixture method");
}
unsafe extern "C" fn unexpected_flag(_: *mut c_void) -> u8 {
    panic!("unexpected menu fixture flag");
}
unsafe extern "C" fn unexpected_note(_: *mut c_void, _: u8) {
    panic!("unexpected menu fixture note");
}
unsafe extern "C" fn unexpected_text(_: *mut c_void, _: app_abi::Text) {
    panic!("unexpected menu fixture text");
}
unsafe extern "C" fn destination(
    _: *mut c_void,
    _: app_abi::Text,
    _: app_abi::TextSink,
    _: *mut c_void,
) -> u8 {
    panic!("unexpected menu fixture destination");
}
unsafe extern "C" fn move_book(
    _: *mut c_void,
    _: app_abi::Text,
    _: app_abi::Text,
    _: app_abi::Text,
) {
    panic!("unexpected menu fixture move");
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_menu_create_legacy_book(
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
    owner.legacy_pages.set(Some(&legacy_book.section.page));
    let state = owner.state.get_mut();
    state.ready = true;
    state.current.spine = seed.spine;
    state.current.page = seed.page;
    state.cached.spine = seed.cached_spine;
    state.cached.pages = seed.cached_count;
    state.next_page = seed.next_page;
    state.focused_tool = seed.focus;
    state.manual_turn = seed.manual_turn;
    state.auto_option = seed.automatic;
    state.overlay = match seed.overlay {
        0 => Overlay::Page,
        1 => Overlay::Toolbar,
        2 => Overlay::Contents,
        3 => Overlay::Text,
        4 => Overlay::More,
        _ => panic!("fixture overlay"),
    };
    state.page_stored = seed.stored != 0;
    state.panel_hold_jumped = seed.hold != 0;
    state.panel_cursor = seed.cursor != 0;
    state.facts.book = !seed.book.is_null();
    let guarded = owner.guarded.get_mut();
    guarded.native = NonNull::new(seed.native);
    guarded.section =
        super::legacy_section::SectionSlot::legacy(seed.section, &legacy_book.section);
    guarded.toolbar = NonNull::new(seed.toolbar);
    guarded.overlay_popup = NonNull::new(seed.popup);
    guarded.bookmarks = NonNull::new(seed.bookmarks);
    assert!(seed.more_count <= 19);
    guarded.more_actions = seed.more_actions;
    guarded.more_count = seed.more_count;
    owner.menu_hooks = Some(seed.hooks);
    let context = seed.hooks.context;
    owner.projection_hooks = Some(ProjectionHooks {
        context,
        toolbar_menu: seed.toolbar_menu,
        base_input: unexpected_flag,
        acknowledge: unexpected,
        clear: unexpected,
        fallback: unexpected,
    });
    owner.overlay_hooks = Some(OverlayHooks {
        context,
        home_reset: unexpected,
        current_toc: seed.toc,
        build_more: None,
        paint: seed.render,
        push: seed.push_overlay,
        a4: unexpected_note,
        xteink: unexpected_note,
    });
    owner.lifetime_overrides = Some(LifetimeOverrides {
        context,
        settle_overlay: seed.settle,
        discard_overlay: seed.discard,
        save_links: unexpected,
        load_links: unexpected,
        load_bookmarks: seed.load_bookmarks,
        disable_fast: unexpected,
        destination,
        move_book,
    });
    owner.navigation_hooks = Some(NavigationHooks {
        context,
        finish: unexpected,
        push: seed.push_child,
        reopen: unexpected_text,
        update: seed.update,
    });
    {
        let access = owner.borrowed_resources();
        owner.publish_section(&access);
    }
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_menu_destroy(raw: *mut c_void) {
    let mut owner = Box::from_raw(raw.cast::<EpubSession>());
    if let Some(section) = owner.guarded.get_mut().section.take() {
        section.return_fixture();
    }
    drop(owner);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_menu_observe(raw: *mut c_void, out: *mut Observation) {
    let owner = &*raw.cast::<EpubSession>();
    let (has_toolbar, more_actions, more_count) = {
        let guarded = owner.guarded.fixture();
        (
            u8::from(guarded.toolbar.is_some()),
            guarded.more_actions,
            guarded.more_count,
        )
    };
    *out = owner.model(|state| Observation {
        cached_spine: state.cached.spine,
        cached_count: state.cached.pages,
        next_page: state.next_page,
        spine: state.current.spine,
        focus: state.focused_tool,
        bookmark_time: state.bookmark_notice.map_or(0, |notice| notice.since),
        manual_turn: state.manual_turn,
        automatic: state.auto_option,
        overlay: state.overlay as u8,
        stored: u8::from(state.page_stored),
        hold: u8::from(state.panel_hold_jumped),
        cursor: u8::from(state.panel_cursor),
        bookmarked: u8::from(state.bookmark_notice.is_some()),
        screenshot: u8::from(state.screenshot),
        has_section: u8::from(state.facts.section.is_some()),
        has_toolbar,
        more_actions,
        more_count,
    });
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_menu_anchor(
    raw: *mut c_void,
    sink: app_abi::TextSink,
    context: *mut c_void,
) {
    let owner = &*raw.cast::<EpubSession>();
    sink(context, owner.guarded.fixture().anchor.wire());
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_menu_open(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).open_reader_menu();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_menu_confirm(raw: *mut c_void, action: i32) {
    (&*raw.cast::<EpubSession>())
        .confirm_reader_menu(action, super::super::menu::MenuOrigin::ChildResult);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_menu_build_more(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).build_more_actions();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_menu_activate(raw: *mut c_void, row: i32) {
    (&*raw.cast::<EpubSession>()).activate_more_row(row);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_menu_result(
    raw: *mut c_void,
    result: *const app_abi::XpAppResultView,
) {
    (&*raw.cast::<EpubSession>()).child_result(result.as_ref());
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_menu_confirm_direct(raw: *mut c_void, action: i32) {
    (&*raw.cast::<EpubSession>())
        .confirm_reader_menu(action, super::super::menu::MenuOrigin::Direct);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_text_settings_open(raw: *mut c_void, from_overlay: u8) {
    (&*raw.cast::<EpubSession>()).open_text_settings(from_overlay != 0);
}
