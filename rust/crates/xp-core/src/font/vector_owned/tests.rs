extern crate std;
use super::*;
use std::{
    alloc::{alloc, dealloc, Layout},
    boxed::Box,
    cell::Cell,
    vec::Vec,
};

struct Heap {
    live: Cell<usize>,
    psram: Cell<usize>,
    internal: Cell<usize>,
    free: Cell<usize>,
}
impl Default for Heap {
    fn default() -> Self {
        Self {
            live: Cell::new(0),
            psram: Cell::new(usize::MAX),
            internal: Cell::new(0),
            free: Cell::new(0),
        }
    }
}
unsafe impl Memory for &Heap {
    fn allocate(self, bytes: usize, alignment: usize) -> NonNull<u8> {
        self.live.set(self.live.get() + bytes);
        NonNull::new(unsafe { alloc(Layout::from_size_align(bytes, alignment).unwrap()) }).unwrap()
    }
    unsafe fn deallocate(self, p: NonNull<u8>, bytes: usize, alignment: usize) {
        self.live.set(self.live.get() - bytes);
        dealloc(
            p.as_ptr(),
            Layout::from_size_align(bytes, alignment).unwrap(),
        );
    }
    fn largest_psram(self) -> usize {
        self.psram.get()
    }
    fn largest_internal(self) -> usize {
        self.internal.get()
    }
    fn free_internal(self) -> usize {
        self.free.get()
    }
}
#[derive(Default)]
struct Fake {
    _pin: PhantomPinned,
    address: Option<NonNull<Self>>,
    pixels: [u8; 8],
    live: bool,
    opens: usize,
    rasters: usize,
    config: Option<Config>,
    source: Option<NonNull<u8>>,
}
impl Fake {
    fn check(&self) {
        if let Some(p) = self.address {
            assert_eq!(p.as_ptr().cast_const(), self as *const Self);
            assert_eq!(unsafe { p.as_ref() }.live, self.live);
        }
    }
}
impl Drop for Fake {
    fn drop(&mut self) {
        self.check();
    }
}
unsafe impl Rasterizer for Fake {
    unsafe fn open(self: Pin<&mut Self>, source: Source<'_>, config: Config) -> bool {
        let f = self.get_unchecked_mut();
        f.check();
        f.address = Some(NonNull::from(&mut *f));
        f.opens += 1;
        f.config = Some(config);
        f.live = matches!(source,Source::Resident(b) if b==b"font");
        f.source = if let Source::Resident(bytes) = source {
            NonNull::new(bytes.as_ptr().cast_mut())
        } else {
            None
        };
        f.live
    }
    fn close(self: Pin<&mut Self>) {
        let f = unsafe { self.get_unchecked_mut() };
        f.check();
        f.live = false;
    }
    fn glyph_id(self: Pin<&mut Self>, cp: u32) -> u32 {
        self.check();
        if self.live && cp < 128 {
            cp
        } else {
            0
        }
    }
    fn raster(self: Pin<&mut Self>, _: u32, _: u32) -> Raster<'_> {
        let f = unsafe { self.get_unchecked_mut() };
        f.check();
        f.rasters += 1;
        f.pixels = [0, 63, 64, 127, 128, 191, 192, 255];
        Raster {
            pixels: &f.pixels,
            width: 4,
            height: 2,
            left: -2,
            yoff: -3,
            advance: 8,
            metrics: Some(129),
        }
    }
    fn kern(self: Pin<&mut Self>, _: u32, _: u32, _: u32) -> i32 {
        self.check();
        -7
    }
    fn line_metrics(self: Pin<&mut Self>, _: u32, _: u16) -> (i32, i32) {
        self.check();
        (10, 14)
    }
    fn ligature(self: Pin<&mut Self>, c: &[u32]) -> u32 {
        self.check();
        if c == [102, 105] {
            12
        } else {
            0
        }
    }
    fn release_gsub(self: Pin<&mut Self>) {
        self.check();
    }
}
#[derive(Default)]
struct Evict {
    order: Vec<u8>,
    free: usize,
    freed: [usize; 3],
}
impl Pressure for Evict {
    fn free_default(&mut self) -> usize {
        self.free
    }
    fn render(&mut self) -> usize {
        self.order.push(1);
        self.freed[0]
    }
    fn sd(&mut self) -> usize {
        self.order.push(2);
        self.freed[1]
    }
    fn vector(&mut self) -> usize {
        self.order.push(3);
        self.freed[2]
    }
}
fn family(heap: &Heap) -> Pin<Box<Family<'_, Fake, &Heap>>> {
    Box::pin(unsafe {
        Family::new(
            [
                Source::Resident(b"font"),
                Source::Missing,
                Source::Missing,
                Source::Missing,
            ],
            heap,
        )
    })
}
#[test]
fn cache_loans_quantization_and_pinned_faces() {
    let heap = Heap::default();
    let mut f = family(&heap);
    let mut e = Evict::default();
    assert!(f.as_mut().load(14, true, 4096, 3));
    assert_eq!(f.faces[0].config().size_px, 29);
    for style in 0..4 {
        let mut pin = f.as_mut();
        let loan = pin.as_mut().glyph(style, 65, &mut e).unwrap();
        assert_eq!(loan.bitmap, [0x05, 0xaf]);
        assert_eq!(loan.glyph.advance_x, 32);
        assert_eq!(loan.glyph.left, -2);
        assert_eq!(loan.glyph.top, 3);
    }
    let generation = f.generation();
    f.as_mut().glyph(0, 65, &mut e).unwrap();
    assert_eq!(f.generation(), generation);
    assert_eq!(f.faces[0].ft.rasters, 1);
    assert_eq!(f.faces[1].config().weight, 700);
    assert!(f.faces[2].config().italic);
    f.as_mut().load(18, false, 4096, 3);
    assert_eq!(f.as_mut().glyph(0, 65, &mut e).unwrap().bitmap, [0x0f]);
    drop(f);
    assert_eq!(heap.live.get(), 0);
}
impl<M: Memory> Face<Fake, M> {
    fn config(&self) -> Config {
        self.ft.config.unwrap()
    }
}
#[test]
fn flush_retains_then_release_frees_and_reopens_lazy_styles() {
    let heap = Heap::default();
    let mut f = family(&heap);
    let mut e = Evict::default();
    f.as_mut().load(14, true, 4096, 2);
    for cp in [65, 66, 67] {
        f.as_mut().glyph(1, cp, &mut e).unwrap();
    }
    assert_eq!(f.faces[1].cps.values(), [67]);
    let allocated = heap.live.get();
    f.as_mut().clear(false);
    assert_eq!(heap.live.get(), allocated);
    assert!(f.faces[1].glyphs.values().is_empty());
    f.as_mut().clear(true);
    assert_eq!(heap.live.get(), 0);
    assert!(!f.faces[1].ft.live);
    assert!(f.faces[0].ft.live);
    assert!(f.as_mut().coverage(65));
    f.as_mut().glyph(1, 65, &mut e).unwrap();
    assert_eq!(f.faces[1].ft.opens, 2);
}
#[test]
fn tiny_arena_yields_metrics_and_preflight_failure_yields_missing() {
    let heap = Heap::default();
    let mut f = family(&heap);
    let mut e = Evict::default();
    f.as_mut().load(14, true, 1, 3);
    let mut pin = f.as_mut();
    let loan = pin.as_mut().glyph(0, 65, &mut e).unwrap();
    assert!(loan.bitmap.is_empty());
    assert_eq!(loan.glyph.advance_x, 32);
    assert_eq!(loan.glyph.width, 0);
    f.as_mut().clear(true);
    heap.psram.set(0);
    assert!(f.as_mut().glyph(0, 66, &mut e).is_none());
    assert_eq!(e.order, [1, 2, 3]);
    assert_eq!(heap.live.get(), 0);
}
#[test]
fn allocation_preflight_preserves_internal_reserve() {
    let heap = Heap::default();
    heap.psram.set(0);
    heap.internal.set(64);
    heap.free.set(12 * 1024 + 63);
    let mut buffer = Buffer::<u8, _>::new(&heap);
    assert!(!buffer.reserve(1, 64, 64));
    heap.free.set(12 * 1024 + 64);
    assert!(buffer.reserve(1, 64, 64));
    buffer.insert(0, 7);
    heap.psram.set(4096);
    assert!(buffer.reserve(65, 64, 128));
    assert_eq!(buffer.values(), [7]);
    buffer.release();
    assert_eq!(heap.live.get(), 0);
}
#[test]
fn pressure_order_and_early_exit_are_fixed() {
    for (free, freed, expected) in [
        (16384, [0, 0, 0], &[][..]),
        (0, [16384, 0, 0], &[1]),
        (0, [1024, 15360, 0], &[1, 2]),
        (0, [0, 0, 0], &[1, 2, 3]),
    ] {
        let mut e = Evict {
            free,
            freed,
            ..Evict::default()
        };
        reclaim(&mut e);
        assert_eq!(e.order, expected);
    }
}
#[test]
fn missing_sources_failed_open_and_borrowed_sources() {
    let heap = Heap::default();
    let source = std::vec::Vec::from(&b"font"[..]);
    let mut missing = Box::pin(unsafe { Family::<Fake, _>::new([Source::Missing; 4], &heap) });
    assert!(!missing.as_mut().load(14, true, 4096, 3));
    let mut bad = Box::pin(unsafe { Family::<Fake, _>::new([Source::Resident(b"bad"); 4], &heap) });
    assert!(!bad.as_mut().load(14, true, 4096, 3));
    let mut a = Box::pin(unsafe { Family::<Fake, _>::new([Source::Resident(&source); 4], &heap) });
    let mut b = Box::pin(unsafe { Family::<Fake, _>::new([Source::Resident(&source); 4], &heap) });
    assert!(a.as_mut().load(14, true, 4096, 3));
    assert!(b.as_mut().load(72, true, 4096, 3));
    assert_eq!(a.faces[0].ft.source, b.faces[0].ft.source);
    drop(a);
    assert!(b.as_mut().coverage(65));
}
#[test]
fn prewarm_ligatures_kerning_and_reload() {
    let heap = Heap::default();
    let mut f = family(&heap);
    let mut e = Evict::default();
    assert!(!f.as_mut().prewarm(Some(b"ab"), true, &mut e));
    f.as_mut().load(14, true, 4096, 3);
    assert!(f.as_mut().coverage(0xfb01));
    assert!(!f.as_mut().coverage(0x10ffff));
    assert_eq!({ f.ligatures(0)[0].ligature_cp }, 0xfb01);
    assert_eq!(f.metrics(0).descent, 4);
    assert!(f.as_mut().prewarm(Some(b"ab"), false, &mut e));
    assert_eq!(f.faces[0].glyphs.values().len(), 2);
    assert!(!f.as_mut().prewarm(None, true, &mut e));
    assert!(f.faces[0].glyphs.values().is_empty());
    assert_eq!(f.as_mut().kern(0, 65, 86), -2);
    assert_eq!(f.as_mut().kern(0, 65, 86), -2);
    assert_eq!(f.faces[0].kern_keys.values().len(), 1);
    let generation = f.generation();
    f.as_mut().load(18, true, 4096, 3);
    assert!(f.generation() > generation);
    assert!(f.faces[0].kern_keys.values().is_empty());
}

