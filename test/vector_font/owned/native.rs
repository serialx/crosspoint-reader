//! Host fixture ABI: C++ owns live source/file slots and serializes every call.
//! Owner handles are unique, styles are 0..4, and output pointers have their
//! declared capacities; no loan survives another operation on its owner.
use core::{
    cell::UnsafeCell, ffi::c_void, marker::PhantomPinned, mem::MaybeUninit, pin::Pin, ptr::NonNull,
};
use xp_core::font::{raw::Glyph, vector_owned::*};
#[repr(C)]
pub struct SourceView {
    mode: u8,
    data: *const u8,
    length: u32,
    file: *mut c_void,
    size: u32,
}
#[repr(C)]
struct FtConfig {
    pixels: u16,
    weight: i32,
    italic: bool,
    auto_hint: bool,
    stem_darkening: bool,
    gpos_budget: usize,
    gsub_budget: usize,
}
#[repr(C)]
#[derive(Default)]
struct FtRaster {
    pixels: *const u8,
    width: u16,
    height: u16,
    left: i16,
    yoff: i16,
    advance: i32,
    metrics: i32,
    have_metrics: bool,
}
#[repr(C)]
struct FtSlot {
    words: [MaybeUninit<usize>; 40],
}
extern "C" {
    fn xp_native_ft_open(
        slot: *mut FtSlot,
        source: *const SourceView,
        config: *const FtConfig,
    ) -> bool;
    fn xp_native_ft_close(slot: *mut FtSlot);
    fn xp_native_ft_glyph_id(slot: *mut FtSlot, cp: u32) -> u32;
    fn xp_native_ft_raster(slot: *mut FtSlot, gid: u32, size: u32, out: *mut FtRaster);
    fn xp_native_ft_kern(slot: *mut FtSlot, left: u32, right: u32, size: u32) -> i32;
    fn xp_native_ft_line_metrics(
        slot: *mut FtSlot,
        size: u32,
        pixels: u16,
        ascent: *mut i32,
        height: *mut i32,
    );
    fn xp_native_ft_ligature(slot: *mut FtSlot, cps: *const u32, count: usize) -> u32;
    fn xp_native_ft_release_gsub(slot: *mut FtSlot);
    fn fiFontMalloc(bytes: usize) -> *mut c_void;
    fn fiFontFree(p: *mut c_void);
    fn vector_owner_heap(metric: u8) -> usize;
    fn vector_owner_pressure(sink: u8) -> usize;
}
// Never create Rust references into live C++ storage: FT retains an interior
// stream-context pointer, and all access goes through the physical leaves.
struct Native {
    slot: UnsafeCell<FtSlot>,
    live: bool,
    _pin: PhantomPinned,
}
impl Default for Native {
    fn default() -> Self {
        Self {
            slot: UnsafeCell::new(FtSlot {
                words: [MaybeUninit::uninit(); 40],
            }),
            _pin: PhantomPinned,
            live: false,
        }
    }
}
impl Drop for Native {
    fn drop(&mut self) {
        if self.live {
            unsafe { xp_native_ft_close(self.slot.get()) }
        }
    }
}
unsafe impl Rasterizer for Native {
    unsafe fn open(self: Pin<&mut Self>, source: Source<'_>, config: Config) -> bool {
        let this = self.get_unchecked_mut();
        if this.live {
            xp_native_ft_close(this.slot.get());
            this.live = false;
        }
        let source = match source {
            Source::Missing => return false,
            Source::Resident(bytes) => SourceView {
                mode: 1,
                data: bytes.as_ptr(),
                length: bytes.len() as u32,
                file: core::ptr::null_mut(),
                size: bytes.len() as u32,
            },
            Source::Stream { file, size, prefix } => SourceView {
                mode: 2,
                data: prefix.as_ptr(),
                length: prefix.len() as u32,
                file: file.as_ptr(),
                size,
            },
        };
        this.live = xp_native_ft_open(
            this.slot.get(),
            &source,
            &FtConfig {
                pixels: config.size_px,
                weight: config.weight,
                italic: config.italic,
                auto_hint: config.auto_hint,
                stem_darkening: config.stem_darkening,
                gpos_budget: config.gpos_budget,
                gsub_budget: config.gsub_budget,
            },
        );
        this.live
    }
    fn close(self: Pin<&mut Self>) {
        let this = unsafe { self.get_unchecked_mut() };
        if this.live {
            unsafe { xp_native_ft_close(this.slot.get()) };
            this.live = false;
        }
    }
    fn glyph_id(self: Pin<&mut Self>, cp: u32) -> u32 {
        let this = unsafe { self.get_unchecked_mut() };
        if this.live {
            unsafe { xp_native_ft_glyph_id(this.slot.get(), cp) }
        } else {
            0
        }
    }
    fn raster(self: Pin<&mut Self>, gid: u32, size: u32) -> Raster<'_> {
        let this = unsafe { self.get_unchecked_mut() };
        let mut r = FtRaster::default();
        unsafe { xp_native_ft_raster(this.slot.get(), gid, size, &mut r) };
        Raster {
            pixels: if r.pixels.is_null() {
                &[]
            } else {
                unsafe {
                    core::slice::from_raw_parts(
                        r.pixels,
                        usize::from(r.width) * usize::from(r.height),
                    )
                }
            },
            width: r.width,
            height: r.height,
            left: r.left,
            yoff: r.yoff,
            advance: r.advance,
            metrics: r.have_metrics.then_some(r.metrics),
        }
    }
    fn kern(self: Pin<&mut Self>, l: u32, r: u32, s: u32) -> i32 {
        unsafe { xp_native_ft_kern(self.get_unchecked_mut().slot.get(), l, r, s) }
    }
    fn line_metrics(self: Pin<&mut Self>, s: u32, p: u16) -> (i32, i32) {
        let this = unsafe { self.get_unchecked_mut() };
        let (mut a, mut h) = (0, 0);
        if this.live {
            unsafe { xp_native_ft_line_metrics(this.slot.get(), s, p, &mut a, &mut h) }
        };
        ((a + 32) >> 6, (h + 32) >> 6)
    }
    fn ligature(self: Pin<&mut Self>, c: &[u32]) -> u32 {
        unsafe { xp_native_ft_ligature(self.get_unchecked_mut().slot.get(), c.as_ptr(), c.len()) }
    }
    fn release_gsub(self: Pin<&mut Self>) {
        unsafe { xp_native_ft_release_gsub(self.get_unchecked_mut().slot.get()) }
    }
}
#[derive(Clone, Copy)]
struct Heap;
unsafe impl Memory for Heap {
    fn allocate(self, n: usize, _: usize) -> NonNull<u8> {
        NonNull::new(unsafe { fiFontMalloc(n) }.cast()).unwrap_or_else(|| std::process::abort())
    }
    unsafe fn deallocate(self, p: NonNull<u8>, _: usize, _: usize) {
        fiFontFree(p.as_ptr().cast());
    }
    fn largest_psram(self) -> usize {
        unsafe { vector_owner_heap(0) }
    }
    fn largest_internal(self) -> usize {
        unsafe { vector_owner_heap(1) }
    }
    fn free_internal(self) -> usize {
        unsafe { vector_owner_heap(2) }
    }
}
struct Evict;
impl Pressure for Evict {
    fn free_default(&mut self) -> usize {
        unsafe { vector_owner_pressure(0) }
    }
    fn render(&mut self) -> usize {
        unsafe { vector_owner_pressure(1) }
    }
    fn sd(&mut self) -> usize {
        unsafe { vector_owner_pressure(2) }
    }
    fn vector(&mut self) -> usize {
        unsafe { vector_owner_pressure(3) }
    }
}
type Owned = Family<'static, Native, Heap>;
// Test-only ABI: the harness keeps all source bytes and file objects live through drop.
#[no_mangle]
unsafe extern "C" fn vector_owner_new(sources: *const SourceView) -> *mut c_void {
    let sources = core::array::from_fn(|i| {
        let s = &*sources.add(i);
        match s.mode {
            1 => Source::Resident(core::slice::from_raw_parts(s.data, s.length as usize)),
            2 => Source::Stream {
                file: NonNull::new(s.file).unwrap(),
                size: s.size,
                prefix: core::slice::from_raw_parts(s.data, s.length as usize),
            },
            _ => Source::Missing,
        }
    });
    Box::into_raw(Box::new(Owned::new(sources, Heap))).cast()
}
unsafe fn owner<'a>(p: *mut c_void) -> Pin<&'a mut Owned> {
    Pin::new_unchecked(&mut *p.cast())
}
#[no_mangle]
unsafe extern "C" fn vector_owner_drop(p: *mut c_void) {
    drop(Box::from_raw(p.cast::<Owned>()));
}
#[no_mangle]
unsafe extern "C" fn vector_owner_load(
    p: *mut c_void,
    points: u16,
    two: bool,
    cap: usize,
    max: u16,
) -> bool {
    owner(p).load(points, two, cap, max)
}
#[no_mangle]
unsafe extern "C" fn vector_owner_clear(p: *mut c_void, resident: bool) {
    owner(p).clear(resident);
}
#[no_mangle]
unsafe extern "C" fn vector_owner_glyph(
    p: *mut c_void,
    style: u8,
    cp: u32,
    g: *mut Glyph,
    bytes: *mut u8,
    capacity: usize,
) -> i32 {
    let mut f = owner(p);
    let Some(loan) = f.as_mut().glyph(style as usize, cp, &mut Evict) else {
        return -1;
    };
    assert!(loan.bitmap.len() <= capacity);
    g.write(*loan.glyph);
    core::ptr::copy_nonoverlapping(loan.bitmap.as_ptr(), bytes, loan.bitmap.len());
    loan.bitmap.len() as i32
}
#[no_mangle]
unsafe extern "C" fn vector_owner_kern(p: *mut c_void, style: u8, l: u32, r: u32) -> i8 {
    owner(p).kern(style as usize, l, r)
}
#[no_mangle]
unsafe extern "C" fn vector_owner_metrics(
    p: *mut c_void,
    style: u8,
    a: *mut i32,
    d: *mut i32,
    h: *mut u8,
) {
    let m = owner(p).metrics(style as usize);
    a.write(m.ascent);
    d.write(m.descent);
    h.write(m.height);
}
#[no_mangle]
unsafe extern "C" fn vector_owner_coverage(p: *mut c_void, cp: u32) -> bool {
    owner(p).coverage(cp)
}
#[no_mangle]
unsafe extern "C" fn vector_owner_prewarm(
    p: *mut c_void,
    text: *const u8,
    len: usize,
    reset: bool,
) -> bool {
    owner(p).prewarm(
        if text.is_null() {
            None
        } else {
            Some(core::slice::from_raw_parts(text, len))
        },
        reset,
        &mut Evict,
    )
}
#[no_mangle]
unsafe extern "C" fn vector_owner_ligature(p: *mut c_void, style: u8, pair: u32) -> u32 {
    owner(p)
        .ligatures(style as usize)
        .iter()
        .find(|l| l.pair == pair)
        .map_or(0, |l| l.ligature_cp)
}

