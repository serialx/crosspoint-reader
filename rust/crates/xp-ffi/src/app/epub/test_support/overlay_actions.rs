extern crate alloc;

use super::{
    super::{
        abi, app_abi,
        state::{Overlay, SectionFacts},
        EpubSession,
    },
    LifetimeOverrides, NavigationHooks, OverlayHooks,
};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct Seed {
    native: *mut c_void,
    book: *mut c_void,
    section: *mut c_void,
    toolbar: *mut c_void,
    popup: *mut c_void,
    focus: i32,
    index: i32,
    counter: i32,
    overlay: u8,
    cursor: u8,
    hold: u8,
    stored: u8,
    stale: u8,
    failed: u8,
    context: *mut c_void,
    home_reset: unsafe extern "C" fn(*mut c_void),
    current_toc: unsafe extern "C" fn(*mut c_void) -> i32,
    build_more: unsafe extern "C" fn(*mut c_void),
    paint: unsafe extern "C" fn(*mut c_void),
    push: unsafe extern "C" fn(*mut c_void),
    a4: unsafe extern "C" fn(*mut c_void, u8),
    xteink: unsafe extern "C" fn(*mut c_void, u8),
    settle: unsafe extern "C" fn(*mut c_void),
    discard: unsafe extern "C" fn(*mut c_void),
    update: unsafe extern "C" fn(*mut c_void),
}
#[repr(C)]
pub struct Observation {
    focus: i32,
    index: i32,
    counter: i32,
    overlay: u8,
    cursor: u8,
    hold: u8,
    stored: u8,
    toolbar: u8,
    failed: u8,
}
fn mode(value: u8) -> Overlay {
    match value {
        0 => Overlay::Page,
        1 => Overlay::Toolbar,
        2 => Overlay::Contents,
        3 => Overlay::Text,
        4 => Overlay::More,
        _ => panic!("invalid fixture overlay"),
    }
}
unsafe extern "C" fn unexpected(_: *mut c_void) {
    panic!("unexpected overlay fixture method");
}
unsafe extern "C" fn destination(
    _: *mut c_void,
    _: app_abi::Text,
    _: app_abi::TextSink,
    _: *mut c_void,
) -> u8 {
    panic!("unexpected destination");
}
unsafe extern "C" fn move_book(
    _: *mut c_void,
    _: app_abi::Text,
    _: app_abi::Text,
    _: app_abi::Text,
) {
    panic!("unexpected move");
}
unsafe extern "C" fn push(_: *mut c_void, _: *mut c_void) {
    panic!("unexpected child");
}
unsafe extern "C" fn reopen(_: *mut c_void, _: app_abi::Text) {
    panic!("unexpected reopen");
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_create_legacy_book(
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
    state.overlay = mode(seed.overlay);
    state.focused_tool = seed.focus;
    state.panel_index = seed.index;
    state.base.refresh_counter = seed.counter;
    state.panel_cursor = seed.cursor != 0;
    state.panel_hold_jumped = seed.hold != 0;
    state.page_stored = seed.stored != 0;
    state.scene_gate.failed = seed.failed != 0;
    state.facts.section = (!seed.section.is_null()).then_some(SectionFacts::default());
    state.facts.frame_stale = seed.stale != 0;
    let guarded = owner.guarded.get_mut();
    guarded.native = NonNull::new(seed.native);
    guarded.section =
        super::legacy_section::SectionSlot::legacy(seed.section, &legacy_book.section);
    guarded.toolbar = NonNull::new(seed.toolbar);
    guarded.overlay_popup = NonNull::new(seed.popup);
    owner.overlay_hooks = Some(OverlayHooks {
        context: seed.context,
        home_reset: seed.home_reset,
        current_toc: seed.current_toc,
        build_more: Some(seed.build_more),
        paint: seed.paint,
        push: seed.push,
        a4: seed.a4,
        xteink: seed.xteink,
    });
    owner.lifetime_overrides = Some(LifetimeOverrides {
        context: seed.context,
        settle_overlay: seed.settle,
        discard_overlay: seed.discard,
        save_links: unexpected,
        load_links: unexpected,
        load_bookmarks: unexpected,
        disable_fast: unexpected,
        destination,
        move_book,
    });
    owner.navigation_hooks = Some(NavigationHooks {
        context: seed.context,
        finish: unexpected,
        push,
        reopen,
        update: seed.update,
    });
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_observe(raw: *mut c_void, out: *mut Observation) {
    let owner = &*raw.cast::<EpubSession>();
    let toolbar = owner.guarded.fixture().toolbar.is_some();
    *out = owner.model(|state| Observation {
        focus: state.focused_tool,
        index: state.panel_index,
        counter: state.base.refresh_counter,
        overlay: state.overlay as u8,
        cursor: u8::from(state.panel_cursor),
        hold: u8::from(state.panel_hold_jumped),
        stored: u8::from(state.page_stored),
        toolbar: u8::from(toolbar),
        failed: u8::from(state.scene_gate.failed),
    });
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_open(raw: *mut c_void, target: u8) {
    (&*raw.cast::<EpubSession>()).open_overlay(mode(target));
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_close(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).close_overlay_to_page();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_reject(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).reject_reader_scene();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_cancel_rejected(
    raw: *mut c_void,
    input: *const xp_ui::model::Input,
) {
    (&*raw.cast::<EpubSession>()).cancel_rejected_input(&*input);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_complete_render(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).complete_render();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_set_mode(raw: *mut c_void, overlay: u8) {
    (&*raw.cast::<EpubSession>()).model(|state| state.overlay = mode(overlay));
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_publish(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).publish_reader_scene();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_consume_rejected(raw: *mut c_void) -> u8 {
    u8::from((&*raw.cast::<EpubSession>()).consume_rejected_input())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_build_more(
    raw: *mut c_void,
    bookmarks: *mut c_void,
    actions: *mut u8,
    count: *mut u8,
) -> u8 {
    let owner = &mut *raw.cast::<EpubSession>();
    owner.overlay_hooks = None;
    owner.guarded.get_mut().bookmarks = NonNull::new(bookmarks);
    let built = owner.build_more_actions();
    let guarded = owner.guarded.get_mut();
    core::ptr::copy_nonoverlapping(
        guarded.more_actions.as_ptr(),
        actions,
        guarded.more_actions.len(),
    );
    *count = guarded.more_count;
    // The single-threaded fixture retains its fake bookmark resource.
    guarded.bookmarks = None;
    u8::from(built)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_popup_prepare(
    raw: *mut c_void,
    pending: u8,
) -> abi::XpEpubBuildPopup {
    (&*raw.cast::<EpubSession>()).model(|state| state.build_popup = pending != 0);
    super::super::book_render::build_popup
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_popup_pending(raw: *mut c_void) -> u8 {
    u8::from((&*raw.cast::<EpubSession>()).model(|state| state.build_popup))
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_destroy(raw: *mut c_void) {
    let mut owner = Box::from_raw(raw.cast::<EpubSession>());
    owner.discard_legacy_fixture();
    let guarded = owner.guarded.get_mut();
    // Historical fixtures keep ownership of all their fake native resources.
    guarded.native = None;
    if let Some(section) = guarded.section.take() {
        section.return_fixture();
    }
    guarded.toolbar = None;
    guarded.overlay_popup = None;
    drop(owner);
}
