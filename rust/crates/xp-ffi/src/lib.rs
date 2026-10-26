#![no_std]

mod chapter;
mod css;
mod hyphen;
mod image;
mod para;
mod pkg;

mod section;
mod text;
mod xml;
mod zip;

use core::alloc::{GlobalAlloc, Layout};
use core::ffi::c_void;
use core::panic::PanicInfo;

extern "C" {
    fn abort() -> !;
    fn heap_caps_malloc(size: usize, caps: u32) -> *mut c_void;
    fn heap_caps_aligned_alloc(alignment: usize, size: usize, caps: u32) -> *mut c_void;
    fn heap_caps_free(ptr: *mut c_void);
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
