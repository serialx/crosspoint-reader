#![allow(dead_code)]

use core::{ffi::c_void, ptr, slice};
use std::cell::Cell;
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
            length
        }
        4 => 1,
        5 => 8,
        8 | 9 => 80_000,
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
    use xp_core::assets::image::{Config, Services};
    pub unsafe fn render_image(_: &[u8], _: Config, services: &mut dyn Services) -> u32 {
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
        assert!(OPERATIONS.with(Cell::get) >= if reject { 2 } else { 10 });
    }

    #[test]
    fn placement_address_survives_all_operations_and_drop() {
        draw(false);
    }
    #[test]
    fn early_open_failure_destroys_the_same_placement_object_once() {
        draw(true);
    }
}
