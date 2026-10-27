//! Synchronous font-table queries; C++ owns bitmap storage and dynamic callbacks.

use core::ffi::c_void;
use xp_core::font::{
    raw::{FontData, Glyph},
    Font, GlyphResult, SdView,
};

unsafe fn font<'a>(data: *const c_void) -> Font<'a> {
    unsafe { Font::new(&*data.cast::<FontData>()) }
}

unsafe fn result(value: GlyphResult, out: *mut Glyph) -> u32 {
    match value {
        GlyphResult::Missing => 0,
        GlyphResult::Resident(glyph) => {
            unsafe { out.write(glyph) };
            1
        }
        GlyphResult::Synthetic(glyph) => {
            unsafe { out.write(glyph) };
            2
        }
        GlyphResult::NotResident => 3,
    }
}

// All entry points borrow immutable C++ tables for one call; see xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_font_glyph(
    data: *const c_void,
    cp: u32,
    metrics: bool,
    out: *mut Glyph,
) -> u32 {
    let f = unsafe { font(data) };
    unsafe { result(if metrics { f.metrics(cp) } else { f.glyph(cp) }, out) }
}

#[no_mangle]
pub unsafe extern "C" fn xp_font_sd_glyph(
    view: *const SdView,
    cp: u32,
    metrics: bool,
    out: *mut Glyph,
) -> u32 {
    let f = unsafe { Font::from_sd(&*view) };
    unsafe { result(if metrics { f.metrics(cp) } else { f.glyph(cp) }, out) }
}

#[no_mangle]
pub unsafe extern "C" fn xp_font_has_codepoint(data: *const c_void, cp: u32) -> i32 {
    unsafe { font(data) }
        .has_codepoint(cp)
        .map_or(-1, i32::from)
}

#[no_mangle]
pub unsafe extern "C" fn xp_font_sd_has_codepoint(view: *const SdView, cp: u32) -> bool {
    unsafe { Font::from_sd(&*view) }
        .has_codepoint(cp)
        .unwrap_or(false)
}

#[no_mangle]
pub unsafe extern "C" fn xp_font_sd_advance(view: *const SdView, cp: u32, out: *mut u16) -> bool {
    if let Some(value) = unsafe { Font::from_sd(&*view) }.advance(cp) {
        unsafe { out.write(value) };
        true
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_font_kerning(
    data: *const c_void,
    left: u32,
    right: u32,
    out: *mut i8,
) -> bool {
    if let Some(value) = unsafe { font(data) }.kerning(left, right) {
        unsafe { out.write(value) };
        true
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_font_ligature(data: *const c_void, left: u32, right: u32) -> u32 {
    unsafe { font(data) }.ligature(left, right)
}

#[no_mangle]
pub unsafe extern "C" fn xp_font_apply_ligatures(
    data: *const c_void,
    cp: u32,
    text: *const u8,
    len: usize,
    consumed: *mut usize,
) -> u32 {
    let bytes = if len == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(text, len) }
    };
    let mut pos = 0;
    let result = unsafe { font(data) }.apply_ligatures(cp, bytes, &mut pos);
    unsafe { consumed.write(pos) };
    result
}

/// # Safety
/// Same table loan contract as xp_font_glyph. The result expires at the next owner mutation.
#[no_mangle]
pub unsafe extern "C" fn xp_font_resident_glyph(data: *const FontData, cp: u32) -> *const Glyph {
    Font::new(&*data)
        .glyph_index(cp)
        .map_or(core::ptr::null(), |i| (*data).glyph.add(i))
}
/// # Safety
/// out is a live, aligned writable glyph; no bitmap storage is allocated.
#[no_mangle]
pub unsafe extern "C" fn xp_font_solid_metrics(
    cp: u32,
    ascender: i32,
    em_advance: u16,
    out: *mut Glyph,
) {
    out.write(xp_core::font::solid_metrics(cp, ascender, Some(em_advance)));
}
