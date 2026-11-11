//! Pinned SDK FreeType storage; leaves borrow sources and never call Rust.
use core::{cell::UnsafeCell, ffi::c_void, marker::PhantomPinned, mem::MaybeUninit, pin::Pin};
use xp_core::font::vector_owned::*;
#[repr(C)]
pub struct SourceView {
    pub mode: u8,
    pub data: *const u8,
    pub length: u32,
    pub file: *mut c_void,
    pub size: u32,
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

}
// Never create Rust references into live C++ storage: FT retains an interior
// stream-context pointer, and all access goes through the physical leaves.
pub(super) struct Native {
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
