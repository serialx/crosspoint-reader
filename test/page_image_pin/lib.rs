#![allow(dead_code)]

use core::{ffi::c_void, ptr, slice};
use std::cell::{Cell, RefCell};
static TEST_GUARD: std::sync::Mutex<()> = std::sync::Mutex::new(());
use xp_core::glyph::Surface;

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
    static FILE: Cell<*mut c_void> = const { Cell::new(ptr::null_mut()) };
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
        FILE.with(|active| assert!(active.replace(file).is_null()));
        // A native placement object can retain its own address.
        file.cast::<*mut c_void>().write(file);
        return 0;
    }
    FILE.with(|active| assert_eq!(file, active.get()));
    let retained = file.cast::<*mut c_void>().read();
    assert_eq!(retained, file);
    assert_eq!(retained.cast::<*mut c_void>().read(), file);
    OPERATIONS.with(|count| count.set(count.get() + 1));
    match operation {
        1 => {
            FILE.with(|active| active.set(ptr::null_mut()));
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
        4 => 1,
        5 => 8,
        8 | 9 => 80_000,
        10 => usize::from(FAULT.with(Cell::get) != 10),
        13 => {
            let written = if FAULT.with(Cell::get) == 13 {
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
        assert_eq!(services.size(), 8);
        services.close();
        assert_eq!(services.heap(), (80_000, 80_000));
        assert_eq!(services.source_size(), Some(8));
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
        unsafe { images::paint(&target, b"image\0", b"source\0", 0, 0, 4, 4, false) };
        assert!(FILE.with(Cell::get).is_null());
        assert_eq!(DROPS.with(Cell::get), 1);
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
        for fault in [0, 10, 13] {
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
                0 => &[0, 7, 10, 13, 11, 6, 1],
                10 => &[0, 7, 10, 1],
                _ => &[0, 7, 10, 13, 11, 6, 12, 1],
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
