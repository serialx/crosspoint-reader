#![allow(dead_code)]

use core::{ffi::c_void, ptr, slice};
use std::cell::{Cell, RefCell};
static TEST_GUARD: std::sync::Mutex<()> = std::sync::Mutex::new(());
use xp_core::glyph::Surface;

#[path = "../../rust/crates/xp-ffi/src/gif.rs"]
mod gif;
#[path = "../../rust/crates/xp-ffi/src/image.rs"]
mod image;
#[path = "../../rust/crates/xp-ffi/src/png.rs"]
mod png;

mod direct {
    pub struct Target {
        pub glyphs: xp_core::glyph::GlyphTarget,
        pub native: *mut core::ffi::c_void,
        pub inverted: u8,
    }
}
mod images {
    include!(concat!(env!("OUT_DIR"), "/images.rs"));
}

thread_local! {
    static EXTRACT: Cell<bool> = const { Cell::new(false) };
    static FAULT: Cell<u32> = const { Cell::new(0) };
    static EVENTS: RefCell<Vec<u32>> = const { RefCell::new(Vec::new()) };
    static BYTES: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
    static CACHE_VALID: Cell<bool> = const { Cell::new(false) };
    static FAILED: Cell<bool> = const { Cell::new(false) };
    static FILES: RefCell<Vec<*mut c_void>> = const { RefCell::new(Vec::new()) };
    static DECODE_KIND: Cell<u8> = const { Cell::new(0) };
    static DROPS: Cell<usize> = const { Cell::new(0) };
    static OPERATIONS: Cell<usize> = const { Cell::new(0) };
    static REJECT_OPEN: Cell<bool> = const { Cell::new(false) };
}

unsafe fn mock_file(
    file: *mut c_void,
    operation: u32,
    _: *const u8,
    bytes: *mut u8,
    length: usize,
) -> usize {
    EVENTS.with(|events| events.borrow_mut().push(operation));
    if operation == 0 {
        FILES.with(|active| {
            let mut files = active.borrow_mut();
            assert!(!files.contains(&file));
            files.push(file);
        });
        file.cast::<usize>().add(1).write(0);
        file.cast::<usize>().add(2).write(0);
        // A native placement object can retain its own address.
        file.cast::<*mut c_void>().write(file);
        return 0;
    }
    FILES.with(|active| assert!(active.borrow().contains(&file)));
    let retained = file.cast::<*mut c_void>().read();
    assert_eq!(retained, file);
    assert_eq!(retained.cast::<*mut c_void>().read(), file);
    OPERATIONS.with(|count| count.set(count.get() + 1));
    match operation {
        1 => {
            FILES.with(|active| active.borrow_mut().retain(|p| *p != file));
            DROPS.with(|count| count.set(count.get() + 1));
            0
        }
        2 => usize::from(!REJECT_OPEN.with(Cell::get)),
        3 => {
            bytes.write_bytes(0, length);
            if CACHE_VALID.with(Cell::get) {
                bytes.write(4);
            }
            length
        }
        4 => {
            file.cast::<usize>().add(1).write(length);
            1
        }
        5 => {
            if DECODE_KIND.with(Cell::get) == 0 {
                8
            } else {
                decode_bytes().len()
            }
        }
        6 => {
            file.cast::<usize>().add(2).write(0);
            1
        }
        13 => file.cast::<usize>().add(1).read(),
        14 => {
            let offset = file.cast::<usize>().add(1).read();
            let input = decode_bytes();
            let count = length.min(input.len().saturating_sub(offset));
            ptr::copy_nonoverlapping(input.as_ptr().add(offset.min(input.len())), bytes, count);
            file.cast::<usize>().add(1).write(offset + count);
            count
        }
        15..=17 => {
            file.cast::<usize>().add(1).write(0);
            file.cast::<usize>().add(2).write(1);
            1
        }
        18 => file.cast::<usize>().add(2).read(),
        19 => 300,
        21 => 80_000,
        8 | 9 => 80_000,
        10 => usize::from(FAULT.with(Cell::get) != 10),
        1000 => {
            let written = if FAULT.with(Cell::get) == 1000 {
                length - 1
            } else {
                length
            };
            BYTES.with(|out| {
                out.borrow_mut()
                    .extend_from_slice(slice::from_raw_parts(bytes, written))
            });
            written
        }
        _ => 0,
    }
}

fn decode_bytes() -> &'static [u8] {
    if DECODE_KIND.with(Cell::get) == 1 {
        include_bytes!("fixtures/tiny.png")
    } else {
        include_bytes!("fixtures/tiny.gif")
    }
}
unsafe fn mock_decode(_: *mut c_void, _: *const c_void, _: *const u8) -> u8 {
    2
}
unsafe fn mock_extract(_: *const c_void, _: u8) -> u8 {
    0
}
unsafe fn mock_log(_: *mut c_void, _: *const c_void, _: *const u8, _: u32, _: i32, _: i32) {}

