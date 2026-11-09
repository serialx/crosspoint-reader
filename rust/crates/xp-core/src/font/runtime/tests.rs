extern crate std;
use crate::font::{
    loaded::{Entry, Registry},
    raw::{FontData, Glyph, Group, Interval},
    runtime::{Backend, ClearKind, FontCacheOwner, Physical, Prewarm, SdFontCacheCompat},
};
use core::{
    ffi::{c_void, CStr},
    mem::MaybeUninit,
    ptr,
    sync::atomic::AtomicU32,
};
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
};
use std::{boxed::Box, string::String, vec::Vec};

#[derive(Debug, PartialEq)]
enum Event {
    Alloc(usize),
    Free(usize),
    Generation,
    Log(u32),
}
#[derive(Default)]
struct Heap {
    live: Vec<(*mut u8, Layout)>,
    events: Vec<Event>,
    attempts: usize,
    fail: usize,
}
std::thread_local! { static HEAP: RefCell<Heap> = RefCell::default(); }
unsafe extern "C" fn allocate(n: usize) -> *mut u8 {
    HEAP.with(|h| {
        let mut h = h.borrow_mut();
        h.attempts += 1;
        h.events.push(Event::Alloc(n));
        if h.attempts == h.fail {
            return ptr::null_mut();
        }
        let layout = Layout::from_size_align(n.max(1), 8).unwrap();
        let p = alloc(layout);
        if !p.is_null() {
            h.live.push((p, layout));
        }
        p
    })
}
unsafe extern "C" fn release(p: *mut u8) {
    if p.is_null() {
        return;
    }
    HEAP.with(|h| {
        let mut h = h.borrow_mut();
        let i = h.live.iter().position(|e| e.0 == p).unwrap();
        let (_, layout) = h.live.remove(i);
        h.events.push(Event::Free(layout.size()));
        dealloc(p, layout);
    })
}
unsafe extern "C" fn clock(_: bool) -> u32 {
    0
}
unsafe extern "C" fn generation() {
    HEAP.with(|h| h.borrow_mut().events.push(Event::Generation));
}
unsafe extern "C" fn diagnostic(
    _: *mut c_void,
    _: *const FontData,
    e: u32,
    _: u32,
    _: u32,
    _: u32,
    _: u32,
) {
    HEAP.with(|h| h.borrow_mut().events.push(Event::Log(e)));
}
fn physical() -> Physical {
    Physical {
        context: ptr::null_mut(),
        alloc: allocate,
        free: release,
        clock,
        generation,
        diagnostic: Some(diagnostic),
    }
}
fn owner() -> Box<FontCacheOwner> {
    let mut value = Box::<FontCacheOwner>::new_uninit();
    unsafe {
        FontCacheOwner::initialize(value.as_mut_ptr());
        value.assume_init()
    }
}
fn reset(fail: usize) {
    HEAP.with(|h| {
        assert!(h.borrow().live.is_empty());
        *h.borrow_mut() = Heap {
            fail,
            ..Heap::default()
        };
    });
}
fn data() -> FontData {
    unsafe { MaybeUninit::zeroed().assume_init() }
}
const BYTES: [u8; 9] = [1, 4, 0, 251, 255, 0x18, 0xe4, 0x55, 0xaa];
const GLYPHS: [Glyph; 2] = [
    Glyph {
        width: 3,
        height: 2,
        advance_x: 3,
        left: 0,
        top: 2,
        data_length: 2,
        data_offset: 0,
    },
    Glyph {
        width: 4,
        height: 2,
        advance_x: 4,
        left: 0,
        top: 2,
        data_length: 2,
        data_offset: 2,
    },
];
const GROUP: Group = Group {
    compressed_offset: 0,
    compressed_size: 9,
    uncompressed_size: 4,
    glyph_count: 2,
    first_glyph_index: 0,
};
const INTERVAL: Interval = Interval {
    first: 65,
    last: 66,
    offset: 0,
};
fn compressed() -> FontData {
    FontData {
        bitmap: BYTES.as_ptr(),
        glyph: GLYPHS.as_ptr(),
        intervals: &INTERVAL,
        interval_count: 1,
        is2_bit: true,
        groups: &GROUP,
        group_count: 1,
        ..data()
    }
}
fn registry(generation: &AtomicU32) -> Registry {
    let mut r = MaybeUninit::uninit();
    unsafe {
        Registry::initialize(r.as_mut_ptr(), generation);
        r.assume_init()
    }
}
fn register(r: &mut Registry, id: i32, slot: &*const FontData) {
    assert!(r.insert(Entry {
        id,
        slots: [slot, ptr::null(), ptr::null(), ptr::null()],
        fallback: 0
    }));
}
#[derive(Debug, PartialEq)]
enum SdEvent {
    Clear(i32, ClearKind),
    Reset(i32),
    Prewarm(i32, Vec<u8>, u8, bool, bool, bool),
    Resolve(i32, u8),
}
#[derive(Default)]
struct Sd(Vec<SdEvent>, Vec<(i32, Backend)>);
impl SdFontCacheCompat for Sd {
    unsafe fn resolve_style(&mut self, id: i32, style: u8) -> u8 {
        self.0.push(SdEvent::Resolve(id, style));
        if style == 1 {
            0
        } else {
            style
        }
    }
    unsafe fn prewarm(&mut self, id: i32, backend: Backend, p: Prewarm<'_>) -> i32 {
        self.1.push((id, backend));
        self.0.push(SdEvent::Prewarm(
            id,
            p.text.to_bytes().to_vec(),
            p.styles,
            p.metadata_only,
            p.load_kern_lig,
            p.accumulate,
        ));
        0
    }
    unsafe fn clear(&mut self, id: i32, backend: Backend, kind: ClearKind) {
        self.1.push((id, backend));
        self.0.push(SdEvent::Clear(id, kind));
    }
    unsafe fn reset_stats(&mut self, id: i32) {
        self.0.push(SdEvent::Reset(id));
    }
}
fn request(text: &CStr, styles: u8, accumulate: bool) -> Prewarm<'_> {
    Prewarm {
        text,
        styles,
        metadata_only: false,
        load_kern_lig: true,
        accumulate,
    }
}

