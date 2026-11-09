//! Exercise the shipping adapters with two independent registry owners.
#![allow(dead_code)]
#[path = "../../xp-ffi/src/font.rs"]
mod font;
#[path = "../../xp-ffi/src/font_registry.rs"]
mod font_registry;
#[path = "../../xp-ffi/src/font_runtime.rs"]
#[allow(unexpected_cfgs)]
mod font_runtime;

use core::{
    ffi::c_void,
    ptr,
    sync::atomic::{AtomicU32, AtomicUsize, Ordering},
};
use font_registry::{xp_font_registry_init, Owner};

static ALLOCATIONS: AtomicUsize = AtomicUsize::new(0);

#[test]
fn another_registry_cannot_redirect_generation_or_a_captured_scope() {
    let generation = AtomicU32::new(10);
    let other_generation = AtomicU32::new(20);
    let mut owner = Box::<Owner>::new_uninit();
    let mut other = Box::<Owner>::new_uninit();
    let owner_ptr = owner.as_mut_ptr();
    let other_ptr = other.as_mut_ptr();
    unsafe {
        // Keep final storage raw after publication, as the native owner does.
        xp_font_registry_init(owner_ptr, &generation, ptr::null_mut());
        let captured = font_runtime::current();
        assert!(ptr::eq(captured, owner_ptr));
        font_runtime::begin(captured);
        assert_eq!(generation.load(Ordering::Relaxed), 11);
        assert!(font_runtime::scanning(captured) != 0);

        xp_font_registry_init(other_ptr, &other_generation, ptr::null_mut());
        assert!(ptr::eq(font_runtime::current(), captured));

        // Even an unbound owner invalidates only its own source generation.
        font_runtime::release(other_ptr, 0);
        assert_eq!(generation.load(Ordering::Relaxed), 11);
        assert_eq!(other_generation.load(Ordering::Relaxed), 21);
        font_runtime::release(owner_ptr, 0);
        assert_eq!(generation.load(Ordering::Relaxed), 12);
        assert_eq!(other_generation.load(Ordering::Relaxed), 21);

        // Reader storage captures this loan at begin and reuses it at finish/end.
        font_runtime::finish(captured);
        font_runtime::end(captured);
        assert_eq!(font_runtime::scanning(captured), 0);
        assert_eq!(generation.load(Ordering::Relaxed), 13);
        assert_eq!(other_generation.load(Ordering::Relaxed), 21);
        assert!(ptr::eq(font_runtime::current(), captured));
        assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), 0);
        font_runtime::reset_current();
    }
}

#[no_mangle]
unsafe extern "C" fn fiFontMalloc(_: usize) -> *mut c_void {
    ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
    ptr::null_mut()
}
#[no_mangle]
unsafe extern "C" fn fiFontFree(bytes: *mut c_void) {
    assert!(bytes.is_null());
}
#[no_mangle]
unsafe extern "C" fn esp_timer_get_time() -> i64 {
    0
}
#[no_mangle]
unsafe extern "C" fn heap_caps_get_free_size(_: u32) -> usize {
    0
}

// Empty registries cannot reach the native font/SD/logger services in this test.
macro_rules! unused_native {
    ($name:ident($($arg:ident: $kind:ty),* $(,)?) $(-> $result:ty)?) => {
        #[no_mangle]
        unsafe extern "C" fn $name($($arg: $kind),*) $(-> $result)? {
            $(let _ = $arg;)*
            unreachable!(stringify!($name))
        }
    };
}
unused_native!(xp_native_font_compat_resolve(native: *mut c_void, font: i32, style: u8) -> u8);
unused_native!(
    xp_native_font_compat_prewarm(
        native: *mut c_void,
        font: i32,
        backend: u8,
        text: *const core::ffi::c_char,
        styles: u8,
        metadata: u8,
        kern: u8,
        accumulate: u8,
    ) -> i32
);
unused_native!(
    xp_native_font_compat_clear(native: *mut c_void, font: i32, backend: u8, resident: u8)
);
unused_native!(xp_native_font_compat_reset(native: *mut c_void, font: i32));
unused_native!(
    xp_native_font_cache_diagnostic(
        font: *const xp_core::font::raw::FontData,
        event: u32,
        a: u32,
        b: u32,
        c: u32,
        d: u32,
    )
);
unused_native!(
    xp_native_sd_font_bitmap(
        font: *const xp_core::font::raw::FontData,
        glyph: *const xp_core::font::raw::Glyph,
    ) -> *const u8
);
unused_native!(
    xp_native_sd_font_view(
        native: *mut c_void,
        font: i32,
        family: *mut xp_core::textm::native::Family,
        source: *mut xp_core::glyph::BitmapSource,
    )
);
unused_native!(
    xp_native_sd_font_resident_slice(
        native: *mut c_void,
        font: i32,
        style: u8,
        text: *const u8,
        len: usize,
        metadata: u8,
    )
);
unused_native!(xp_native_sd_font_load(native: *mut c_void, font: i32) -> u8);
unused_native!(xp_native_font_diagnostic(native: *mut c_void, event: u32, value: u32));
unused_native!(xp_native_bidi_acquire() -> *mut xp_core::bidi::Workspace);
unused_native!(xp_native_bidi_release());
