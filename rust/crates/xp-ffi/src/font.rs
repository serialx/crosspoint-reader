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
use xp_core::font::cache::{Cache, Services};

#[no_mangle]
pub unsafe extern "C" fn xp_font_cache_clear(cache: *mut Cache, io: *const Services) {
    (*cache).clear(&*io);
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_cache_bitmap(
    cache: *mut Cache,
    data: *const FontData,
    glyph: *const Glyph,
    index: u32,
    io: *const Services,
) -> *const u8 {
    (*cache).bitmap(&*data, &*glyph, index, &*io)
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_cache_prewarm(
    cache: *mut Cache,
    data: *const FontData,
    text: *const u8,
    length: usize,
    io: *const Services,
) -> i32 {
    if data.is_null() || text.is_null() {
        return 0;
    }
    (*cache).prewarm(&*data, core::slice::from_raw_parts(text, length), &*io)
}
/// Dynamic lookup may mutate the resident tables: each immutable loan ends
/// before invoking an owner callback.
unsafe fn lookup(data: *const FontData, cp: u32) -> *const Glyph {
    if (*data).interval_count == 0 && (*data).glyph_miss_handler.is_none() {
        return core::ptr::null();
    }
    let resident = xp_font_resident_glyph(data, cp);
    if !resident.is_null() {
        return resident;
    }
    if let Some(miss) = (*data).glyph_miss_handler {
        let loaded = miss((*data).glyph_miss_ctx, cp);
        if !loaded.is_null() {
            return loaded;
        }
    }
    if cp == 0x02bb && xp_font_coverage(data, 0x2018) {
        return lookup(data, 0x2018);
    }
    if cp != 0xfffd {
        return lookup(data, 0xfffd);
    }
    core::ptr::null()
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_lookup(data: *const FontData, cp: u32) -> *const Glyph {
    lookup(data, cp)
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_coverage(data: *const FontData, cp: u32) -> bool {
    if let Some(covered) = Font::new(&*data).has_codepoint(cp) {
        return covered;
    }
    (*data)
        .coverage_handler
        .is_some_and(|coverage| coverage((*data).glyph_miss_ctx, cp))
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_dynamic_kern(data: *const FontData, left: u32, right: u32) -> i8 {
    if let Some(kern) = Font::new(&*data).kerning(left, right) {
        return kern;
    }
    (*data)
        .kern_handler
        .map_or(0, |kern| kern((*data).glyph_miss_ctx, left, right))
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_metrics(
    data: *const FontData,
    cp: u32,
    out: *mut Glyph,
) -> *const Glyph {
    if (cp != 0x2588 && cp != 0x25a0) || xp_font_coverage(data, cp) {
        return lookup(data, cp);
    }
    let em = if xp_font_coverage(data, 77) {
        lookup(data, 77)
    } else {
        core::ptr::null()
    };
    out.write(xp_core::font::solid_metrics(
        cp,
        (*data).ascender,
        Some(if em.is_null() { 0 } else { (*em).advance_x }),
    ));
    out
}
#[no_mangle]
pub extern "C" fn xp_font_style(style: u8, bold: bool, italic: bool, bold_italic: bool) -> u8 {
    match style & 3 {
        3 if bold_italic => 3,
        3 | 1 if bold => 1,
        3 | 2 if italic => 2,
        _ => 0,
    }
}
use xp_core::font::registry::{self, Candidate, Discovery, File};
#[no_mangle]
pub unsafe extern "C" fn xp_font_file_nearest(
    files: *const File,
    count: usize,
    size: u8,
    style: u8,
    exact: bool,
) -> i32 {
    registry::nearest(files, count, size, style, exact)
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_file_path(
    root: *const core::ffi::c_char,
    family: *const core::ffi::c_char,
    file: *const core::ffi::c_char,
    size: u8,
    loose: bool,
    out: *mut u8,
    capacity: usize,
) -> bool {
    registry::path(root, family, file, size, loose, out, capacity)
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_select_faces(
    candidates: *const Candidate,
    count: usize,
    files: *const File,
    selected: *mut i32,
) -> bool {
    registry::select_faces(candidates, count, files, selected)
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_scan_root(io: *const Discovery, root: *const core::ffi::c_char) {
    registry::scan_root(&*io, root);
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_sizes(
    files: *const File,
    count: usize,
    ctx: *mut c_void,
    emit: unsafe extern "C" fn(*mut c_void, u8),
) {
    registry::sizes(files, count, ctx, emit);
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_find_family(
    ctx: *const c_void,
    count: usize,
    get: unsafe extern "C" fn(*const c_void, usize) -> *const core::ffi::c_char,
    target: *const u8,
    len: usize,
) -> i32 {
    registry::find_family(ctx, count, get, core::slice::from_raw_parts(target, len))
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_root(
    family: *const core::ffi::c_char,
    hidden: *const core::ffi::c_char,
    visible: *const core::ffi::c_char,
    exists: unsafe extern "C" fn(*const core::ffi::c_char) -> bool,
    write: bool,
) -> *const core::ffi::c_char {
    registry::root(family, hidden, visible, exists, write)
}

#[no_mangle]
pub unsafe extern "C" fn xp_font_has_size(files: *const File, count: usize, size: u8) -> bool {
    registry::has_size(files, count, size)
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_discover(
    io: *const registry::Registry,
    hidden: *const core::ffi::c_char,
    visible: *const core::ffi::c_char,
    maximum: usize,
) -> bool {
    registry::discover(&*io, hidden, visible, maximum)
}
use xp_core::font::vector;
#[no_mangle]
pub unsafe extern "C" fn xp_vector_glyph(io: *const vector::Services, cp: u32) -> *const Glyph {
    vector::glyph(&*io, cp)
}
#[no_mangle]
pub unsafe extern "C" fn xp_vector_kern(io: *const vector::Services, left: u32, right: u32) -> i8 {
    vector::kern(&*io, left, right)
}
#[no_mangle]
pub unsafe extern "C" fn xp_vector_init(
    io: *const vector::Services,
    sources: *const vector::Source,
) {
    vector::init(&*io, sources);
}
#[no_mangle]
pub unsafe extern "C" fn xp_vector_load(
    family: *const vector::Family,
    point_size: u16,
    two_bit: bool,
    cap: usize,
    maximum: u16,
) -> bool {
    vector::load(&*family, point_size, two_bit, cap, maximum)
}
#[no_mangle]
pub unsafe extern "C" fn xp_vector_clear(family: *const vector::Family, resident: bool) {
    vector::clear(&*family, resident);
}
#[no_mangle]
pub unsafe extern "C" fn xp_vector_prewarm(
    family: *const vector::Family,
    text: *const u8,
    length: usize,
    reset: bool,
) -> bool {
    vector::prewarm(
        &*family,
        if text.is_null() {
            None
        } else {
            Some(core::slice::from_raw_parts(text, length))
        },
        reset,
    )
}
#[no_mangle]
pub unsafe extern "C" fn xp_vector_coverage(io: *const vector::Services, cp: u32) -> bool {
    vector::coverage(&*io, cp)
}
#[no_mangle]
pub unsafe extern "C" fn xp_vector_metrics(io: *const vector::Services, data: *mut FontData) {
    vector::metrics(&*io, data);
}
use xp_core::font::manager;
#[no_mangle]
pub unsafe extern "C" fn xp_font_manager_destroy(io: *const manager::Services) {
    manager::destroy(&*io);
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_manager_unload(io: *const manager::Services, id: i32, all: bool) {
    manager::unload(&*io, if all { None } else { Some(id) });
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_manager_get(
    io: *const manager::Services,
    family: *const core::ffi::c_char,
    len: usize,
    size: u8,
) -> i32 {
    manager::get(&*io, family, len, size)
}
#[no_mangle]
pub unsafe extern "C" fn xp_font_manager_load(
    io: *const manager::Services,
    family: *const manager::Family,
    size: u8,
    id: i32,
    mode: u8,
) -> i32 {
    manager::load(
        &*io,
        &*family,
        size,
        id,
        match mode {
            0 => manager::Mode::Replace,
            1 => manager::Mode::ExtraSize,
            2 => manager::Mode::Variant,
            _ => return 0,
        },
    )
}

#[no_mangle]
pub unsafe extern "C" fn xp_font_canonical(
    name: *const core::ffi::c_char,
    family: *const u8,
    len: usize,
    size: u8,
) -> bool {
    registry::canonical(
        core::ffi::CStr::from_ptr(name).to_bytes(),
        core::slice::from_raw_parts(family, len),
        size,
    )
}