#[test]
fn direct_decoder_preserves_glyph_bytes_and_allocation_order() {
    reset(0);
    let font = compressed();
    let slot = &font as *const _;
    let epoch = AtomicU32::new(0);
    let mut r = registry(&epoch);
    register(&mut r, 7, &slot);
    let o = owner();
    let io = physical();
    let mut sd = Sd::default();
    unsafe {
        assert_eq!(o.prewarm(&r, 7, request(c"AB", 1, true), &io, &mut sd), 0);
        assert_eq!(
            core::slice::from_raw_parts(o.bitmap(&font, &GLYPHS[0], 0, &io), 2),
            [0x1b, 0x90]
        );
        assert_eq!(
            core::slice::from_raw_parts(o.bitmap(&font, &GLYPHS[1], 1, &io), 2),
            [0x55, 0xaa]
        );
        assert_eq!(o.cache().stats.hits, 2);
        o.clear(&r, ClearKind::Frame, &io, &mut sd);
    }
    HEAP.with(|h| {
        let h = h.borrow();
        assert!(h.live.is_empty());
        assert_eq!(
            h.events,
            [
                Event::Generation,
                Event::Alloc(4),
                Event::Alloc(24),
                Event::Alloc(4),
                Event::Free(4),
                Event::Log(10),
                Event::Generation,
                Event::Free(4),
                Event::Free(24)
            ]
        );
    });
}

