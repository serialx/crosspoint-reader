#![no_std]

mod app;
#[path = "display/source.rs"]
mod display_source;

#[cfg(feature = "display-api")]
mod display;

mod bidi;
mod ble;
#[cfg(not(target_has_atomic = "ptr"))]
mod ble_atomic;
mod ble_json;
mod ble_runtime;
mod book;
mod book_io;
mod chapter;
mod css;
mod dictionary;
mod font;
mod font_registry;
mod font_runtime;
mod font_sd;
mod gif;
mod glyph;
mod hal;
mod http_transfer;
mod hyphen;
mod i18n;
mod image;
mod inflate;
mod library;
mod page;
mod page_render;
mod para;
mod pkg;
mod png;

mod book_cache;
mod book_css;
mod book_limits;
mod book_parser;
mod book_policy;
mod book_section;
// Historical native parser/cache ABI; shipping owners call xp-core directly.
#[cfg(feature = "app-test")]
mod sdfont;
mod section;
mod section_cache;
mod settings;
mod system_items;
mod text;
mod textm;
mod web;
mod web_jobs;
mod xml;
mod xtc;
mod xtc_owner;
mod zip;

pub use xp_ui::ffi;
mod ui_home;

use core::alloc::{GlobalAlloc, Layout};
use core::ffi::c_void;
use core::panic::PanicInfo;

extern "C" {
    fn abort() -> !;
    fn heap_caps_malloc(size: usize, caps: u32) -> *mut c_void;
    fn heap_caps_aligned_alloc(alignment: usize, size: usize, caps: u32) -> *mut c_void;
    fn heap_caps_free(ptr: *mut c_void);
    fn heap_caps_realloc(ptr: *mut c_void, size: usize, caps: u32) -> *mut c_void;
}

struct InternalHeap;

// ESP-IDF esp_heap_caps.h: byte-addressable internal RAM, including on S3.
const HEAP_CAPS: u32 = (1 << 2) | (1 << 11);

unsafe impl GlobalAlloc for InternalHeap {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // The Rust owner releases this allocation through dealloc; null means OOM.
        if layout.align() > 4 {
            heap_caps_aligned_alloc(layout.align(), layout.pad_to_align().size(), HEAP_CAPS).cast()
        } else {
            heap_caps_malloc(layout.size(), HEAP_CAPS).cast()
        }
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, size: usize) -> *mut u8 {
        if layout.align() <= 4 {
            return heap_caps_realloc(ptr.cast(), size, HEAP_CAPS).cast();
        }
        let replacement = self.alloc(Layout::from_size_align_unchecked(size, layout.align()));
        if !replacement.is_null() {
            core::ptr::copy_nonoverlapping(ptr, replacement, layout.size().min(size));
            self.dealloc(ptr, layout);
        }
        replacement
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        heap_caps_free(ptr.cast());
    }
}

#[global_allocator]
static ALLOCATOR: InternalHeap = InternalHeap;

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    unsafe { abort() }
}

#[no_mangle]
pub extern "C" fn xp_abi_version() -> u32 {
    1
}

/// # Safety
/// For nonzero len, data must point to len readable bytes in one allocation,
/// unchanged during this call. len must not exceed isize::MAX. Null is allowed
/// only for zero len. The buffer is borrowed and never retained.
#[no_mangle]
pub unsafe extern "C" fn xp_fnv1a64(data: *const u8, len: usize) -> u64 {
    let bytes = if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(data, len)
    };
    xp_core::fnv1a64(bytes)
}

/// # Safety
/// The same buffer contract as xp_fnv1a64 applies.
#[no_mangle]
pub unsafe extern "C" fn xp_crc32(data: *const u8, len: usize) -> u32 {
    let bytes = if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(data, len)
    };
    xp_core::crc32(bytes)
}

/// # Safety
/// size and align must form a valid Layout: align a power of two, size no
/// larger than isize::MAX once padded. The result is null on failure, otherwise
/// size bytes owned by the caller until freed with the same layout.
#[cfg(feature = "chapter-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_test_rust_alloc(size: usize, align: usize) -> *mut u8 {
    ALLOCATOR.alloc(Layout::from_size_align(size, align).unwrap())
}
/// # Safety
/// p must be null or a live block from this allocator with layout (old, align),
/// and align must be the power of two used to obtain it. On success the first
/// min(old, size) bytes are preserved and p is released; null keeps p live.
#[cfg(feature = "chapter-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_test_rust_realloc(
    p: *mut u8,
    old: usize,
    align: usize,
    size: usize,
) -> *mut u8 {
    ALLOCATOR.realloc(p, Layout::from_size_align(old, align).unwrap(), size)
}
/// # Safety
/// p must be a live block from this allocator with layout (size, align), and
/// align the power of two used to obtain it. It is freed exactly once here.
#[cfg(feature = "chapter-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_test_rust_free(p: *mut u8, size: usize, align: usize) {
    ALLOCATOR.dealloc(p, Layout::from_size_align(size, align).unwrap());
}

mod sync;

mod ota;

mod assets;
mod dictionary_ui;
mod plugin;

mod ui_settings;
mod ui_text_settings;

mod ui_boot;

mod book_residual;

mod ui_catalog;

mod ui_koreader;
mod ui_updates;

#[cfg(feature = "app-test")]
#[path = "test_support/network_oracles.rs"]
mod legacy_network_oracles;
mod ui_network;

mod ui_reader;
