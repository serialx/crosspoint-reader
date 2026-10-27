//! Synchronous byte loans; outputs and parser state are caller-owned (xp_ffi.h).
use xp_core::font::raw::Interval;
use xp_core::sdfont::{self, Directory, Intervals, Style};

unsafe fn bytes<'a>(ptr: *const u8, len: usize) -> Option<&'a [u8]> {
    if len > isize::MAX as usize || (ptr.is_null() && len != 0) {
        return None;
    }
    Some(if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(ptr, len)
    })
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_header(
    data: *const u8,
    len: usize,
    out: *mut Directory,
) -> bool {
    if out.is_null() {
        return false;
    }
    let Some(value) = bytes(data, len).and_then(sdfont::header) else {
        return false;
    };
    out.write(value);
    true
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_style(
    data: *const u8,
    len: usize,
    state: *mut Directory,
    out: *mut Style,
) -> bool {
    if state.is_null() || out.is_null() {
        return false;
    }
    let Some(value) = bytes(data, len).and_then(|b| sdfont::style(b, &mut *state)) else {
        return false;
    };
    out.write(value);
    true
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_interval(
    data: *const u8,
    len: usize,
    glyphs: u32,
    index: u32,
    state: *mut Intervals,
    out: *mut Interval,
) -> bool {
    if state.is_null() || out.is_null() {
        return false;
    }
    let Some(value) =
        bytes(data, len).and_then(|b| sdfont::interval(b, glyphs, index, &mut *state))
    else {
        return false;
    };
    out.write(value);
    true
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_glyph_valid(
    data: *const u8,
    len: usize,
    two_bit: bool,
    bitmap: u32,
    file_size: u32,
) -> bool {
    bytes(data, len).is_some_and(|b| sdfont::glyph_valid(b, two_bit, bitmap, file_size))
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_kern_class(
    data: *const u8,
    len: usize,
    count: u8,
    cp: *mut u32,
) -> u8 {
    if cp.is_null() {
        return 0;
    }
    let Some((codepoint, id)) = bytes(data, len).and_then(|b| sdfont::kern_class(b, count)) else {
        return 0;
    };
    cp.write(codepoint);
    id
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_ligature(
    data: *const u8,
    len: usize,
    left: *mut u32,
    right: *mut u32,
    out: *mut u32,
) -> bool {
    if left.is_null() || right.is_null() || out.is_null() {
        return false;
    }
    let Some((l, r, cp)) = bytes(data, len).and_then(sdfont::ligature) else {
        return false;
    };
    left.write(l);
    right.write(r);
    out.write(cp);
    true
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_filename(data: *const u8, len: usize, size: *mut u8) -> bool {
    if size.is_null() {
        return false;
    }
    let Some(value) = bytes(data, len).and_then(sdfont::filename) else {
        return false;
    };
    size.write(value);
    true
}