#[test]
fn failures_keep_the_existing_free_order_and_recover() {
    for fail in 1..=3 {
        reset(fail);
        let font = compressed();
        let slot = &font as *const _;
        let epoch = AtomicU32::new(0);
        let mut r = registry(&epoch);
        register(&mut r, 7, &slot);
        let o = owner();
        let io = physical();
        let mut sd = Sd::default();
        unsafe {
            assert_eq!(
                o.prewarm(&r, 7, request(c"AB", 1, true), &io, &mut sd),
                if fail <= 2 { 2 } else { 1 }
            );
            o.clear(&r, ClearKind::Frame, &io, &mut sd);
            assert_eq!(o.prewarm(&r, 7, request(c"AB", 1, true), &io, &mut sd), 0);
            assert_eq!(
                core::slice::from_raw_parts(o.bitmap(&font, &GLYPHS[0], 0, &io), 2),
                [0x1b, 0x90]
            );
            o.clear(&r, ClearKind::Resident, &io, &mut sd);
        }
        HEAP.with(|h| assert!(h.borrow().live.is_empty()));
    }
}

#[test]
fn hot_fallback_grows_free_first_and_does_not_retain_source_loans() {
    reset(0);
    let font = compressed();
    let o = owner();
    let io = physical();
    unsafe {
        assert_eq!(
            core::slice::from_raw_parts(o.bitmap(&font, &GLYPHS[0], 0, &io), 2),
            [0x1b, 0x90]
        );
        assert_eq!(
            core::slice::from_raw_parts(o.bitmap(&font, &GLYPHS[1], 1, &io), 2),
            [0x55, 0xaa]
        );
        assert_eq!(o.cache().stats.misses, 1);
        let stored = [1, 8, 0, 247, 255, 1, 2, 3, 4, 5, 6, 7, 8];
        let group = Group {
            compressed_size: 13,
            uncompressed_size: 8,
            glyph_count: 1,
            ..GROUP
        };
        let glyph = Glyph {
            width: 4,
            height: 8,
            data_length: 8,
            ..GLYPHS[0]
        };
        let larger = FontData {
            bitmap: stored.as_ptr(),
            groups: &group,
            glyph: &glyph,
            ..font
        };
        assert_eq!(
            core::slice::from_raw_parts(o.bitmap(&larger, &glyph, 0, &io), 8),
            [1, 2, 3, 4, 5, 6, 7, 8]
        );
        HEAP.with(|h| {
            assert_eq!(
                h.borrow().events,
                [
                    Event::Alloc(4),
                    Event::Alloc(2),
                    Event::Free(4),
                    Event::Alloc(8),
                    Event::Free(2),
                    Event::Alloc(8)
                ]
            )
        });
        let epoch = AtomicU32::new(0);
        o.clear(&registry(&epoch), ClearKind::Frame, &io, &mut Sd::default());
    }
    HEAP.with(|h| assert!(h.borrow().live.is_empty()));
}

#[test]
fn corrupt_or_short_group_reports_failure_without_exposing_uninitialized_bytes() {
    for stored in [&[7u8][..], &BYTES[..BYTES.len() - 1]] {
        reset(0);
        let group = Group {
            compressed_size: stored.len() as u32,
            ..GROUP
        };
        let font = FontData {
            bitmap: stored.as_ptr(),
            groups: &group,
            ..compressed()
        };
        let slot = &font as *const _;
        let epoch = AtomicU32::new(0);
        let mut r = registry(&epoch);
        register(&mut r, 7, &slot);
        let o = owner();
        let io = physical();
        let mut sd = Sd::default();
        unsafe {
            assert_eq!(o.prewarm(&r, 7, request(c"AB", 1, true), &io, &mut sd), 1);
            assert!(o.bitmap(&font, &GLYPHS[0], 0, &io).is_null());
            o.clear(&r, ClearKind::Frame, &io, &mut sd);
        }
        HEAP.with(|h| assert!(h.borrow().live.is_empty()));
    }
}

