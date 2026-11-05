//! Vector font cache policy over FreeType and the existing PSRAM containers.
use super::raw::{Glyph, Ligature};
use core::{ffi::c_void, mem, ptr};

#[repr(C)]
pub struct State {
    pub used: usize,
    pub cap: usize,
    pub max_glyphs: u16,
    pub two_bit: bool,
    pub size_px: u16,
    pub source: u8,
    pub weight: i32,
    pub italic: bool,
    pub inited: bool,
    pub ready: bool,
    pub lig_gids: [u32; 5],
    pub lig_pairs: [Ligature; 5],
    pub lig_count: u8,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Buffer {
    pub data: *mut u8,
    pub len: usize,
    pub capacity: usize,
}
#[repr(C)]
#[derive(Default)]
pub struct Raster {
    pub pixels: *const u8,
    pub width: u16,
    pub height: u16,
    pub left: i16,
    pub yoff: i16,
    pub advance: i32,
    pub metrics: i32,
    pub have_metrics: bool,
}
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub state: unsafe extern "C" fn(*mut c_void) -> *mut State,
    pub init: unsafe extern "C" fn(*mut c_void),
    pub buffer: unsafe extern "C" fn(*mut c_void, u8) -> Buffer,
    pub reserve: unsafe extern "C" fn(*mut c_void, u8, usize),
    pub resize: unsafe extern "C" fn(*mut c_void, u8, usize),
    pub insert: unsafe extern "C" fn(*mut c_void, u8, usize, *const u8),
    pub release: unsafe extern "C" fn(*mut c_void, u8),
    pub heap: unsafe extern "C" fn(u8) -> usize,
    pub evict: unsafe extern "C" fn(*mut c_void),
    pub glyph_id: unsafe extern "C" fn(*mut c_void, u32) -> u32,
    pub raster: unsafe extern "C" fn(*mut c_void, u32, *mut Raster),
    pub kern: unsafe extern "C" fn(*mut c_void, u32, u32) -> i32,
    pub ft_init: unsafe extern "C" fn(*mut c_void, *const Source, u16, i32, bool) -> bool,
    pub options: unsafe extern "C" fn(*mut c_void),
    pub gpos_budget: unsafe extern "C" fn(*mut c_void, usize),
    pub gsub_budget: unsafe extern "C" fn(*mut c_void, usize),
    pub ligature: unsafe extern "C" fn(*mut c_void, *const u32, usize) -> u32,
    pub release_gsub: unsafe extern "C" fn(*mut c_void),
    pub has_glyph: unsafe extern "C" fn(*mut c_void, u32) -> bool,
    pub deinit: unsafe extern "C" fn(*mut c_void),
    pub line_metrics: unsafe extern "C" fn(*mut c_void, bool, *mut i32, *mut i32),
}
// Buffer IDs: bitmap, glyph records, sorted codepoints, slots, kern keys, kern values.
const SIZES: [usize; 6] = [1, mem::size_of::<Glyph>(), 4, 2, 8, 1];
// PsramVector growth must fit one block and leave 12 KiB internal headroom.
unsafe fn can_grow(io: &Services, bytes: usize) -> bool {
    (io.heap)(0) >= bytes || ((io.heap)(1) >= bytes && (io.heap)(2) >= bytes + 12 * 1024)
}
unsafe fn reserve(io: &Services, id: u8, need: usize, step: usize, ceiling: usize) -> bool {
    if (io.buffer)(io.ctx, id).capacity >= need {
        return true;
    }
    let count = need
        .div_ceil(step)
        .saturating_mul(step)
        .min(ceiling)
        .max(need);
    let Some(bytes) = count.checked_mul(*SIZES.get_unchecked(usize::from(id))) else {
        return false;
    };
    if !can_grow(io, bytes) {
        return false;
    }
    (io.reserve)(io.ctx, id, count);
    true
}
/// # Safety
/// Services lend one live face and its six containers; callbacks cannot reenter
/// its cache, except the eviction callback which locks this owner against release.
pub unsafe fn flush(io: &Services) {
    for id in 1..6 {
        (io.resize)(io.ctx, id, 0);
    }
    (*(io.state)(io.ctx)).used = 0;
}
unsafe fn position(io: &Services, id: u8, key: u64) -> (usize, bool) {
    let buffer = (io.buffer)(io.ctx, id);
    let read = |i| {
        if id == 2 {
            u64::from(*buffer.data.cast::<u32>().add(i))
        } else {
            *buffer.data.cast::<u64>().add(i)
        }
    };
    let (mut base, mut count) = (0usize, buffer.len);
    while count > 0 {
        let half = count / 2;
        let mid = base + half;
        if read(mid) < key {
            base = mid + 1;
            count -= half + 1;
        } else {
            count = half;
        }
    }
    (base, base != buffer.len && read(base) == key)
}
/// # Safety
/// Same loan contract as flush; result expires on the next glyph fault or release.
pub unsafe fn glyph(io: &Services, cp: u32) -> *const Glyph {
    if !(*(io.state)(io.ctx)).inited {
        (io.init)(io.ctx);
    }
    if !(*(io.state)(io.ctx)).ready {
        return ptr::null();
    }
    let (pos, found) = position(io, 2, u64::from(cp));
    if found {
        let index = *(io.buffer)(io.ctx, 3).data.cast::<u16>().add(pos);
        return (io.buffer)(io.ctx, 1)
            .data
            .cast::<Glyph>()
            .add(usize::from(index));
    }
    let mut gid = (io.glyph_id)(io.ctx, cp);
    if gid == 0 && (0xfb00..=0xfb04).contains(&cp) {
        gid = *(*(io.state)(io.ctx))
            .lig_gids
            .get_unchecked((cp - 0xfb00) as usize);
    }
    if gid == 0 {
        return ptr::null();
    }
    let mut raster = Raster::default();
    (io.raster)(io.ctx, gid, &mut raster);
    let state = &*(io.state)(io.ctx);
    let mut pixels = if raster.pixels.is_null() {
        0
    } else {
        u32::from(raster.width) * u32::from(raster.height)
    };
    let mut bytes = if state.two_bit {
        pixels.div_ceil(4)
    } else {
        pixels.div_ceil(8)
    } as usize;
    if bytes > state.cap {
        bytes = 0;
        pixels = 0;
    }
    if (io.buffer)(io.ctx, 1).len >= usize::from(state.max_glyphs) || state.used + bytes > state.cap
    {
        flush(io);
    }
    let grow_tables = || {
        let maximum = usize::from((*(io.state)(io.ctx)).max_glyphs);
        reserve(io, 1, (io.buffer)(io.ctx, 1).len + 1, 64, maximum)
            && reserve(io, 2, (io.buffer)(io.ctx, 2).len + 1, 64, maximum)
            && reserve(io, 3, (io.buffer)(io.ctx, 3).len + 1, 64, maximum)
    };
    if !grow_tables() {
        (io.evict)(io.ctx);
        if !grow_tables() {
            return ptr::null();
        }
    }
    let state = &*(io.state)(io.ctx);
    if pixels != 0 && !reserve(io, 0, state.used + bytes, 4096, state.cap) {
        (io.evict)(io.ctx);
        let state = &*(io.state)(io.ctx);
        if !reserve(io, 0, state.used + bytes, 4096, state.cap) {
            bytes = 0;
            pixels = 0;
        }
    }
    let advance = if raster.have_metrics {
        (raster.metrics + 2) >> 2
    } else {
        raster.advance << 4
    };
    let mut glyph = Glyph {
        width: 0,
        height: 0,
        advance_x: advance.max(0) as u16,
        left: 0,
        top: 0,
        data_length: 0,
        data_offset: (*(io.state)(io.ctx)).used as u32,
    };
    if pixels != 0 && !raster.pixels.is_null() {
        let used = (*(io.state)(io.ctx)).used;
        if (io.buffer)(io.ctx, 0).len < used + bytes {
            (io.resize)(io.ctx, 0, used + bytes);
        }
        let dst = (io.buffer)(io.ctx, 0).data.add(used);
        ptr::write_bytes(dst, 0, bytes);
        let two_bit = (*(io.state)(io.ctx)).two_bit;
        for i in 0..pixels as usize {
            let alpha = *raster.pixels.add(i);
            if two_bit {
                let v = if alpha < 64 {
                    0
                } else if alpha < 128 {
                    1
                } else if alpha < 192 {
                    2
                } else {
                    3
                };
                *dst.add(i >> 2) |= v << ((3 - (i & 3)) * 2);
            } else if alpha >= 128 {
                *dst.add(i >> 3) |= 1 << (7 - (i & 7));
            }
        }
        glyph.width = raster.width as u8;
        glyph.height = raster.height as u8;
        glyph.left = raster.left;
        glyph.top = raster.yoff.wrapping_neg();
        glyph.data_length = bytes as u16;
        (*(io.state)(io.ctx)).used += bytes;
    }
    let index = (io.buffer)(io.ctx, 1).len as u16;
    (io.insert)(
        io.ctx,
        1,
        usize::from(index),
        (&glyph as *const Glyph).cast(),
    );
    let (pos, _) = position(io, 2, u64::from(cp));
    (io.insert)(io.ctx, 2, pos, (&cp as *const u32).cast());
    (io.insert)(io.ctx, 3, pos, (&index as *const u16).cast());
    (io.buffer)(io.ctx, 1)
        .data
        .cast::<Glyph>()
        .add(usize::from(index))
}
/// # Safety
/// Same services contract as glyph.
pub unsafe fn kern(io: &Services, left: u32, right: u32) -> i8 {
    if left == 0 || right == 0 {
        return 0;
    }
    if !(*(io.state)(io.ctx)).inited {
        (io.init)(io.ctx);
    }
    if !(*(io.state)(io.ctx)).ready {
        return 0;
    }
    let key = (u64::from(left) << 32) | u64::from(right);
    let (pos, found) = position(io, 4, key);
    if found {
        return *(io.buffer)(io.ctx, 5).data.cast::<i8>().add(pos);
    }
    let k = (io.kern)(io.ctx, left, right);
    let value = ((k + if k < 0 { -2 } else { 2 }) / 4).clamp(-128, 127) as i8;
    if (io.buffer)(io.ctx, 4).len >= 512 {
        (io.resize)(io.ctx, 4, 0);
        (io.resize)(io.ctx, 5, 0);
    }
    if !reserve(io, 4, 512, 512, 512) || !reserve(io, 5, 512, 512, 512) {
        return value;
    }
    let (pos, _) = position(io, 4, key);
    (io.insert)(io.ctx, 4, pos, (&key as *const u64).cast());
    (io.insert)(io.ctx, 5, pos, (&value as *const i8).cast());
    value
}
/// # Safety
/// Same services contract as flush; caller retains the regular FT face.
pub unsafe fn release(io: &Services) {
    flush(io);
    for id in 0..6 {
        (io.release)(io.ctx, id);
    }
}
/// Resolve source/style policy without allocating or constructing FT faces.
pub fn source(style: u8, present: u8) -> (u8, i32, bool) {
    match style {
        1 if present & 2 != 0 => (1, 400, false),
        1 => (0, 700, false),
        2 if present & 4 != 0 => (2, 400, false),
        2 => (0, 400, true),
        3 if present & 8 != 0 => (3, 400, false),
        3 if present & 4 != 0 => (2, 700, false),
        3 if present & 2 != 0 => (1, 400, true),
        3 => (0, 700, true),
        _ => (0, 400, false),
    }
}
#[repr(C)]
pub struct Source {
    pub present: bool,
    pub streamed: bool,
    pub data: *const u8,
    pub len: u32,
    pub read: Option<
        unsafe extern "C" fn(
            *mut c_void,
            core::ffi::c_ulong,
            *mut u8,
            core::ffi::c_ulong,
        ) -> core::ffi::c_ulong,
    >,
    pub ctx: *mut c_void,
    pub file_size: core::ffi::c_ulong,
}
#[repr(C)]
pub struct Family {
    pub ctx: *mut c_void,
    pub sources: *const Source,
    pub size: *mut u32,
    pub loaded: *mut bool,
    pub locked: *const bool,
    pub face: unsafe extern "C" fn(*mut c_void, u8) -> Services,
    pub setup: unsafe extern "C" fn(*mut c_void),
}
/// # Safety
/// Sources are four stable borrowed source descriptors; services loan one face.
pub unsafe fn init(io: &Services, sources: *const Source) {
    let state = (io.state)(io.ctx);
    if (*state).inited {
        return;
    }
    (*state).inited = true;
    let source = &*sources.add(usize::from((*state).source));
    (*state).ready = source.present
        && (io.ft_init)(
            io.ctx,
            source,
            (*state).size_px,
            (*state).weight,
            (*state).italic,
        );
    if (*state).ready {
        (io.options)(io.ctx);
        (io.gpos_budget)(io.ctx, 0);
        (io.gsub_budget)(io.ctx, 48 * 1024);
        const COMPONENTS: [[u32; 3]; 5] = [
            [102, 102, 0],
            [102, 105, 0],
            [102, 108, 0],
            [102, 102, 105],
            [102, 102, 108],
        ];
        for (i, components) in COMPONENTS.iter().enumerate() {
            *(*state).lig_gids.get_unchecked_mut(i) = (io.ligature)(
                io.ctx,
                components.as_ptr(),
                if components[2] == 0 { 2 } else { 3 },
            );
        }
        (io.release_gsub)(io.ctx);
        let available = move |i: usize| {
            *(*state).lig_gids.get_unchecked(i) != 0 || (io.has_glyph)(io.ctx, 0xfb00 + i as u32)
        };
        (*state).lig_count = 0;
        let add = move |left: u32, right: u32, cp: u32| {
            let pos = usize::from((*state).lig_count);
            *(*state).lig_pairs.get_unchecked_mut(pos) = Ligature {
                pair: (left << 16) | right,
                ligature_cp: cp,
            };
            (*state).lig_count += 1;
        };
        if available(0) {
            add(102, 102, 0xfb00);
        }
        if available(1) {
            add(102, 105, 0xfb01);
        }
        if available(2) {
            add(102, 108, 0xfb02);
        }
        if available(0) {
            if available(3) {
                add(0xfb00, 105, 0xfb03);
            }
            if available(4) {
                add(0xfb00, 108, 0xfb04);
            }
        }
    }
}
/// # Safety
/// Four face services and the family scalar pointers remain live throughout load.
pub unsafe fn load(
    family: &Family,
    point_size: u16,
    two_bit: bool,
    cap: usize,
    maximum: u16,
) -> bool {
    *family.loaded = false;
    if !(*family.sources).present {
        return false;
    }
    *family.size = (u32::from(point_size) * 150 * 64 + 36) / 72;
    let size_px = ((*family.size + 32) >> 6) as u16;
    let mut mask = 0;
    for i in 0..4 {
        if (*family.sources.add(i)).present {
            mask |= 1 << i;
        }
    }
    for i in 0..4 {
        let io = (family.face)(family.ctx, i);
        let state = (io.state)(io.ctx);
        let (source, weight, italic) = source(i, mask);
        (*state).source = source;
        (*state).weight = weight;
        (*state).italic = italic;
    }
    for i in 0..4 {
        let io = (family.face)(family.ctx, i);
        let state = (io.state)(io.ctx);
        (*state).two_bit = two_bit;
        (*state).size_px = size_px;
        (*state).cap = cap;
        (*state).max_glyphs = maximum;
        (*state).inited = false;
        (*state).ready = false;
        flush(&io);
        (*state).lig_count = 0;
        (*state).lig_gids.fill(0);
    }
    let regular = (family.face)(family.ctx, 0);
    (regular.init)(regular.ctx);
    if !(*(regular.state)(regular.ctx)).ready {
        return false;
    }
    for i in 1..4 {
        let io = (family.face)(family.ctx, i);
        (family.setup)(io.ctx);
    }
    *family.loaded = true;
    true
}
/// # Safety
/// Same family/face contract as load. Release is suppressed during eviction.
pub unsafe fn clear(family: &Family, resident: bool) {
    if resident && *family.locked {
        return;
    }
    for i in 0..4 {
        let io = (family.face)(family.ctx, i);
        let state = (io.state)(io.ctx);
        if resident {
            release(&io);
            if i != 0 && (*state).inited {
                (io.deinit)(io.ctx);
                (*state).inited = false;
                (*state).ready = false;
            }
        } else if (*state).inited {
            flush(&io);
        }
    }
}
/// # Safety
/// The text loan is valid for the call and the family owns all face storage.
pub unsafe fn prewarm(family: &Family, text: Option<&[u8]>, reset: bool) -> bool {
    if !*family.loaded {
        return false;
    }
    let io = (family.face)(family.ctx, 0);
    if reset {
        flush(&io);
    }
    let Some(text) = text else {
        return false;
    };
    let mut position = 0;
    while let Some(cp) = crate::utf8::next(text, &mut position) {
        glyph(&io, cp);
    }
    true
}
/// # Safety
/// The always-live regular face services remain valid for the call.
pub unsafe fn coverage(io: &Services, cp: u32) -> bool {
    (io.has_glyph)(io.ctx, cp)
        || ((0xfb00..=0xfb04).contains(&cp)
            && *(*(io.state)(io.ctx))
                .lig_gids
                .get_unchecked((cp - 0xfb00) as usize)
                != 0)
}
/// # Safety
/// data is writable and its owner wires the C++ miss/coverage handlers afterward.
pub unsafe fn metrics(io: &Services, data: *mut super::raw::FontData) {
    let state = &*(io.state)(io.ctx);
    let (mut ascent, mut height) = (0, 0);
    (io.line_metrics)(io.ctx, !state.ready, &mut ascent, &mut height);
    ptr::write_bytes(data, 0, 1);
    (*data).advance_y = height.clamp(0, 255) as u8;
    (*data).ascender = ascent;
    (*data).descender = (height - ascent).max(0);
    (*data).is2_bit = state.two_bit;
    (*data).ligature_pairs = if state.lig_count != 0 {
        state.lig_pairs.as_ptr()
    } else {
        ptr::null()
    };
    (*data).ligature_pair_count = u32::from(state.lig_count);
}
