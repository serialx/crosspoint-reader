//! Host-only bindings for the production FreeType adapter and owner.
use core::{ffi::c_void, pin::Pin, ptr::NonNull};
use xp_core::font::{raw::Glyph, vector_owned::*};
#[path = "../../../rust/crates/xp-ffi/src/font_vector/physical.rs"]
mod physical;
use physical::{Native, SourceView};
extern "C" {
    fn fiFontMalloc(bytes: usize) -> *mut c_void;
    fn fiFontFree(p: *mut c_void);
    fn vector_owner_heap(metric: u8) -> usize;
    fn vector_owner_pressure(sink: u8) -> usize;
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