#[test]
fn scan_batches_resolve_aliases_small_caps_and_preserve_descending_group_order() {
    reset(0);
    let marker = 0u8;
    let font = FontData {
        glyph_miss_ctx: ptr::from_ref(&marker).cast_mut().cast(),
        ..data()
    };
    let slot = &font as *const _;
    let epoch = AtomicU32::new(0);
    let mut r = registry(&epoch);
    register(&mut r, -17, &slot);
    register(&mut r, 23, &slot);
    let o = owner();
    let io = physical();
    let mut sd = Sd::default();
    unsafe {
        o.begin(&r, &io, &mut sd);
        assert!(o.scanning());
        o.record(&r, -17, b"a", 128, &io, &mut sd);
        o.record(&r, -17, b"B", 1, &io, &mut sd);
        o.record_resolved(23, "é中😀é".as_bytes(), 0, &io);
        o.finish(&r, &io, &mut sd);
        assert!(!o.scanning());
        o.finish(&r, &io, &mut sd);
        o.end(&r, &io, &mut sd);
    }
    assert_eq!(
        sd.0,
        [
            SdEvent::Clear(-17, ClearKind::Frame),
            SdEvent::Clear(23, ClearKind::Frame),
            SdEvent::Reset(-17),
            SdEvent::Reset(23),
            SdEvent::Resolve(-17, 0),
            SdEvent::Resolve(-17, 1),
            SdEvent::Prewarm(23, "é中😀".as_bytes().to_vec(), 1, false, true, false),
            SdEvent::Prewarm(-17, b"AB".to_vec(), 1, false, true, false),
            SdEvent::Clear(-17, ClearKind::Frame),
            SdEvent::Clear(23, ClearKind::Frame)
        ]
    );
    HEAP.with(|h| {
        assert!(!h
            .borrow()
            .events
            .iter()
            .any(|e| matches!(e, Event::Alloc(_))))
    });
}

#[test]
fn scan_cap_warns_once_and_preserves_512_multibyte_codepoints() {
    reset(0);
    let marker = 0u8;
    let font = FontData {
        glyph_miss_ctx: ptr::from_ref(&marker).cast_mut().cast(),
        ..data()
    };
    let slot = &font as *const _;
    let epoch = AtomicU32::new(0);
    let mut r = registry(&epoch);
    for id in 1..=5 {
        register(&mut r, id, &slot);
    }
    let o = owner();
    let io = physical();
    let mut sd = Sd::default();
    let text: String = (0x1000..0x1201)
        .map(|v| char::from_u32(v).unwrap())
        .collect();
    unsafe {
        o.begin(&r, &io, &mut sd);
        o.record_resolved(1, text.as_bytes(), 0, &io);
        o.record_resolved(1, b"Z", 0, &io);
        for id in 2..=5 {
            o.record_resolved(id, b"A", 0, &io);
        }
        o.finish(&r, &io, &mut sd);
    }
    let calls: Vec<_> =
        sd.0.iter()
            .filter_map(|e| {
                if let SdEvent::Prewarm(id, text, ..) = e {
                    Some((*id, text))
                } else {
                    None
                }
            })
            .collect();
    assert_eq!(calls.len(), 1);
    assert_eq!(calls[0].0, 1);
    assert_eq!(calls[0].1.len(), 512 * 3);
    HEAP.with(|h| {
        assert_eq!(
            h.borrow()
                .events
                .iter()
                .filter(|e| **e == Event::Log(11))
                .count(),
            1
        )
    });
}

#[test]
fn fifth_scan_font_is_ignored_without_hiding_the_first_four() {
    reset(0);
    let marker = 0u8;
    let font = FontData {
        glyph_miss_ctx: ptr::from_ref(&marker).cast_mut().cast(),
        ..data()
    };
    let slot = &font as *const _;
    let epoch = AtomicU32::new(0);
    let mut r = registry(&epoch);
    for id in 1..=5 {
        register(&mut r, id, &slot);
    }
    let o = owner();
    let io = physical();
    let mut sd = Sd::default();
    unsafe {
        o.begin(&r, &io, &mut sd);
        for id in 1..=5 {
            o.record_resolved(id, b"A", 0, &io);
        }
        o.finish(&r, &io, &mut sd);
    }
    let ids: Vec<_> =
        sd.0.iter()
            .filter_map(|e| {
                if let SdEvent::Prewarm(id, ..) = e {
                    Some(*id)
                } else {
                    None
                }
            })
            .collect();
    assert_eq!(ids, [4, 3, 2, 1]);
}

