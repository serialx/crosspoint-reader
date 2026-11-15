use core::{
    mem,
    ptr::{self, NonNull},
    slice,
};

/// PSRAM-preferred storage with the existing abort-on-allocation-failure contract.
/// # Safety
/// Allocations are unique, aligned, writable and paired with deallocate. Failure
/// must abort; it must not return a dangling pointer or unwind. Calls cannot reenter.
pub unsafe trait Memory: Copy {
    fn allocate(self, bytes: usize, alignment: usize) -> NonNull<u8>;
    /// # Safety
    /// Pointer and layout identify a live allocation from this allocator.
    unsafe fn deallocate(self, pointer: NonNull<u8>, bytes: usize, alignment: usize);
    fn largest_psram(self) -> usize;
    fn largest_internal(self) -> usize;
    fn free_internal(self) -> usize;
}

pub(super) struct Buffer<T: Copy, M: Memory> {
    pointer: NonNull<T>,
    length: usize,
    capacity: usize,
    memory: M,
}
impl<T: Copy, M: Memory> Buffer<T, M> {
    pub fn new(memory: M) -> Self {
        Self {
            pointer: NonNull::dangling(),
            length: 0,
            capacity: 0,
            memory,
        }
    }
    pub fn values(&self) -> &[T] {
        unsafe { slice::from_raw_parts(self.pointer.as_ptr(), self.length) }
    }
    pub fn values_mut(&mut self) -> &mut [T] {
        unsafe { slice::from_raw_parts_mut(self.pointer.as_ptr(), self.length) }
    }
    pub fn clear(&mut self) {
        self.length = 0;
    }
    pub fn reserve(&mut self, need: usize, step: usize, ceiling: usize) -> bool {
        if self.capacity >= need {
            return true;
        }
        let count = need
            .div_ceil(step)
            .saturating_mul(step)
            .min(ceiling)
            .max(need);
        let Some(bytes) = count
            .checked_mul(mem::size_of::<T>())
            .filter(|n| *n <= isize::MAX as usize)
        else {
            return false;
        };
        let m = self.memory;
        if m.largest_psram() < bytes
            && (m.largest_internal() < bytes || m.free_internal() < bytes.saturating_add(12 * 1024))
        {
            return false;
        }
        let next = m.allocate(bytes, mem::align_of::<T>()).cast::<T>();
        unsafe {
            ptr::copy_nonoverlapping(self.pointer.as_ptr(), next.as_ptr(), self.length);
        }
        self.free_storage();
        self.pointer = next;
        self.capacity = count;
        true
    }
    pub fn resize(&mut self, count: usize, value: T) {
        assert!(count <= self.capacity);
        for i in self.length..count {
            unsafe {
                self.pointer.as_ptr().add(i).write(value);
            }
        }
        self.length = count;
    }
    pub fn insert(&mut self, position: usize, value: T) {
        assert!(position <= self.length && self.length < self.capacity);
        unsafe {
            let p = self.pointer.as_ptr().add(position);
            ptr::copy(p, p.add(1), self.length - position);
            p.write(value);
        }
        self.length += 1;
    }
    fn free_storage(&mut self) {
        if self.capacity != 0 {
            unsafe {
                self.memory.deallocate(
                    self.pointer.cast(),
                    self.capacity * mem::size_of::<T>(),
                    mem::align_of::<T>(),
                );
            }
        }
    }
    pub fn release(&mut self) {
        self.free_storage();
        self.pointer = NonNull::dangling();
        self.length = 0;
        self.capacity = 0;
    }
}
impl<T: Copy, M: Memory> Drop for Buffer<T, M> {
    fn drop(&mut self) {
        self.free_storage();
    }
}
