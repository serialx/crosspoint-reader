use super::*;
use crate::font::raw::{FontData, Glyph, Group};
use core::{
    ptr,
    sync::atomic::{AtomicU32, Ordering},
};

fn target(buffer: &mut [u8]) -> GlyphTarget {
    GlyphTarget {
        buffer: buffer.as_mut_ptr(),
        buffer_len: buffer.len() as u32,
        width: 32,
        height: 24,
        stride: 4,
        origin_y: 0,
        rows: 24,
        clip: Clip {
            left: 0,
            top: 0,
            right: 32,
            bottom: 24,
        },
        orientation: 3,
        plane: 0,
        absolute: 0,
        strip: 0,
        scanning: 0,
    }
}
fn draw() -> Draw {
    Draw {
        glyph: Glyph {
            width: 7,
            height: 9,
            advance_x: 16,
            left: 0,
            top: 0,
            data_length: 16,
            data_offset: 0,
        },
        x: 2,
        y: 2,
        ascender: 9,
        scale_num: 1,
        scale_den: 2,
        scale256: 384,
        kind: 0,
        two_bit: 1,
        state: 1,
        solid: 0,
    }
}
#[test]
fn truncations_and_invalid_descriptors_do_not_write() {
    let bitmap = [0xe7; 16];
    for kind in 0..4 {
        for len in 0..bitmap.len() {
            let mut buffer = [0x55; 96];
            let t = target(&mut buffer);
            let mut d = draw();
            d.kind = kind;
            assert!(!Surface::new(&t, &mut buffer)
                .unwrap()
                .draw(&bitmap[..len], &d));
            assert_eq!(buffer, [0x55; 96]);
        }
        for value in [i32::MIN, -1, 0, 4097, i32::MAX] {
            let mut buffer = [0x55; 96];
            let t = target(&mut buffer);
            let mut d = draw();
            d.kind = kind;
            if kind == 2 {
                d.scale_den = value;
            } else if kind == 3 {
                d.scale256 = value;
            } else {
                d.kind = 255;
            }
            assert!(!Surface::new(&t, &mut buffer).unwrap().draw(&bitmap, &d));
            assert_eq!(buffer, [0x55; 96]);
        }
    }
    let mut buffer = [0x55; 96];
    let mut oversized = target(&mut buffer);
    oversized.width = 65535;
    oversized.height = 65535;
    oversized.rows = 65535;
    oversized.stride = 8192;
    oversized.buffer_len = 8192 * 65535;
    assert!(!oversized.valid());
    for orientation in 4..=255 {
        let mut t = target(&mut buffer);
        t.orientation = orientation;
        assert!(!t.valid());
    }
    for plane in 3..=255 {
        let mut t = target(&mut buffer);
        t.plane = plane;
        assert!(!t.valid());
    }
    for len in 0..96 {
        let mut t = target(&mut buffer);
        t.buffer_len = len;
        assert!(!t.valid());
    }
    for v in [i32::MIN, -1, 0, i32::MAX] {
        let mut t = target(&mut buffer);
        t.width = v;
        assert!(!t.valid());
    }
}
#[test]
fn source_misses_and_generation_reject_before_dereference() {
    let generation = AtomicU32::new(1);
    let bytes = [0xff; 16];
    let glyph = draw().glyph;
    // All-zero pointers/counts/options/bool are a valid empty FontData.
    let mut font: FontData = unsafe { core::mem::zeroed() };
    font.glyph = &glyph;
    font.bitmap = bytes.as_ptr();
    font.is2_bit = true;
    let mut source = BitmapSource {
        generation: &generation,
        captured_generation: 1,
        font: &font,
        glyph_count: 1,
        bitmap_len: 16,
        slots: ptr::null(),
        slot_count: 0,
        overflow_first: ptr::null(),
        overflow_stride: 0,
        overflow_count: 0,
    };
    unsafe extern "C" fn vector_miss(_: *mut core::ffi::c_void, _: *const Glyph) -> *const u8 {
        panic!("The bitmap view must not invoke vector callbacks");
    }
    font.vector_bitmap_handler = Some(vector_miss);
    assert!(unsafe { source.resolve(ptr::dangling()) }.is_none());
    font.vector_bitmap_handler = None;
    let resolved = unsafe { source.resolve(&glyph) }.unwrap();
    assert_eq!(resolved.data, bytes.as_ptr());
    assert_eq!(resolved.len, 16);
    for len in 0..16 {
        source.bitmap_len = len;
        assert!(unsafe { source.resolve(&glyph) }.is_none());
    }
    source.bitmap_len = 16;
    source.overflow_first = &glyph;
    source.overflow_stride = 32;
    source.overflow_count = 1;
    assert!(unsafe { source.resolve(&glyph) }.is_none());
    source.overflow_count = 0;
    let group = Group {
        compressed_offset: 0,
        compressed_size: 0,
        uncompressed_size: 16,
        glyph_count: 1,
        first_glyph_index: 0,
    };
    font.groups = &group;
    font.group_count = 1;
    assert!(unsafe { source.resolve(&glyph) }.is_none());
    let mut entries = [PageEntry {
        glyph_index: 0,
        buffer_offset: 0,
        aligned_offset: 0,
    }];
    let mut slots = [
        PageSlot {
            buffer: bytes.as_ptr(),
            font: &font,
            entries: entries.as_ptr(),
            count: 1,
        },
        PageSlot {
            buffer: bytes.as_ptr(),
            font: &font,
            entries: entries.as_ptr(),
            count: 1,
        },
    ];
    source.slots = slots.as_ptr();
    source.slot_count = 2;
    assert_eq!(
        unsafe { source.resolve(&glyph) }.unwrap().data,
        bytes.as_ptr()
    );
    entries[0].buffer_offset = u32::MAX;
    assert!(unsafe { source.resolve(&glyph) }.is_none());
    entries[0].buffer_offset = 0;
    slots[1].entries = entries.as_ptr();
    // The first nonempty matching slot wins, even if later slots have the glyph.
    let missing = [PageEntry {
        glyph_index: 1,
        buffer_offset: 0,
        aligned_offset: 0,
    }];
    slots[0].entries = missing.as_ptr();
    assert!(unsafe { source.resolve(&glyph) }.is_none());
    slots[0].count = 0;
    source.slots = slots.as_ptr();
    assert!(unsafe { source.resolve(&glyph) }.is_some());
    generation.store(2, Ordering::Relaxed);
    source.font = ptr::dangling();
    source.slots = ptr::dangling();
    assert!(unsafe { source.resolve(ptr::dangling()) }.is_none());
}