#[test]
fn builtin_aliases_share_a_batch_and_prewarm_high_styles_first() {
    reset(0);
    let regular = compressed();
    let bold = compressed();
    let regular_slot = &regular as *const _;
    let bold_slot = &bold as *const _;
    let epoch = AtomicU32::new(0);
    let mut r = registry(&epoch);
    assert!(r.insert(Entry {
        id: 7,
        slots: [&regular_slot, &bold_slot, &regular_slot, ptr::null()],
        fallback: 0
    }));
    let o = owner();
    let io = physical();
    let mut sd = Sd::default();
    unsafe {
        o.begin(&r, &io, &mut sd);
        for (text, style) in [(b"A", 0), (b"B", 1), (b"B", 2), (b"A", 3)] {
            o.record(&r, 7, text, style, &io, &mut sd);
        }
        o.finish(&r, &io, &mut sd);
        assert_eq!(o.cache().count, 2);
        assert_eq!(o.cache().slots[0].font, bold_slot);
        assert_eq!(o.cache().slots[1].font, regular_slot);
        assert_eq!(o.cache().stats.page_bytes, 8);
        assert_eq!(o.cache().stats.entry_bytes, 48);
        o.end(&r, &io, &mut sd);
    }
    assert!(sd.0.is_empty());
    HEAP.with(|h| assert!(h.borrow().live.is_empty()));
}

#[test]
fn null_style_cells_keep_their_requested_style_and_scan_capacity() {
    reset(0);
    let missing = ptr::null();
    let marker = 0u8;
    let sd_font = FontData {
        glyph_miss_ctx: ptr::from_ref(&marker).cast_mut().cast(),
        ..data()
    };
    let sd_slot = &sd_font as *const _;
    let epoch = AtomicU32::new(0);
    let mut r = registry(&epoch);
    register(&mut r, 1, &missing);
    register(&mut r, 2, &sd_slot);
    let text: String = (0x1000..0x1100)
        .map(|v| char::from_u32(v).unwrap())
        .collect();
    let o = owner();
    let io = physical();
    let mut sd = Sd::default();
    unsafe {
        o.begin(&r, &io, &mut sd);
        o.record(&r, 1, text.as_bytes(), 0, &io, &mut sd);
        o.record(&r, 1, text.as_bytes(), 1, &io, &mut sd);
        o.record(&r, 2, b"A", 0, &io, &mut sd);
        o.finish(&r, &io, &mut sd);
    }
    assert!(!sd.0.iter().any(|e| matches!(e, SdEvent::Prewarm(..))));
}

#[test]
fn early_end_finishes_scan_before_release_and_keeps_resident_release_distinct() {
    reset(0);
    let marker = 0u8;
    let font = FontData {
        glyph_miss_ctx: ptr::from_ref(&marker).cast_mut().cast(),
        ..data()
    };
    let slot = &font as *const _;
    let epoch = AtomicU32::new(0);
    let mut r = registry(&epoch);
    register(&mut r, 7, &slot);
    let o = owner();
    let io = physical();
    let mut sd = Sd::default();
    unsafe {
        o.begin(&r, &io, &mut sd);
        o.record_resolved(7, b"A", 0, &io);
        o.end(&r, &io, &mut sd);
        o.clear(&r, ClearKind::Resident, &io, &mut sd);
    }
    assert_eq!(
        sd.0,
        [
            SdEvent::Clear(7, ClearKind::Frame),
            SdEvent::Reset(7),
            SdEvent::Prewarm(7, b"A".to_vec(), 1, false, true, false),
            SdEvent::Clear(7, ClearKind::Frame),
            SdEvent::Clear(7, ClearKind::Resident)
        ]
    );
}

