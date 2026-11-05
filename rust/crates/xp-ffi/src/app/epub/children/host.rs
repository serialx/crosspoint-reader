//! Host-only composition of real session/book/child owners and physical leaves.
extern crate alloc;
use super::{
    chapter_book::GuardedBook,
    owned::{Chapters, Percent, Toolbar},
    physical::Physical,
    toolbar_view::Content,
};
use crate::app::{
    abi,
    epub::{self, session_book, EpubSession},
    resources::Text,
};
use alloc::boxed::Box;
use core::ffi::c_void;
use xp_ui::{reader_chrome::list, scene::Rect};

#[repr(C)]
pub struct Drawing {
    theme: unsafe extern "C" fn(*mut list::Theme, u8),
    header: unsafe extern "C" fn(Rect, abi::Text, u8),
    clear: unsafe extern "C" fn(),
    icon: unsafe extern "C" fn(u32, Rect, Rect),
    prewarm: unsafe extern "C" fn(
        i32,
        *const c_void,
        unsafe extern "C" fn(*const c_void, u32) -> abi::Text,
        u32,
    ),
}
struct Visit {
    parent: Box<EpubSession>,
    child: Option<Box<Chapters>>,
    toolbar: Option<Box<Toolbar>>,
    physical: Physical,
}
unsafe extern "C" fn unused_layout(_: *mut xp_ui::utility::Layout) -> u8 {
    panic!("unexpected utility layout")
}
unsafe extern "C" fn unused_footer(_: Rect, _: *const abi::Text) {
    panic!("unexpected utility footer")
}
unsafe extern "C" fn unused_qr(_: Rect, _: abi::Text) {
    panic!("unexpected QR")
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_book_open(
    runtime: &'static abi::Runtime,
    resources: &'static abi::Resources,
    leaves: &'static epub::abi::XpEpubLeaves,
    path: abi::Text,
    cache: abi::Text,
    drawing: &Drawing,
) -> *mut c_void {
    let path = unsafe { path.bytes() }.unwrap();
    let parent = EpubSession::try_new(path, false, 0, leaves, runtime, resources).unwrap();
    let access = parent.lock_resources();
    let mut pending =
        session_book::Pending::Reader(Text::copy(unsafe { cache.bytes() }.unwrap()).unwrap());
    let book = pending
        .load(
            unsafe { &mut *parent.path.get() },
            resources,
            leaves,
            runtime.now_ms,
            true,
        )
        .ok()
        .unwrap();
    unsafe { session_book::assert_real(book) };
    parent.set_book(&access, Some(book));
    let child = Chapters::try_new(runtime, 0).unwrap();
    let physical = Physical {
        list_theme: drawing.theme,
        header: drawing.header,
        clear_fonts: drawing.clear,
        prewarm_rows: drawing.prewarm,
        utility_layout: unused_layout,
        utility_footer: unused_footer,
        qr: unused_qr,
        toolbar_icon: drawing.icon,
    };
    assert!(unsafe { child.enter(resources, &physical) });
    drop(access);
    Box::into_raw(Box::new(Visit {
        parent,
        child: Some(child),
        toolbar: Some(Toolbar::try_new(runtime).unwrap()),
        physical,
    }))
    .cast()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_book_render(raw: *mut c_void, selected: i32) -> u16 {
    let visit = unsafe { &*raw.cast::<Visit>() };
    let child = visit.child.as_ref().unwrap();
    child.model.with(|model| {
        model.state.list.selected = selected;
        model.state.list.follow = 1;
    });
    let access = visit.parent.lock_resources();
    let mut book = GuardedBook(&access);
    unsafe { child.render(visit.parent.resources, &visit.physical, &mut book) }.unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_book_close(raw: *mut c_void) {
    let mut visit = unsafe { Box::from_raw(raw.cast::<Visit>()) };
    let access = visit.parent.lock_resources();
    drop(visit.child.take());
    drop(visit.toolbar.take());
    let book = visit.parent.take_book(&access).unwrap();
    unsafe { session_book::close(book) };
    drop(access);
    drop(visit);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_toolbar_render(raw: *mut c_void, selected: i32) -> u16 {
    let visit = unsafe { &*raw.cast::<Visit>() };
    let access = visit.parent.lock_resources();
    let book = visit.parent.book(&access).unwrap();
    let content = Content {
        panel: true,
        dense: false,
        active_tool: 0,
        progress_permille: 0,
        chapter: None,
        page_info: None,
        panel_title: Some(c"Contents"),
        count: unsafe { session_book::toc_count(book.as_ptr()) },
        selected,
    };
    unsafe {
        visit.parent.paint_owned_toolbar(
            &access,
            visit.toolbar.as_ref().unwrap(),
            &visit.physical,
            epub::state::Overlay::Contents,
            &content,
        )
    }
    .unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_percent_open(
    runtime: &'static abi::Runtime,
    value: i32,
) -> *mut c_void {
    Box::into_raw(Percent::try_new(runtime, value).unwrap()).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_percent_input(raw: *mut c_void, input: &xp_ui::model::Input) {
    unsafe { &*raw.cast::<Percent>() }.input(*input, true);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_percent_render(
    raw: *mut c_void,
    services: &abi::Resources,
    layout: &xp_ui::utility::Layout,
) -> u16 {
    unsafe extern "C" fn overflow(_: u8, _: abi::Text, _: abi::Text) {
        panic!("Percent command bound exceeded")
    }
    unsafe { (&*raw.cast::<Percent>()).render(services, overflow, *layout) }.unwrap()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_percent_close(raw: *mut c_void) {
    drop(unsafe { Box::from_raw(raw.cast::<Percent>()) });
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_move_result(raw: *mut c_void) -> u8 {
    let visit = unsafe { &*raw.cast::<Visit>() };
    let anchor = Text::copy(b"owned-anchor").unwrap();
    let pointer = anchor.bytes().as_ptr();
    visit
        .parent
        .model(|state| state.child = epub::state::ChildIntent::Chapter);
    visit
        .parent
        .deliver_reader_child(super::child_result::ReaderChildResult::Chapter(Some(
            super::child_result::ChapterSelection { spine: 7, anchor },
        )));
    let access = visit.parent.lock_resources();
    let moved = visit.parent.anchor(&access);
    assert_eq!(moved.data, pointer);
    assert_eq!(unsafe { moved.bytes() }.unwrap(), b"owned-anchor");
    assert!(visit.parent.model(|state| state.current.spine == 7
        && state.next_page == 0
        && matches!(state.child, epub::state::ChildIntent::None)));
    1
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_child_result_anchor(raw: *const c_void) -> *const u8 {
    let owner = unsafe { &*raw.cast::<super::owned::Chapters>() };
    match unsafe { &*owner.result.get() } {
        Some(super::child_result::ReaderChildResult::Chapter(Some(result))) => {
            result.anchor.bytes().as_ptr()
        }
        _ => core::ptr::null(),
    }
}

// Physical fixture rows exercise the real owner; parent row providers are covered
// separately by the guarded book and App compositions.
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_toolbar_open(runtime: &'static abi::Runtime) -> *mut c_void {
    Box::into_raw(Toolbar::try_new(runtime).unwrap()).cast()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_toolbar_close(raw: *mut c_void) {
    drop(unsafe { Box::from_raw(raw.cast::<Toolbar>()) });
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_toolbar_render(
    raw: *mut c_void,
    services: &'static abi::Resources,
    physical: &Physical,
    model: &epub::abi::XpEpubToolbarModel,
    fail_row: i32,
) -> u8 {
    let text = |wire: abi::Text| {
        (!wire.data.is_null()).then(|| unsafe { core::ffi::CStr::from_ptr(wire.data.cast()) })
    };
    let content = Content {
        panel: model.panel != 0,
        dense: model.dense != 0,
        active_tool: model.active_tool,
        progress_permille: model.progress_permille,
        chapter: text(model.chapter),
        page_info: text(model.page_info),
        panel_title: text(model.panel_title),
        count: model.item_count,
        selected: model.selected,
    };
    let owner = unsafe { &*raw.cast::<Toolbar>() };
    u8::from(
        unsafe {
            owner.render(services, physical, &content, &mut |index, row| {
                if index == fail_row {
                    return false;
                }
                row.toggle = index % 2 != 0;
                row.checked = index % 3 == 0;
                let label = [b'A' + (index % 26) as u8; 64];
                let value = [b'a' + (index % 26) as u8; 32];
                row.label
                    .assign_parts(&[&b"    "[..(index % 5) as usize], &label])
                    .is_ok()
                    && row.value.assign(&value).is_ok()
            })
        }
        .is_ok(),
    )
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_toolbar_observe(
    raw: *mut c_void,
    nav: *mut xp_ui::model::ListState,
    hits: *mut xp_ui::reader_chrome::hits::Layout,
) -> u8 {
    unsafe { &*raw.cast::<Toolbar>() }.model.with(|model| {
        unsafe {
            nav.write(model.navigation);
            hits.write(model.hits);
        }
        u8::from(model.ready)
    })
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_toolbar_reset(raw: *mut c_void, selected: i32, top: i32) {
    unsafe { &*raw.cast::<Toolbar>() }.reset_navigation(selected, top);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_toolbar_scroll(
    raw: *mut c_void,
    delta: i32,
    count: i32,
) -> u8 {
    u8::from(unsafe { &*raw.cast::<Toolbar>() }.scroll(delta, count))
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_toolbar_route(
    raw: *mut c_void,
    runtime: &abi::Runtime,
    input: &xp_ui::model::Input,
) -> epub::abi::XpEpubRouted {
    unsafe { &*raw.cast::<Toolbar>() }.route(runtime, || *input)
}