mod raw {
    pub use xp_core::font::raw::*;
}
mod utf8 {
    pub use xp_core::utf8::*;
}
#[path = "../frozen/vector.rs"]
mod legacy;
#[no_mangle]
unsafe extern "C" fn vector_oracle_glyph(io: *const legacy::Services, cp: u32) -> *const Glyph {
    legacy::glyph(&*io, cp)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_kern(io: *const legacy::Services, l: u32, r: u32) -> i8 {
    legacy::kern(&*io, l, r)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_init(io: *const legacy::Services, s: *const legacy::Source) {
    legacy::init(&*io, s)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_load(
    f: *const legacy::Family,
    p: u16,
    t: bool,
    c: usize,
    m: u16,
) -> bool {
    legacy::load(&*f, p, t, c, m)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_clear(f: *const legacy::Family, r: bool) {
    legacy::clear(&*f, r)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_prewarm(
    f: *const legacy::Family,
    t: *const u8,
    n: usize,
    r: bool,
) -> bool {
    legacy::prewarm(
        &*f,
        if t.is_null() {
            None
        } else {
            Some(core::slice::from_raw_parts(t, n))
        },
        r,
    )
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_coverage(io: *const legacy::Services, cp: u32) -> bool {
    legacy::coverage(&*io, cp)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_metrics(io: *const legacy::Services, d: *mut raw::FontData) {
    legacy::metrics(&*io, d)
}
#[no_mangle]
pub extern "C" fn vector_owner_size() -> usize {
    core::mem::size_of::<Owned>()
}
