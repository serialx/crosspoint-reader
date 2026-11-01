extern crate alloc;

use super::super::{abi, app_abi, state::Position, view_values, EpubSession};
use super::ViewValueHooks;
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct ViewSeed {
    pub native: *mut c_void,
    pub book: *mut c_void,
    pub section: *mut c_void,
    pub spine: i32,
    pub page: i32,
    pub duration: u32,
    pub automatic: u8,
    pub bookmarked: u8,
    pub context: *mut c_void,
    pub more_label: unsafe extern "C" fn(*mut c_void, i32) -> u16,
    pub link_note_drawn: unsafe extern "C" fn(*mut c_void, u8, u8),
}
#[repr(C)]
pub struct ViewObservation {
    pub note_shown: u8,
    pub note: u8,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_view_create(
    seed: *const ViewSeed,
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
    state.turn_duration_ms = seed.duration;
    state.auto_turn = seed.automatic != 0;
    state.bookmarked = seed.bookmarked != 0;
    let guarded = owner.guarded.get_mut();
    guarded.native = NonNull::new(seed.native);
    guarded.book = NonNull::new(seed.book);
    guarded.section = NonNull::new(seed.section);
    owner.view_value_hooks = Some(ViewValueHooks {
        context: seed.context,
        more_label: seed.more_label,
        link_note_drawn: seed.link_note_drawn,
    });
    let access = owner.borrowed_resources();
    owner.publish_section(&access);
    drop(access);
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_view_destroy(raw: *mut c_void) {
    let mut owner = Box::from_raw(raw.cast::<EpubSession>());
    let access = owner.borrowed_resources();
    owner.drop_section(&access);
    drop(access);
    // Book and native root remain borrowed from the enclosing physical fixture.
    owner.guarded.get_mut().book = None;
    owner.guarded.get_mut().native = None;
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_view_seed_rows(
    raw: *mut c_void,
    actions: *const u8,
    count: u8,
    rate: u8,
) {
    let owner = &*raw.cast::<EpubSession>();
    assert!(count <= 19);
    let access = owner.borrowed_resources();
    let guarded = &mut *owner.guarded(&access);
    guarded.more_actions.fill(0);
    guarded.more_actions[..usize::from(count)]
        .copy_from_slice(core::slice::from_raw_parts(actions, usize::from(count)));
    guarded.more_count = count;
    owner.model(|state| state.auto_option = rate);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_view_real_labels(raw: *mut c_void) {
    // Explicit host-only fixture configuration, between operations.
    (&mut *raw.cast::<EpubSession>()).view_value_hooks = None;
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_view_observe(raw: *mut c_void, out: *mut ViewObservation) {
    let owner = &*raw.cast::<EpubSession>();
    *out = owner.model(|state| ViewObservation {
        note_shown: u8::from(state.link_note_drawn.is_some()),
        note: state.link_note_drawn.unwrap_or(0),
    });
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_view_status(raw: *mut c_void) {
    let owner = &*raw.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.render_status_bar(&access);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_view_toolbar(raw: *mut c_void) -> u8 {
    u8::from((&*raw.cast::<EpubSession>()).uses_toolbar_menu())
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_view_text(
    raw: *mut c_void,
    operation: u8,
    row: i32,
    sink: app_abi::TextSink,
    context: *mut c_void,
) -> u8 {
    let owner = &*raw.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    match operation {
        0 => owner
            .current_chapter_title(&access, |text| sink(context, text))
            .unwrap_or(0),
        1 => sink(context, owner.text_row_name(&access, row).wire()),
        2 => sink(context, owner.text_row_value(&access, row).wire()),
        3 => sink(context, owner.more_row_name(&access, row).wire()),
        4 => sink(context, owner.more_row_value(&access, row).wire()),
        _ => panic!("fixture operation"),
    }
}
#[no_mangle]
pub extern "C" fn xp_epub_test_view_action_label(action: u8) -> u16 {
    view_values::more_action_label(action)
}
