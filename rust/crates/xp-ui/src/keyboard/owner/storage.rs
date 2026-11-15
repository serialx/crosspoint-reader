//! Explicit keyboard growth; replacements coexist with old storage until success.
extern crate alloc;
use core::{alloc::Layout, marker::PhantomData, ptr, slice};

/// # Safety
/// Resource allocations are zero-filled and satisfy Layout; release matches allocate.
/// Owner allocation is zero-filled and compatible with the Rust global allocator.
pub unsafe trait Memory {
    fn allocate(layout: Layout) -> *mut u8;
    /// # Safety
    /// Pointer is a live allocation from allocate with this exact layout.
    unsafe fn release(pointer: *mut u8, layout: Layout);
    fn owner(layout: Layout) -> *mut u8 {
        unsafe { alloc::alloc::alloc_zeroed(layout) }
    }
}

pub fn next_bytes(capacity: u32, required: u32) -> u32 {
    required.max(capacity.checked_mul(2).unwrap_or(required))
}

pub struct Block<T: Copy, M: Memory> {
    pointer: *mut T,
    capacity: u32,
    memory: PhantomData<M>,
}
impl<T: Copy, M: Memory> Default for Block<T, M> {
    fn default() -> Self {
        Self {
            pointer: ptr::null_mut(),
            capacity: 0,
            memory: PhantomData,
        }
    }
}
impl<T: Copy, M: Memory> Block<T, M> {
    pub fn capacity(&self) -> u32 {
        self.capacity
    }
    pub fn as_slice(&self) -> &[T] {
        if self.capacity == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(self.pointer, self.capacity as usize) }
        }
    }
    pub fn as_mut_slice(&mut self) -> &mut [T] {
        if self.capacity == 0 {
            &mut []
        } else {
            unsafe { slice::from_raw_parts_mut(self.pointer, self.capacity as usize) }
        }
    }
    /// # Safety
    /// Zero is a valid representation of T. Used only for keyboard POD buffers.
    pub unsafe fn replace(&mut self, capacity: u32, used: u32) -> bool {
        if used > self.capacity || used > capacity {
            return false;
        }
        let Ok(layout) = Layout::array::<T>(capacity as usize) else {
            return false;
        };
        if layout.size() == 0 {
            return false;
        }
        let next = M::allocate(layout).cast::<T>();
        if next.is_null() {
            return false;
        }
        if used != 0 {
            unsafe { ptr::copy_nonoverlapping(self.pointer, next, used as usize) };
        }
        self.clear();
        self.pointer = next;
        self.capacity = capacity;
        true
    }
    pub fn clear(&mut self) {
        if self.capacity != 0 {
            unsafe {
                M::release(
                    self.pointer.cast(),
                    Layout::array::<T>(self.capacity as usize).unwrap(),
                )
            };
        }
        self.pointer = ptr::null_mut();
        self.capacity = 0;
    }
}
impl<M: Memory> Block<u8, M> {
    pub fn grow(&mut self, required: u32, used: u32) -> bool {
        required <= self.capacity
            || unsafe { self.replace(next_bytes(self.capacity, required), used) }
    }
}
impl<T: Copy, M: Memory> Drop for Block<T, M> {
    fn drop(&mut self) {
        self.clear();
    }
}
