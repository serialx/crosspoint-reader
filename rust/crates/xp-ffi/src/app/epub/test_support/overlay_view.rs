extern crate alloc;

use super::super::{
    abi, app_abi,
    state::{Overlay, Position},
    EpubSession,
};
use super::OverlayViewHooks;
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct OverlayViewSeed {
    pub native: *mut c_void,
    pub book: *mut c_void,
    pub section: *mut c_void,
    pub toolbar: *mut c_void,
    pub context: *mut c_void,
    pub spine: i32,
    pub page: i32,
    pub focus: i32,
    pub index: i32,
    pub overlay: u8,
    pub cursor: u8,
    pub more_count: u8,
    pub real_values: u8,
    pub more_actions: [u8; 19],
    pub chapter: unsafe extern "C" fn(*mut c_void, app_abi::TextSink, *mut c_void) -> u8,
    pub text_name: unsafe extern "C" fn(*mut c_void, i32, app_abi::TextSink, *mut c_void) -> u8,
    pub text_value: unsafe extern "C" fn(*mut c_void, i32, app_abi::TextSink, *mut c_void) -> u8,
    pub more_name: unsafe extern "C" fn(*mut c_void, i32, app_abi::TextSink, *mut c_void) -> u8,
    pub more_value: unsafe extern "C" fn(*mut c_void, i32, app_abi::TextSink, *mut c_void) -> u8,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_view_create(
    seed: *const OverlayViewSeed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
) -> *mut c_void {
    let seed = &*seed;
    assert!(seed.more_count <= 19);
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
    state.overlay = match seed.overlay {
        0 => Overlay::Page,
        1 => Overlay::Toolbar,
        2 => Overlay::Contents,
        3 => Overlay::Text,
        4 => Overlay::More,
        _ => panic!("fixture overlay"),
    };
    state.panel_cursor = seed.cursor != 0;
    state.focused_tool = seed.focus;
    state.panel_index = seed.index;
    state.facts.book = !seed.book.is_null();
    let guarded = owner.guarded.get_mut();
    guarded.native = NonNull::new(seed.native);
    guarded.book = NonNull::new(seed.book);
    guarded.section = NonNull::new(seed.section);
    guarded.toolbar = NonNull::new(seed.toolbar);
    guarded.more_actions = seed.more_actions;
    guarded.more_count = seed.more_count;
    if seed.real_values == 0 {
        owner.overlay_view_hooks = Some(OverlayViewHooks {
            context: seed.context,
            chapter: seed.chapter,
            text_name: seed.text_name,
            text_value: seed.text_value,
            more_name: seed.more_name,
            more_value: seed.more_value,
        });
    }
    let access = owner.borrowed_resources();
    owner.publish_section(&access);
    drop(access);
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_view_destroy(raw: *mut c_void) {
    let mut owner = Box::from_raw(raw.cast::<EpubSession>());
    let access = owner.borrowed_resources();
    owner.drop_section(&access);
    drop(access);
    // The enclosing physical fixture owns the book, toolbar and native root.
    let guarded = owner.guarded.get_mut();
    guarded.book = None;
    guarded.toolbar = None;
    guarded.native = None;
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_view_render(raw: *mut c_void) {
    let owner = &*raw.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.render_overlay(&access);
}