mod assets {
    pub unsafe fn xp_assets_image_failed(path: *const u8, len: usize) -> bool {
        assert_eq!(core::slice::from_raw_parts(path, len), b"image.png");
        super::FAILED.with(core::cell::Cell::get)
    }
    use xp_core::assets::image::{Config, Services};
    pub unsafe fn render_image(_: &[u8], _: Config, services: &mut dyn Services) -> u32 {
        if super::EXTRACT.with(core::cell::Cell::get) {
            services.extract();
            return 0;
        }
        if !services.open_cache(b"cache\0") {
            return 0;
        }
        let mut bytes = [0; 8];
        assert_eq!(services.read(&mut bytes), 8);
        services.seek(0);
        let size = if super::DECODE_KIND.with(core::cell::Cell::get) == 0 {
            8
        } else {
            super::decode_bytes().len()
        };
        assert_eq!(services.size(), size);
        services.close();
        assert_eq!(services.heap(), (80_000, 80_000));
        assert_eq!(services.source_size(), Some(size));
        assert_eq!(services.decode(b"cache\0"), Some(true));
        1
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn draw(reject: bool) {
        REJECT_OPEN.with(|value| value.set(reject));
        DROPS.with(|value| value.set(0));
        OPERATIONS.with(|value| value.set(0));
        let mut pixels = [0xff; 8];
        let target = direct::Target {
            glyphs: xp_core::glyph::GlyphTarget {
                buffer: pixels.as_mut_ptr(),
                buffer_len: 8,
                width: 8,
                height: 8,
                stride: 1,
                origin_y: 0,
                rows: 8,
                clip: xp_core::glyph::Clip {
                    left: 0,
                    top: 0,
                    right: 8,
                    bottom: 8,
                },
                orientation: 3,
                plane: 0,
                absolute: 0,
                strip: 0,
                scanning: 0,
            },
            native: ptr::null_mut(),
            inverted: 0,
        };
        let path = match DECODE_KIND.with(Cell::get) {
            1 => c"image.png",
            2 => c"image.gif",
            _ => c"image.jpg",
        };
        unsafe { images::paint(&target, path.to_bytes(), b"source\0", 0, 0, 4, 4, false) };
        assert!(FILES.with(|files| files.borrow().is_empty()));
        assert_eq!(
            DROPS.with(Cell::get),
            if DECODE_KIND.with(Cell::get) == 0 {
                1
            } else {
                2
            }
        );
        assert!(
            OPERATIONS.with(Cell::get)
                >= if EXTRACT.with(Cell::get) {
                    1
                } else if reject {
                    2
                } else {
                    10
                }
        );
    }

    #[test]
    fn direct_codecs_keep_both_file_cells_pinned() {
        let _guard = TEST_GUARD.lock().unwrap();
        for kind in [1, 2] {
            BYTES.with(|bytes| bytes.borrow_mut().clear());
            DECODE_KIND.with(|value| value.set(kind));
            draw(false);
            BYTES.with(|bytes| assert_eq!(&bytes.borrow()[..4], &[4, 0, 4, 0]));
        }
        DECODE_KIND.with(|value| value.set(0));
    }

    #[test]
    fn placement_address_survives_all_operations_and_drop() {
        let _guard = TEST_GUARD.lock().unwrap();
        draw(false);
    }
    #[test]
    fn early_open_failure_destroys_the_same_placement_object_once() {
        let _guard = TEST_GUARD.lock().unwrap();
        draw(true);
    }
    #[test]
    fn extraction_keeps_the_file_pinned_and_removes_only_a_failed_stream() {
        let _guard = TEST_GUARD.lock().unwrap();
        unsafe fn stream(
            owner: *mut c_void,
            source: &[u8],
            sink: xp_core::book::io::Sink,
            file: *mut c_void,
        ) -> bool {
            assert!(owner.is_null());
            assert_eq!(source, b"source");
            let payload = b"extracted image bytes";
            sink(file, payload.as_ptr(), payload.len()) != 0
        }
        for fault in [0, 10, 1000] {
            FAULT.with(|value| value.set(fault));
            EVENTS.with(|events| events.borrow_mut().clear());
            BYTES.with(|bytes| bytes.borrow_mut().clear());
            EXTRACT.with(|value| value.set(true));
            unsafe {
                images::set_extractor(ptr::null_mut(), Some(stream));
            }
            draw(false);
            unsafe {
                images::set_extractor(ptr::null_mut(), None);
            }
            let expected: &[u32] = match fault {
                0 => &[0, 7, 10, 1000, 11, 6, 1],
                10 => &[0, 7, 10, 1],
                _ => &[0, 7, 10, 1000, 11, 6, 12, 1],
            };
            EVENTS.with(|events| assert_eq!(events.borrow().as_slice(), expected));
            if fault == 0 {
                BYTES.with(|bytes| assert_eq!(&*bytes.borrow(), b"extracted image bytes"));
            }
        }
        EVENTS.with(|events| events.borrow_mut().clear());
        draw(false);
        EVENTS.with(|events| assert_eq!(events.borrow().as_slice(), &[0, 1]));
        EXTRACT.with(|value| value.set(false));
        FAULT.with(|value| value.set(0));
    }

    #[test]
    fn probe_uses_the_existing_cache_header_rules_and_failed_path_range() {
        let _guard = TEST_GUARD.lock().unwrap();
        REJECT_OPEN.with(|value| value.set(false));
        for valid in [false, true] {
            CACHE_VALID.with(|value| value.set(valid));
            assert_eq!(unsafe { images::needs_decode(b"image.png", 4, 4) }, !valid);
        }
        FAILED.with(|value| value.set(true));
        EVENTS.with(|events| events.borrow_mut().clear());
        assert!(!unsafe { images::needs_decode(b"image.png", 4, 4) });
        EVENTS.with(|events| assert!(events.borrow().is_empty()));
        FAILED.with(|value| value.set(false));
        CACHE_VALID.with(|value| value.set(false));
    }
}
