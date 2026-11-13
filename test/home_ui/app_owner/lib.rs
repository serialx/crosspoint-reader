// Compile the production application bridge with the host allocator and test runtime.
#[path = "../../../rust/crates/xp-ffi/src/app/mod.rs"]
pub mod app;
#[path = "../../../rust/crates/xp-ffi/src/ble.rs"]
mod ble;
#[path = "../../../rust/crates/xp-ffi/src/ble_runtime.rs"]
mod ble_runtime;
#[path = "../../../rust/crates/xp-ffi/src/book_css.rs"]
mod book_css;
#[path = "../../../rust/crates/xp-ffi/src/book_limits.rs"]
mod book_limits;
#[path = "../../../rust/crates/xp-ffi/src/book_policy.rs"]
mod book_policy;
#[path = "../../../rust/crates/xp-ffi/src/chapter.rs"]
mod chapter;
#[path = "../../../rust/crates/xp-ffi/src/css.rs"]
mod css;
#[path = "../../../rust/crates/xp-ffi/src/section_cache.rs"]
mod section_cache;
#[path = "../../../rust/crates/xp-ffi/src/settings.rs"]
mod settings;

/// # Safety
/// Nonempty input must point to `len` readable bytes for this call.
pub unsafe extern "C" fn xp_crc32(data: *const u8, len: usize) -> u32 {
    let bytes = if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(data, len)
    };
    xp_core::crc32(bytes)
}

#[path = "../../../rust/crates/xp-ffi/src/display/source.rs"]
mod display_source;
#[path = "../../../rust/crates/xp-ffi/src/page_plan/storage.rs"]
mod page_plan_storage;
mod font_runtime {
    pub(crate) unsafe fn scanning(_: *const core::ffi::c_void) -> u8 {
        panic!("owner fixture must use its scene observer");
    }
}
mod page_render {
    pub use crate::page_plan_storage::Plan;
    pub mod images {
        use core::ffi::c_void;
        type Stream = unsafe fn(*mut c_void, &[u8], xp_core::book::io::Sink, *mut c_void) -> bool;
        pub(crate) unsafe fn supported(_: &[u8]) -> bool {
            panic!("owner fixture must not probe images");
        }
        pub(crate) unsafe fn dimensions(_: &[u8], _: &mut u16, _: &mut u16) -> u8 {
            panic!("owner fixture must not probe images");
        }
        pub(crate) unsafe fn set_extractor(_: *mut c_void, _: Option<Stream>) {
            panic!("owner fixture must use its book observer");
        }
    }
    pub mod direct {
        use core::{cell::Cell, ffi::c_void};
        #[repr(C)]
        pub(crate) struct Target {
            pub glyphs: xp_core::glyph::GlyphTarget,
            pub fonts: *const c_void,
            pub native: *mut c_void,
            pub inverted: u8,
        }
        pub(crate) unsafe fn xp_native_render_target(
            _: *mut Target,
            _: *mut xp_ui::ffi::Paint,
        ) -> u8 {
            panic!("owner fixture must use its scene observer");
        }
        // Only the ownership contract is observed in this fixture.
        #[allow(dead_code)]
        pub(crate) struct Request {
            pub font: i32,
            pub x: i32,
            pub y: i32,
            pub mode: u8,
            pub plan: *mut super::Plan,
            pub bounds: *mut c_void,
            pub count: usize,
            pub prepare: u8,
        }
        type Paint = fn(&[u8]);
        thread_local! { static OBSERVER: Cell<Option<Paint>> = const { Cell::new(None) }; }
        #[cfg(feature = "app-test")]
        pub(crate) fn observe(paint: Paint) {
            OBSERVER.with(|v| v.set(Some(paint)));
        }
        pub unsafe fn paint_current(bytes: &[u8], font: i32, x: i32, y: i32, mode: u8) -> u32 {
            paint_request_current(
                bytes,
                &Request {
                    font,
                    x,
                    y,
                    mode,
                    plan: core::ptr::null_mut(),
                    bounds: core::ptr::null_mut(),
                    count: 0,
                    prepare: 0,
                },
            )
        }
        pub(crate) unsafe fn paint_request_current(bytes: &[u8], _: &Request) -> u32 {
            assert!(xp_core::page::Page::open(bytes).is_ok());
            OBSERVER.with(|v| {
                if let Some(paint) = v.get() {
                    paint(bytes)
                }
            });
            0
        }
    }
}
