use xp_ui::{
    boot::{self, ffi::Metrics, view::Layout, ImageState, Placement, SleepPlan, XtcState},
    ffi::{Buffer, SceneView},
    model::{Effect, Input, Interest},
};
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_sleep_plan(
    mode: u8,
    timeout: u8,
    quick: u8,
    reader: u8,
    inverted: u8,
) -> SleepPlan {
    boot::sleep_plan(mode, timeout != 0, quick != 0, reader != 0, inverted != 0)
}
/// # Safety
/// Descriptors follow xp_ui_boot.h borrowing and aliasing rules.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_boot_view(
    l: *const Layout,
    m: *const Metrics,
    b: *const Buffer,
    o: *mut SceneView,
) -> u8 {
    unsafe { boot::ffi::view(l, m, b, o) }
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_boot_interest(crash: u8) -> Interest {
    boot::interest(crash != 0)
}
/// # Safety
/// Input is live and aligned for this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_crash_update(i: *const Input) -> Effect {
    unsafe { i.as_ref() }.map_or_else(Effect::default, boot::crash_update)
}
/// # Safety
/// State is exclusively borrowed and disjoint from input.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_image_update(s: *mut ImageState, i: *const Input) -> Effect {
    match (unsafe { s.as_mut() }, unsafe { i.as_ref() }) {
        (Some(s), Some(i)) => boot::image_update(s, i),
        _ => Effect::default(),
    }
}
/// # Safety
/// State is live, aligned and exclusively borrowed.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_xtc_turn(s: *mut XtcState, n: u32, a: i32, skip: u8) -> u8 {
    unsafe { s.as_mut() }.is_some_and(|s| s.turn(n, a, skip != 0)) as u8
}
/// # Safety
/// State is live, aligned and exclusively borrowed.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_xtc_restore(s: *mut XtcState, n: u32, end: u8) {
    if let Some(s) = unsafe { s.as_mut() } {
        s.restore(n, end != 0)
    }
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_image_place(w: i32, h: i32, sw: i32, sh: i32, crop: u8) -> Placement {
    boot::placement(w, h, sw, sh, crop != 0)
}
/// # Safety
/// Target's writable buffer and input bytes are live, aligned, and disjoint.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_xtc_pixels(
    t: *const xp_core::glyph::GlyphTarget,
    p: *const u8,
    n: u32,
    w: u16,
    h: u16,
    d: u8,
    pass: u8,
    diagnostics: *const boot::ffi::XtcDiagnostics,
) -> u8 {
    let Some(t) = (unsafe { t.as_ref() }) else {
        return 0;
    };
    if !t.valid() || t.buffer.is_null() || p.is_null() {
        return 0;
    }
    let bytes = unsafe { core::slice::from_raw_parts(p, n as usize) };
    let frame = unsafe { core::slice::from_raw_parts_mut(t.buffer, t.buffer_len as usize) };
    let diagnostics = unsafe { diagnostics.as_ref() };
    boot::xtc_pixels(t, frame, bytes, [w, h], d, pass, &mut |x, y, black| {
        if let Some(diag) = diagnostics {
            if let Some(outside) = diag.outside {
                unsafe { outside(diag.context, x, y, black as u8) }
            }
        }
    }) as u8
}

/// # Safety
/// State is live and aligned for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_image_hints(s: *const ImageState) -> u8 {
    unsafe { s.as_ref() }.map_or(0, boot::image_hints)
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_xtc_overlay(
    sw: i16,
    sh: i16,
    top: i16,
    bottom: i16,
    height: i16,
    mode: u8,
    position: u8,
) -> boot::XtcOverlay {
    boot::xtc_overlay(sw, sh, top, bottom, height, mode, position)
}