#[test]
fn clears_sd_before_vector_and_vector_prewarm_keeps_accumulation_choice() {
    reset(0);
    unsafe extern "C" fn bitmap(_: *mut c_void, _: *const Glyph) -> *const u8 {
        ptr::null()
    }
    let mut sd_data = data();
    sd_data.glyph_miss_ctx = ptr::without_provenance_mut(1);
    let mut vec_data = data();
    vec_data.vector_bitmap_handler = Some(bitmap);
    let sd_slot = &sd_data as *const FontData;
    let vec_slot = &vec_data as *const FontData;
    let generation = AtomicU32::new(0);
    let mut reg = registry(&generation);
    register(&mut reg, 1, &vec_slot);
    register(&mut reg, 2, &sd_slot);
    let o = owner();
    let mut bridge = Sd::default();
    let io = physical();
    unsafe {
        o.begin(&reg, &io, &mut bridge);
    }
    assert_eq!(
        bridge.0,
        [
            SdEvent::Clear(2, ClearKind::Frame),
            SdEvent::Clear(1, ClearKind::Frame),
            SdEvent::Reset(2)
        ]
    );
    bridge.0.clear();
    unsafe {
        o.prewarm(&reg, 1, request(c"A", 15, false), &io, &mut bridge);
        o.prewarm(&reg, 1, request(c"B", 1, true), &io, &mut bridge);
        o.clear(&reg, ClearKind::Resident, &io, &mut bridge);
    }
    assert_eq!(
        bridge.0,
        [
            SdEvent::Prewarm(1, b"A".to_vec(), 15, false, true, false),
            SdEvent::Prewarm(1, b"B".to_vec(), 1, false, true, true),
            SdEvent::Clear(2, ClearKind::Resident),
            SdEvent::Clear(1, ClearKind::Resident)
        ]
    );
    assert!(
        bridge.1
            == [
                (2, Backend::Sd),
                (1, Backend::Vector),
                (1, Backend::Vector),
                (1, Backend::Vector),
                (2, Backend::Sd),
                (1, Backend::Vector)
            ]
    );
    HEAP.with(|heap| {
        assert!(!heap
            .borrow()
            .events
            .iter()
            .any(|e| matches!(e, Event::Alloc(_))))
    });
}

#[test]
fn vector_pressure_can_reenter_eviction_while_the_scan_text_stays_live() {
    reset(0);
    unsafe extern "C" fn vector_bitmap(_: *mut c_void, _: *const Glyph) -> *const u8 {
        ptr::null()
    }
    let builtin = compressed();
    let mut vector = data();
    vector.vector_bitmap_handler = Some(vector_bitmap);
    let builtin_slot = &builtin as *const FontData;
    let vector_slot = &vector as *const FontData;
    let epoch = AtomicU32::new(0);
    let mut reg = registry(&epoch);
    register(&mut reg, 1, &builtin_slot);
    register(&mut reg, 2, &vector_slot);
    let o = owner();
    let io = physical();
    let mut ordinary = Sd::default();
    unsafe {
        o.begin(&reg, &io, &mut ordinary);
        o.record_resolved(2, b"A", 0, &io);
        o.record_resolved(1, b"AB", 0, &io);
    }
    struct Pressure {
        owner: *const FontCacheOwner,
        registry: *const Registry,
        calls: usize,
    }
    impl SdFontCacheCompat for Pressure {
        unsafe fn resolve_style(&mut self, _: i32, style: u8) -> u8 {
            style
        }
        unsafe fn prewarm(&mut self, font: i32, backend: Backend, request: Prewarm<'_>) -> i32 {
            assert_eq!(font, 2);
            assert!(backend == Backend::Vector);
            assert_eq!(request.text.to_bytes(), b"A");
            assert_eq!((*self.owner).cache().count, 1);
            // FreeType's pressure callback runs SDK eviction while this text is borrowed.
            (*self.owner).clear(
                self.registry,
                ClearKind::Frame,
                &physical(),
                &mut Sd::default(),
            );
            assert_eq!(request.text.to_bytes(), b"A");
            assert_eq!((*self.owner).cache().count, 0);
            self.calls += 1;
            0
        }
        unsafe fn clear(&mut self, _: i32, _: Backend, _: ClearKind) {}
        unsafe fn reset_stats(&mut self, _: i32) {}
    }
    let mut pressure = Pressure {
        owner: ptr::from_ref(&*o),
        registry: &reg,
        calls: 0,
    };
    unsafe {
        o.finish(&reg, &io, &mut pressure);
    }
    assert_eq!(pressure.calls, 1);
    HEAP.with(|h| assert!(h.borrow().live.is_empty()));
}
