use super::{Error, Global, Memory};
use core::{
    alloc::Layout,
    marker::PhantomData,
    mem,
    ptr::{self, NonNull},
    slice,
};

#[repr(C)]
pub struct Buffer<T, M: Memory = Global> {
    pointer: NonNull<T>,
    length: usize,
    capacity: usize,
    memory: PhantomData<M>,
}
impl<T, M: Memory> Buffer<T, M> {
    pub const fn new() -> Self {
        Self {
            pointer: NonNull::dangling(),
            length: 0,
            capacity: 0,
            memory: PhantomData,
        }
    }
    pub fn capacity(&self) -> usize {
        self.capacity
    }
    pub fn as_slice(&self) -> &[T] {
        unsafe { slice::from_raw_parts(self.pointer.as_ptr(), self.length) }
    }
    pub fn as_mut_slice(&mut self) -> &mut [T] {
        unsafe { slice::from_raw_parts_mut(self.pointer.as_ptr(), self.length) }
    }
    pub fn try_reserve(&mut self, minimum: usize) -> Result<(), Error> {
        if minimum <= self.capacity {
            return Ok(());
        }
        if mem::size_of::<T>() == 0 {
            return Err(Error::Capacity);
        }
        let layout = Layout::array::<T>(minimum).map_err(|_| Error::Capacity)?;
        let Some(pointer) = NonNull::new(M::allocate(layout).cast::<T>()) else {
            return Err(Error::Allocation(layout.size()));
        };
        // Move initialized elements; the old allocation must not run their drops.
        unsafe {
            ptr::copy_nonoverlapping(self.pointer.as_ptr(), pointer.as_ptr(), self.length);
            self.release_storage();
        }
        self.pointer = pointer;
        self.capacity = minimum;
        Ok(())
    }
    pub fn try_push(&mut self, value: T) -> Result<(), Error> {
        if self.length == self.capacity {
            let count = if self.capacity == 0 {
                1
            } else {
                self.capacity.checked_mul(2).ok_or(Error::Capacity)?
            };
            self.try_reserve(count)?;
        }
        unsafe { self.pointer.as_ptr().add(self.length).write(value) };
        self.length += 1;
        Ok(())
    }
    pub fn truncate(&mut self, length: usize) {
        if length < self.length {
            let old = self.length;
            self.length = length;
            unsafe {
                ptr::drop_in_place(ptr::slice_from_raw_parts_mut(
                    self.pointer.as_ptr().add(length),
                    old - length,
                ))
            };
        }
    }
    pub fn clear(&mut self) {
        self.truncate(0);
    }
    unsafe fn release_storage(&mut self) {
        if self.capacity != 0 {
            // Only a successful Layout::array result can set this capacity.
            unsafe {
                M::release(
                    self.pointer.as_ptr().cast(),
                    Layout::from_size_align_unchecked(
                        self.capacity * mem::size_of::<T>(),
                        mem::align_of::<T>(),
                    ),
                )
            };
        }
    }
}
impl<T, M: Memory> Default for Buffer<T, M> {
    fn default() -> Self {
        Self::new()
    }
}
impl<T, M: Memory> Drop for Buffer<T, M> {
    fn drop(&mut self) {
        self.clear();
        unsafe { self.release_storage() };
    }
}
