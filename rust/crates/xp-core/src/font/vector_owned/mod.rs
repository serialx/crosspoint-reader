//! Owned vector faces and cache storage.
mod buffer;
pub mod registration;
use super::{
    raw::{Glyph, Ligature},
    vector,
};
use buffer::Buffer;
pub use buffer::Memory;
use core::{
    ffi::c_void,
    marker::PhantomPinned,
    pin::Pin,
    ptr::{self, NonNull},
};

#[derive(Clone, Copy)]
pub enum Source<'a> {
    Missing,
    Resident(&'a [u8]),
    /// The file slot is pinned and borrowed until every family using it is dropped.
    Stream {
        file: NonNull<c_void>,
        size: u32,
        prefix: &'a [u8],
    },
}
#[derive(Clone, Copy)]
pub struct Config {
    pub size_px: u16,
    pub weight: i32,
    pub italic: bool,
    pub auto_hint: bool,
    pub stem_darkening: bool,
    pub gpos_budget: usize,
    pub gsub_budget: usize,
}
pub struct Raster<'a> {
    pub pixels: &'a [u8],
    pub width: u16,
    pub height: u16,
    pub left: i16,
    pub yoff: i16,
    pub advance: i32,
    pub metrics: Option<i32>,
}
/// SDK face storage stays pinned from its first open through destruction.
/// # Safety
/// Implementations neither reenter Rust policy nor invalidate another face's
/// raster. Open borrows its source until reopen/drop; raster covers width*height
/// bytes or is empty. Failed open leaves a destructible face. Drop releases it.
pub unsafe trait Rasterizer: Default {
    /// # Safety
    /// Source bytes and its pinned file slot outlive this face's next open/drop.
    unsafe fn open(self: Pin<&mut Self>, source: Source<'_>, config: Config) -> bool;
    fn close(self: Pin<&mut Self>);
    fn glyph_id(self: Pin<&mut Self>, cp: u32) -> u32;
    fn raster(self: Pin<&mut Self>, gid: u32, size: u32) -> Raster<'_>;
    fn kern(self: Pin<&mut Self>, left: u32, right: u32, size: u32) -> i32;
    fn line_metrics(self: Pin<&mut Self>, size: u32, pixels: u16) -> (i32, i32);
    fn ligature(self: Pin<&mut Self>, components: &[u32]) -> u32;
    fn release_gsub(self: Pin<&mut Self>);
}
pub use super::pressure::{reclaim, Pressure};
#[derive(Clone, Copy, Default)]
pub struct Metrics {
    pub ascent: i32,
    pub height: u8,
    pub descent: i32,
}
/// A glyph loan prevents cache mutation until its last use.
/// ```compile_fail
/// use core::pin::Pin;
/// use xp_core::font::vector_owned::{Family, Memory, Pressure, Rasterizer};
/// fn invalid<P: Rasterizer, M: Memory>(mut f: Pin<&mut Family<'_,P,M>>, p: &mut impl Pressure) {
///     let loan = f.as_mut().glyph(0, 65, p).unwrap();
///     f.as_mut().clear(true);
///     assert!(!loan.bitmap.is_empty());
/// }
/// ```
pub struct GlyphLoan<'a> {
    pub glyph: &'a Glyph,
    pub bitmap: &'a [u8],
}
struct Face<P: Rasterizer, M: Memory> {
    ft: P,
    bitmap: Buffer<u8, M>,
    glyphs: Buffer<Glyph, M>,
    cps: Buffer<u32, M>,
    slots: Buffer<u16, M>,
    kern_keys: Buffer<u64, M>,
    kern_values: Buffer<i8, M>,
    used: usize,
    inited: bool,
    ready: bool,
    lig_gids: [u32; 5],
    lig_pairs: [Ligature; 5],
    lig_count: usize,
    metrics: Metrics,
}
impl<P: Rasterizer, M: Memory> Face<P, M> {
    fn new(memory: M) -> Self {
        Self {
            ft: P::default(),
            bitmap: Buffer::new(memory),
            glyphs: Buffer::new(memory),
            cps: Buffer::new(memory),
            slots: Buffer::new(memory),
            kern_keys: Buffer::new(memory),
            kern_values: Buffer::new(memory),
            used: 0,
            inited: false,
            ready: false,
            lig_gids: [0; 5],
            lig_pairs: [Ligature {
                pair: 0,
                ligature_cp: 0,
            }; 5],
            lig_count: 0,
            metrics: Metrics::default(),
        }
    }
    fn flush(&mut self) {
        self.glyphs.clear();
        self.cps.clear();
        self.slots.clear();
        self.kern_keys.clear();
        self.kern_values.clear();
        self.used = 0;
    }
    fn release(&mut self) {
        self.flush();
        self.bitmap.release();
        self.glyphs.release();
        self.cps.release();
        self.slots.release();
        self.kern_keys.release();
        self.kern_values.release();
    }
}
/// Sources are borrowed; the family owns every face and every glyph-cache buffer.
/// Pin before load. No pointer into a movable registration table is stored here.
pub struct Family<'a, P: Rasterizer, M: Memory> {
    sources: [Source<'a>; 4],
    faces: [Face<P, M>; 4],
    size: u32,
    pixels: u16,
    cap: usize,
    maximum: u16,
    two_bit: bool,
    loaded: bool,
    generation: u64,
    _pin: PhantomPinned,
}
impl<'a, P: Rasterizer, M: Memory> Family<'a, P, M> {
    /// # Safety
    /// Out is exclusive uninitialized storage, pinned through destruction.
    /// Sources obey new's lifetime and serialization contract.
    pub unsafe fn initialize(out: *mut Self, sources: [Source<'a>; 4], memory: M) {
        ptr::addr_of_mut!((*out).sources).write(sources);
        for i in 0..4 {
            ptr::addr_of_mut!((*out).faces)
                .cast::<Face<P, M>>()
                .add(i)
                .write(Face::new(memory));
        }
        ptr::addr_of_mut!((*out).size).write(0);
        ptr::addr_of_mut!((*out).pixels).write(0);
        ptr::addr_of_mut!((*out).cap).write(0);
        ptr::addr_of_mut!((*out).maximum).write(0);
        ptr::addr_of_mut!((*out).two_bit).write(true);
        ptr::addr_of_mut!((*out).loaded).write(false);
        ptr::addr_of_mut!((*out).generation).write(0);
        ptr::addr_of_mut!((*out)._pin).write(PhantomPinned);
    }
    /// # Safety
    /// Every streamed HAL slot remains pinned/live through this family's drop;
    /// operations on families sharing a slot are synchronous and serialized.
    pub unsafe fn new(sources: [Source<'a>; 4], memory: M) -> Self {
        Self {
            sources,
            faces: core::array::from_fn(|_| Face::new(memory)),
            size: 0,
            pixels: 0,
            cap: 0,
            maximum: 0,
            two_bit: true,
            loaded: false,
            generation: 0,
            _pin: PhantomPinned,
        }
    }
    fn changed(&mut self) {
        self.generation = self.generation.wrapping_add(1);
    }
    pub fn generation(&self) -> u64 {
        self.generation
    }
    pub fn metrics(&self, style: usize) -> Metrics {
        self.faces[style].metrics
    }
    pub fn ligatures(&self, style: usize) -> &[Ligature] {
        &self.faces[style].lig_pairs[..self.faces[style].lig_count]
    }
    pub fn bitmap(&self, style: usize, glyph: &Glyph) -> Option<&[u8]> {
        let start = glyph.data_offset as usize;
        self.faces[style]
            .bitmap
            .values()
            .get(start..start.checked_add(usize::from(glyph.data_length))?)
    }
    // All callers have a pinned Family; these helpers never move a face or its FT slot.
    fn init(&mut self, style: usize) {
        if self.faces[style].inited {
            return;
        }
        let present = self.sources.iter().enumerate().fold(0, |mask, (i, s)| {
            mask | if matches!(s, Source::Missing) {
                0
            } else {
                1 << i
            }
        });
        let (source, weight, italic) = vector::source(style as u8, present);
        let face = &mut self.faces[style];
        face.inited = true;
        face.ready = !matches!(self.sources[usize::from(source)], Source::Missing)
            && unsafe {
                Pin::new_unchecked(&mut face.ft).open(
                    self.sources[usize::from(source)],
                    Config {
                        size_px: self.pixels,
                        weight,
                        italic,
                        auto_hint: true,
                        stem_darkening: true,
                        gpos_budget: 0,
                        gsub_budget: 48 * 1024,
                    },
                )
            };
        if face.ready {
            let mut ft = unsafe { Pin::new_unchecked(&mut face.ft) };
            for (i, cps) in [
                &[102, 102][..],
                &[102, 105],
                &[102, 108],
                &[102, 102, 105],
                &[102, 102, 108],
            ]
            .iter()
            .enumerate()
            {
                face.lig_gids[i] = ft.as_mut().ligature(cps);
            }
            ft.as_mut().release_gsub();
            let mut available = [false; 5];
            for (i, a) in available.iter_mut().enumerate() {
                *a = face.lig_gids[i] != 0 || ft.as_mut().glyph_id(0xfb00 + i as u32) != 0;
            }
            face.lig_count = 0;
            for (i, (left, right, ok)) in [
                (102, 102, available[0]),
                (102, 105, available[1]),
                (102, 108, available[2]),
                (0xfb00, 105, available[0] && available[3]),
                (0xfb00, 108, available[0] && available[4]),
            ]
            .into_iter()
            .enumerate()
            {
                if ok {
                    face.lig_pairs[face.lig_count] = Ligature {
                        pair: (left << 16) | right,
                        ligature_cp: 0xfb00 + i as u32,
                    };
                    face.lig_count += 1;
                }
            }
        }
        self.setup(style);
    }
    fn setup(&mut self, style: usize) {
        let target = if self.faces[style].ready { style } else { 0 };
        let (ascent, height) = unsafe { Pin::new_unchecked(&mut self.faces[target].ft) }
            .line_metrics(self.size, self.pixels);
        self.faces[style].metrics = Metrics {
            ascent,
            height: height.clamp(0, 255) as u8,
            descent: (height - ascent).max(0),
        };
    }
    pub fn load(
        self: Pin<&mut Self>,
        points: u16,
        two_bit: bool,
        cap: usize,
        maximum: u16,
    ) -> bool {
        let this = unsafe { self.get_unchecked_mut() };
        this.changed();
        this.loaded = false;
        if matches!(this.sources[0], Source::Missing) {
            return false;
        }
        this.size = (u32::from(points) * crate::textm::FONT_RASTER_DPI * 64 + 36) / 72;
        this.pixels = ((this.size + 32) >> 6) as u16;
        this.cap = cap;
        this.maximum = maximum;
        this.two_bit = two_bit;
        for face in &mut this.faces {
            face.inited = false;
            face.ready = false;
            face.flush();
            face.lig_count = 0;
            face.lig_gids.fill(0);
        }
        this.init(0);
        if !this.faces[0].ready {
            return false;
        }
        for style in 1..4 {
            this.setup(style);
        }
        this.loaded = true;
        true
    }
    pub fn clear(self: Pin<&mut Self>, resident: bool) {
        let this = unsafe { self.get_unchecked_mut() };
        this.changed();
        for (i, face) in this.faces.iter_mut().enumerate() {
            if resident {
                face.release();
                if i != 0 && face.inited {
                    unsafe { Pin::new_unchecked(&mut face.ft) }.close();
                    face.inited = false;
                    face.ready = false;
                }
            } else if face.inited {
                face.flush();
            }
        }
    }
    pub fn coverage(self: Pin<&mut Self>, cp: u32) -> bool {
        let this = unsafe { self.get_unchecked_mut() };
        unsafe { Pin::new_unchecked(&mut this.faces[0].ft) }.glyph_id(cp) != 0
            || ((0xfb00..=0xfb04).contains(&cp)
                && this.faces[0].lig_gids[(cp - 0xfb00) as usize] != 0)
    }
    pub fn glyph(
        self: Pin<&mut Self>,
        style: usize,
        cp: u32,
        pressure: &mut impl Pressure,
    ) -> Option<GlyphLoan<'_>> {
        let this = unsafe { self.get_unchecked_mut() };
        this.init(style);
        let (before, tail) = this.faces.split_at_mut(style);
        let (face, after) = tail.split_first_mut().unwrap();
        if !face.ready {
            return None;
        }
        let pos = face.cps.values().partition_point(|&v| v < cp);
        if face.cps.values().get(pos) == Some(&cp) {
            let g = &face.glyphs.values()[usize::from(face.slots.values()[pos])];
            return Some(GlyphLoan {
                glyph: g,
                bitmap: &face.bitmap.values()
                    [g.data_offset as usize..g.data_offset as usize + usize::from(g.data_length)],
            });
        }
        this.generation = this.generation.wrapping_add(1);
        let mut ft = unsafe { Pin::new_unchecked(&mut face.ft) };
        let mut gid = ft.as_mut().glyph_id(cp);
        if gid == 0 && (0xfb00..=0xfb04).contains(&cp) {
            gid = face.lig_gids[(cp - 0xfb00) as usize];
        }
        if gid == 0 && cp != 0xfffd {
            return None;
        }
        // The raster loan excludes FT mutation; pressure can only touch other families.
        let raster = ft.as_mut().raster(gid, this.size);
        let mut pixels = if raster.pixels.is_empty() {
            0
        } else {
            usize::from(raster.width) * usize::from(raster.height)
        };
        let mut bytes = if this.two_bit {
            pixels.div_ceil(4)
        } else {
            pixels.div_ceil(8)
        };
        if bytes > this.cap {
            bytes = 0;
            pixels = 0;
        }
        if face.glyphs.values().len() >= usize::from(this.maximum) || face.used + bytes > this.cap {
            face.glyphs.clear();
            face.cps.clear();
            face.slots.clear();
            face.kern_keys.clear();
            face.kern_values.clear();
            face.used = 0;
        }
        macro_rules! flush_active {
            () => {{
                face.glyphs.clear();
                face.cps.clear();
                face.slots.clear();
                face.kern_keys.clear();
                face.kern_values.clear();
                face.used = 0;
                for other in before.iter_mut().chain(after.iter_mut()) {
                    if other.inited {
                        other.flush();
                    }
                }
                this.generation = this.generation.wrapping_add(1);
            }};
        }
        macro_rules! tables {
            () => {{
                face.glyphs.reserve(
                    face.glyphs.values().len() + 1,
                    64,
                    usize::from(this.maximum),
                ) && face
                    .cps
                    .reserve(face.cps.values().len() + 1, 64, usize::from(this.maximum))
                    && face.slots.reserve(
                        face.slots.values().len() + 1,
                        64,
                        usize::from(this.maximum),
                    )
            }};
        }
        if !tables!() {
            reclaim(pressure);
            if pressure.clear_active_frame() {
                flush_active!();
            }
            if !tables!() {
                return None;
            }
        }
        if pixels != 0 && !face.bitmap.reserve(face.used + bytes, 4096, this.cap) {
            reclaim(pressure);
            if pressure.clear_active_frame() {
                flush_active!();
            }
            if !face.bitmap.reserve(face.used + bytes, 4096, this.cap) {
                bytes = 0;
                pixels = 0;
            }
        }
        let advance = raster.metrics.map_or(raster.advance << 4, |m| (m + 2) >> 2);
        let mut glyph = Glyph {
            width: 0,
            height: 0,
            advance_x: advance.max(0) as u16,
            left: 0,
            top: 0,
            data_length: 0,
            data_offset: face.used as u32,
        };
        if pixels != 0 {
            if face.bitmap.values().len() < face.used + bytes {
                face.bitmap.resize(face.used + bytes, 0);
            }
            let dst = &mut face.bitmap.values_mut()[face.used..face.used + bytes];
            dst.fill(0);
            for (i, &alpha) in raster.pixels[..pixels].iter().enumerate() {
                if this.two_bit {
                    dst[i >> 2] |= (alpha / 64) << ((3 - (i & 3)) * 2);
                } else if alpha >= 128 {
                    dst[i >> 3] |= 1 << (7 - (i & 7));
                }
            }
            glyph.width = raster.width as u8;
            glyph.height = raster.height as u8;
            glyph.left = raster.left;
            glyph.top = raster.yoff.wrapping_neg();
            glyph.data_length = bytes as u16;
            face.used += bytes;
        }
        let index = face.glyphs.values().len();
        face.glyphs.insert(index, glyph);
        let pos = face.cps.values().partition_point(|&v| v < cp);
        face.cps.insert(pos, cp);
        face.slots.insert(pos, index as u16);
        let g = &face.glyphs.values()[index];
        Some(GlyphLoan {
            glyph: g,
            bitmap: &face.bitmap.values()
                [g.data_offset as usize..g.data_offset as usize + usize::from(g.data_length)],
        })
    }
    pub fn kern(self: Pin<&mut Self>, style: usize, left: u32, right: u32) -> i8 {
        if left == 0 || right == 0 {
            return 0;
        }
        let this = unsafe { self.get_unchecked_mut() };
        this.init(style);
        let face = &mut this.faces[style];
        if !face.ready {
            return 0;
        }
        let key = (u64::from(left) << 32) | u64::from(right);
        let pos = face.kern_keys.values().partition_point(|&v| v < key);
        if face.kern_keys.values().get(pos) == Some(&key) {
            return face.kern_values.values()[pos];
        }
        let k = unsafe { Pin::new_unchecked(&mut face.ft) }.kern(left, right, this.size);
        let value = ((k + if k < 0 { -2 } else { 2 }) / 4).clamp(-128, 127) as i8;
        if face.kern_keys.values().len() >= 512 {
            face.kern_keys.clear();
            face.kern_values.clear();
        }
        if face.kern_keys.reserve(512, 512, 512) && face.kern_values.reserve(512, 512, 512) {
            let pos = face.kern_keys.values().partition_point(|&v| v < key);
            face.kern_keys.insert(pos, key);
            face.kern_values.insert(pos, value);
        }
        value
    }
    pub fn prewarm(
        mut self: Pin<&mut Self>,
        text: Option<&[u8]>,
        reset: bool,
        pressure: &mut impl Pressure,
    ) -> bool {
        if !self.loaded {
            return false;
        }
        if reset {
            let this = unsafe { self.as_mut().get_unchecked_mut() };
            this.changed();
            this.faces[0].flush();
        }
        let Some(text) = text else {
            return false;
        };
        let mut position = 0;
        while let Some(cp) = crate::utf8::next(text, &mut position) {
            self.as_mut().glyph(0, cp, pressure);
        }
        true
    }
}

#[cfg(test)]
mod tests;
