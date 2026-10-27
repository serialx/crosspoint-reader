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

use xp_core::sdfont::pool::{Cache, Resident, Services, TextGetter, Texts};
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_prewarm(
    pool: *mut Cache,
    getter: Option<TextGetter>,
    context: *const core::ffi::c_void,
    count: u32,
    mask: u8,
    metadata: bool,
    kern: bool,
    accumulate: bool,
) -> i32 {
    let (Some(pool), Some(getter)) = (pool.as_mut(), getter) else {
        return -1;
    };
    pool.prewarm(getter, context, count, mask, metadata, kern, accumulate)
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_advances(
    pool: *mut Cache,
    texts: *const Texts,
    mask: u8,
) -> i32 {
    let (Some(pool), Some(texts)) = (pool.as_mut(), texts.as_ref()) else {
        return -1;
    };
    pool.build_advances(texts, mask)
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_advance(pool: *const Cache, cp: u32, style: u8) -> u16 {
    pool.as_ref()
        .and_then(|p| p.advance(usize::from(style & 3), cp))
        .unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_resolve(pool: *const Cache, style: u8) -> u8 {
    pool.as_ref()
        .map_or(0, |p| p.resolve(usize::from(style)) as u8)
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_mask(pool: *const Cache, mask: u8) -> u8 {
    pool.as_ref().map_or(0, |p| p.mask(mask))
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_coverage(pool: *const Cache, cp: u32, style: u8) -> bool {
    pool.as_ref()
        .is_some_and(|p| p.coverage(usize::from(style & 3), cp))
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_miss(
    pool: *mut Cache,
    cp: u32,
    style: u8,
) -> *const xp_core::font::raw::Glyph {
    pool.as_mut()
        .map_or(core::ptr::null(), |p| p.miss(usize::from(style), cp))
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_clear_advances(pool: *mut Cache) {
    if let Some(p) = pool.as_mut() {
        p.clear_advances();
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_clear_overflow(pool: *mut Cache) {
    if let Some(p) = pool.as_mut() {
        p.clear_overflow();
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_free_mini(style: *mut Resident, io: *const Services) {
    if let (Some(s), Some(io)) = (style.as_mut(), io.as_ref()) {
        s.free_mini(io);
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_free_ligatures(style: *mut Resident, io: *const Services) {
    if let (Some(s), Some(io)) = (style.as_mut(), io.as_ref()) {
        s.free_ligatures(io);
    }
}
#[no_mangle]
pub extern "C" fn xp_sdfont_pool_layout(field: u32) -> usize {
    xp_core::sdfont::pool::LAYOUT
        .get(field as usize)
        .copied()
        .unwrap_or(usize::MAX)
}

#[no_mangle]
pub unsafe extern "C" fn xp_sdfont_pool_lookup(
    pool: *const Cache,
    cp: u32,
    style: u8,
    out: *mut u16,
) -> bool {
    let Some(value) = pool
        .as_ref()
        .and_then(|p| p.advance(usize::from(style & 3), cp))
    else {
        return false;
    };
    if !out.is_null() {
        out.write(value);
    }
    true
}
