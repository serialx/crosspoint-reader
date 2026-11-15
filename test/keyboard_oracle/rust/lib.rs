pub use xp_ui::{ffi, model, scene};
pub use xp_ui::keyboard;
pub use xp_ui::keyboard_view;
use keyboard::*;
/// # Safety
/// Test-only caller supplies valid, disjoint buffers and descriptors.
#[no_mangle]
pub unsafe extern "C" fn oracle_activate(
    s: &mut State,
    text: *mut u8,
    capacity: u32,
    l: &Layouts,
    value: i16,
    long: bool,
    now: u32,
) -> keyboard::Effect {
    s.activate(
        std::slice::from_raw_parts_mut(text, capacity as usize),
        l,
        value,
        long,
        now,
    )
}
/// # Safety
/// Test-only caller supplies valid, disjoint buffers and descriptors.
#[no_mangle]
pub unsafe extern "C" fn oracle_mask(s: &State, text: *const u8, out: *mut u8, capacity: u32) {
    s.mask(
        std::slice::from_raw_parts(text, s.length as usize),
        std::slice::from_raw_parts_mut(out, capacity as usize),
    );
}
/// # Safety
/// Test-only caller supplies valid, disjoint buffers and descriptors.
#[no_mangle]
pub unsafe extern "C" fn oracle_insert(
    s: &mut State,
    text: *mut u8,
    capacity: u32,
    out: *const std::ffi::c_char,
) {
    s.insert(
        std::slice::from_raw_parts_mut(text, capacity as usize),
        std::ffi::CStr::from_ptr(out).to_bytes(),
    );
}
/// # Safety
/// Test-only caller supplies valid, disjoint buffers and descriptors.
#[no_mangle]
pub unsafe extern "C" fn oracle_backspace(s: &mut State, text: *mut u8, capacity: u32) -> bool {
    s.backspace(std::slice::from_raw_parts_mut(text, capacity as usize))
}
/// # Safety
/// Test-only caller supplies valid, disjoint buffers and descriptors.
#[no_mangle]
pub extern "C" fn oracle_sync(s: &mut State, l: &Layouts, value: i16) -> bool {
    s.sync(l, value)
}
/// # Safety
/// Test-only caller supplies valid, disjoint buffers and descriptors.
#[no_mangle]
pub unsafe extern "C" fn oracle_widget(
    s: &State,
    l: &Layouts,
    layout: &keyboard_view::Layout,
    area: scene::Rect,
    bottom: i16,
    commands: *mut scene::Command,
    hits: *mut scene::Interaction,
    out: &mut ffi::SceneView,
) -> u8 {
    let mut text = [];
    let mut a = scene::Arena::new(
        std::slice::from_raw_parts_mut(commands, 80),
        &mut text,
        std::slice::from_raw_parts_mut(hits, 56),
    )
    .unwrap();
    keyboard_view::key_commands(&mut a, s, layout, l, area, bottom).unwrap();
    let scene = a.finish(scene::Refresh::Fast, 1).unwrap();
    *out = ffi::SceneView {
        commands: scene.commands.as_ptr(),
        text: std::ptr::null(),
        interactions: scene.interactions.as_ptr(),
        generation: 1,
        command_count: scene.commands.len() as u16,
        text_len: 0,
        interaction_count: scene.interactions.len() as u16,
        refresh: 1,
        reserved: 0,
    };
    1
}

// The debug staticlib retains asset code; keyboard oracles must never call it.
#[no_mangle]
pub extern "C" fn xp_assets_platform_alloc(_bytes: usize, _caps: u32) -> *mut u8 {
    std::process::abort()
}

#[no_mangle]
pub extern "C" fn xp_assets_platform_realloc(_data: *mut u8, _bytes: usize, _caps: u32) -> *mut u8 {
    std::process::abort()
}

#[no_mangle]
pub extern "C" fn xp_assets_platform_free(_data: *mut u8) {
    std::process::abort()
}