#[test]
fn pressure_releases_another_owner_during_the_raster_loan() {
    struct Other<'a, 'b> {
        family: Pin<&'a mut Family<'b, Fake, &'b Heap>>,
        heap: &'b Heap,
    }
    impl Pressure for Other<'_, '_> {
        fn free_default(&mut self) -> usize {
            0
        }
        fn render(&mut self) -> usize {
            0
        }
        fn sd(&mut self) -> usize {
            0
        }
        fn vector(&mut self) -> usize {
            self.family.as_mut().clear(true);
            self.heap.psram.set(200 * 1024);
            200 * 1024
        }
    }
    let heap = Heap::default();
    let mut active = family(&heap);
    let mut other = family(&heap);
    active.as_mut().load(14, true, 4096, 3);
    other.as_mut().load(18, true, 4096, 3);
    other.as_mut().glyph(1, 65, &mut Evict::default()).unwrap();
    heap.psram.set(0);
    let mut pressure = Other {
        family: other.as_mut(),
        heap: &heap,
    };
    assert_eq!(
        active.as_mut().glyph(0, 65, &mut pressure).unwrap().bitmap,
        [0x05, 0xaf]
    );
    assert!(!other.faces[1].ft.live);
    assert!(active.faces[0].ft.live);
    assert_eq!(active.faces[0].ft.rasters, 1);
}

#[test]
fn kern_cache_flushes_at_512_and_zero_limit_still_admits_one_glyph() {
    let heap = Heap::default();
    let mut f = family(&heap);
    f.as_mut().load(14, true, 4096, 0);
    for left in 1..=513 {
        assert_eq!(f.as_mut().kern(0, left, 86), -2);
    }
    assert_eq!(f.faces[0].kern_keys.values().len(), 1);
    for cp in [65, 66] {
        f.as_mut().glyph(0, cp, &mut Evict::default()).unwrap();
    }
    assert_eq!(f.faces[0].cps.values(), [66]);
    assert!(f.faces[0].kern_keys.values().is_empty());
}
