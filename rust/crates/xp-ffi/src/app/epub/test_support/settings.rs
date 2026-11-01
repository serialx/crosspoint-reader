extern crate alloc;

use super::super::{abi, app_abi, state::Position, EpubSession};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Overrides {
    pub context: *mut c_void,
    pub initial: unsafe extern "C" fn(*mut c_void),
    pub remember: unsafe extern "C" fn(*mut c_void),
    pub discard: unsafe extern "C" fn(*mut c_void),
    pub paint: unsafe extern "C" fn(*mut c_void),
    pub status_height: unsafe extern "C" fn(*mut c_void, i32),
    pub progress_height: unsafe extern "C" fn(*mut c_void, i32),
    pub snap: unsafe extern "C" fn(*mut c_void, u8),
}

#[repr(C)]
pub struct SettingsSeed {
    pub native: *mut c_void,
    pub section: *mut c_void,
    pub popup: *mut c_void,
    pub spine: i32,
    pub page: i32,
    pub cached_spine: i32,
    pub cached_count: i32,
    pub next_page: i32,
    pub last_turn: u32,
    pub duration: u32,
    pub orientation: u8,
    pub automatic: u8,
    pub hooks: Overrides,
}

#[repr(C)]
pub struct SettingsObservation {
    pub cached_spine: i32,
    pub cached_count: i32,
    pub next_page: i32,
    pub last_turn: u32,
    pub duration: u32,
    pub orientation: u8,
    pub automatic: u8,
    pub has_section: u8,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_settings_create(
    seed: *const SettingsSeed,
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
    state.cached.spine = seed.cached_spine;
    state.cached.pages = seed.cached_count;
    state.next_page = seed.next_page;
    state.last_turn_ms = seed.last_turn;
    state.turn_duration_ms = seed.duration;
    state.orientation = seed.orientation;
    state.auto_turn = seed.automatic != 0;
    let guarded = owner.guarded.get_mut();
    guarded.native = Some(NonNull::new(seed.native).expect("fixture root"));
    guarded.section = NonNull::new(seed.section);
    guarded.overlay_popup = Some(NonNull::new(seed.popup).expect("fixture popup"));
    owner.settings_overrides = Some(seed.hooks);
    {
        let access = owner.borrowed_resources();
        owner.publish_section(&access);
    }
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_settings_destroy(owner: *mut c_void) {
    let mut owner = Box::from_raw(owner.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        owner.drop_section(&access);
    }
    // Native root and popup remain fixture-owned after all retained callbacks finish.
    owner.guarded.get_mut().native = None;
    owner.guarded.get_mut().overlay_popup = None;
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_settings_observe(
    owner: *mut c_void,
    out: *mut SettingsObservation,
) {
    let owner = &*owner.cast::<EpubSession>();
    *out = owner.model(|state| SettingsObservation {
        cached_spine: state.cached.spine,
        cached_count: state.cached.pages,
        next_page: state.next_page,
        last_turn: state.last_turn_ms,
        duration: state.turn_duration_ms,
        orientation: state.orientation,
        automatic: u8::from(state.auto_turn),
        has_section: u8::from(state.facts.section.is_some()),
    });
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_settings_initial(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).apply_reader_initial_orientation();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_settings_orientation(owner: *mut c_void, value: u8) {
    (&*owner.cast::<EpubSession>()).apply_orientation(value);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_settings_auto(owner: *mut c_void, value: u8) {
    (&*owner.cast::<EpubSession>()).toggle_auto_page_turn(value);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_settings_text(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).apply_text_settings();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_settings_live(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).apply_text_setting_live();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_settings_popup(owner: *mut c_void, row: i32) {
    (&*owner.cast::<EpubSession>()).show_text_row_popup(row);
}
