//! Default-platform allocation for the buffers that previously used C++ new[].
use core::{
    alloc::Layout,
    ops::{Deref, DerefMut},
    ptr, slice,
};

#[cfg(not(test))]
const DEFAULT: u32 = 3;

#[cfg(not(test))]
extern "C" {
    fn xp_assets_platform_alloc(bytes: usize, caps: u32) -> *mut u8;
    fn xp_assets_platform_realloc(data: *mut u8, bytes: usize, caps: u32) -> *mut u8;
    fn xp_assets_platform_free(data: *mut u8);
}

/// # Safety
/// The pointer is null or owned by the asset platform allocator.
pub(super) unsafe fn free(data: *mut u8) {
    #[cfg(not(test))]
    unsafe {
        xp_assets_platform_free(data)
    };
    #[cfg(test)]
    unsafe {
        libc_free(data.cast())
    };
}

// Standalone core tests do not link the firmware/host platform leaves.
#[cfg(test)]
extern "C" {
    #[link_name = "malloc"]
    fn libc_malloc(bytes: usize) -> *mut core::ffi::c_void;
    #[link_name = "realloc"]
    fn libc_realloc(data: *mut core::ffi::c_void, bytes: usize) -> *mut core::ffi::c_void;
    #[link_name = "free"]
    fn libc_free(data: *mut core::ffi::c_void);
}

/// # Safety
/// Implementors accept all-zero bits and have no destructor.
pub(super) unsafe trait Zeroed: Copy {}
unsafe impl Zeroed for u8 {}
unsafe impl Zeroed for u32 {}
unsafe impl Zeroed for i16 {}

pub(super) struct Buffer<T: Zeroed> {
    pub(super) data: *mut T,
    pub(super) len: usize,
}
impl<T: Zeroed> Buffer<T> {
    pub(super) const fn new() -> Self {
        Self {
            data: ptr::null_mut(),
            len: 0,
        }
    }
    pub(super) fn grow(&mut self, len: usize) -> bool {
        if len <= self.len {
            return true;
        }
        let Ok(layout) = Layout::array::<T>(len) else {
            return false;
        };
        // Only byte/u32/i16 owners use this interface; malloc supplies their alignment.
        let data = unsafe {
            #[cfg(not(test))]
            {
                if self.data.is_null() {
                    xp_assets_platform_alloc(layout.size(), DEFAULT)
                } else {
                    xp_assets_platform_realloc(self.data.cast(), layout.size(), DEFAULT)
                }
            }
            #[cfg(test)]
            {
                if self.data.is_null() {
                    libc_malloc(layout.size()).cast::<u8>()
                } else {
                    libc_realloc(self.data.cast(), layout.size()).cast::<u8>()
                }
            }
        }
        .cast::<T>();
        if data.is_null() {
            return false;
        }
        // SAFETY: growth preserves the old initialized prefix; Zeroed accepts zero bits.
        unsafe { ptr::write_bytes(data.add(self.len), 0, len - self.len) };
        self.data = data;
        self.len = len;
        true
    }
    pub(super) fn bytes(&self) -> &[T] {
        self
    }
    pub(super) fn bytes_mut(&mut self) -> &mut [T] {
        self
    }
}
impl<T: Zeroed> Deref for Buffer<T> {
    type Target = [T];
    fn deref(&self) -> &[T] {
        if self.len == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(self.data, self.len) }
        }
    }
}
impl<T: Zeroed> DerefMut for Buffer<T> {
    fn deref_mut(&mut self) -> &mut [T] {
        if self.len == 0 {
            &mut []
        } else {
            unsafe { slice::from_raw_parts_mut(self.data, self.len) }
        }
    }
}
impl<T: Zeroed> Drop for Buffer<T> {
    fn drop(&mut self) {
        if !self.data.is_null() {
            unsafe { free(self.data.cast()) };
        }
    }
}
