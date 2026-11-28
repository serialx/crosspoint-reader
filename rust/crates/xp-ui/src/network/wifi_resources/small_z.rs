use super::{Error, Global, Memory};
use core::{alloc::Layout, marker::PhantomData, ptr, slice};

const INLINE: usize = 15;
const HEAP: usize = 1 << (usize::BITS - 1);
#[derive(Clone, Copy)]
struct Heap {
    pointer: *mut u8,
    capacity: usize,
}
#[repr(C)]
union Payload {
    inline: [u8; INLINE + 1],
    heap: Heap,
}
/// Owned bytes with a trailing NUL. Embedded NUL bytes remain part of the value.
#[repr(C)]
pub struct SmallZ<M: Memory = Global> {
    payload: Payload,
    length_heap: usize,
    memory: PhantomData<M>,
}
impl<M: Memory> SmallZ<M> {
    pub const fn new() -> Self {
        Self {
            payload: Payload {
                inline: [0; INLINE + 1],
            },
            length_heap: 0,
            memory: PhantomData,
        }
    }
    pub fn len(&self) -> usize {
        self.length_heap & !HEAP
    }
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
    fn heap(&self) -> bool {
        self.length_heap & HEAP != 0
    }
    pub fn capacity(&self) -> usize {
        if self.heap() {
            unsafe { self.payload.heap.capacity - 1 }
        } else {
            INLINE
        }
    }
    fn pointer(&self) -> *const u8 {
        if self.heap() {
            unsafe { self.payload.heap.pointer }
        } else {
            unsafe { self.payload.inline.as_ptr() }
        }
    }
    fn pointer_mut(&mut self) -> *mut u8 {
        if self.heap() {
            unsafe { self.payload.heap.pointer }
        } else {
            unsafe { self.payload.inline.as_mut_ptr() }
        }
    }
    pub fn as_bytes(&self) -> &[u8] {
        unsafe { slice::from_raw_parts(self.pointer(), self.len()) }
    }
    pub fn as_z(&self) -> &[u8] {
        unsafe { slice::from_raw_parts(self.pointer(), self.len() + 1) }
    }
    pub fn try_reserve(&mut self, minimum: usize) -> Result<(), Error> {
        if minimum <= self.capacity() {
            return Ok(());
        }
        let bytes = minimum.checked_add(1).ok_or(Error::Capacity)?;
        let layout = Layout::array::<u8>(bytes).map_err(|_| Error::Capacity)?;
        let pointer = M::allocate(layout);
        if pointer.is_null() {
            return Err(Error::Allocation(bytes));
        }
        unsafe { ptr::copy_nonoverlapping(self.pointer(), pointer, self.len() + 1) };
        if self.heap() {
            let old = unsafe { self.payload.heap };
            // The capacity was accepted by Layout when this allocation was made.
            unsafe {
                M::release(
                    old.pointer,
                    Layout::from_size_align_unchecked(old.capacity, 1),
                )
            };
        }
        self.payload.heap = Heap {
            pointer,
            capacity: bytes,
        };
        self.length_heap |= HEAP;
        Ok(())
    }
    pub fn try_assign(&mut self, bytes: &[u8]) -> Result<(), Error> {
        self.try_reserve(bytes.len())?;
        self.write_joined(&[bytes])
    }
    pub fn try_clone(&self) -> Result<Self, Error> {
        let mut out = Self::new();
        out.try_assign(self.as_bytes())?;
        Ok(out)
    }
    /// Write only into prepared capacity; this function never calls the allocator.
    pub fn write_joined(&mut self, parts: &[&[u8]]) -> Result<(), Error> {
        let length = parts
            .iter()
            .try_fold(0usize, |n, part| n.checked_add(part.len()))
            .filter(|n| *n <= self.capacity())
            .ok_or(Error::Capacity)?;
        let out = self.pointer_mut();
        let mut offset = 0;
        for part in parts {
            unsafe { ptr::copy_nonoverlapping(part.as_ptr(), out.add(offset), part.len()) };
            offset += part.len();
        }
        unsafe { out.add(length).write(0) };
        self.length_heap = (self.length_heap & HEAP) | length;
        Ok(())
    }
    pub fn clear(&mut self) {
        unsafe { self.pointer_mut().write(0) };
        self.length_heap &= HEAP;
    }
}
impl<M: Memory> Default for SmallZ<M> {
    fn default() -> Self {
        Self::new()
    }
}
impl<M: Memory> Drop for SmallZ<M> {
    fn drop(&mut self) {
        if self.heap() {
            let heap = unsafe { self.payload.heap };
            unsafe {
                M::release(
                    heap.pointer,
                    Layout::from_size_align_unchecked(heap.capacity, 1),
                )
            };
        }
    }
}
#[cfg(target_pointer_width = "32")]
const _: () = assert!(core::mem::size_of::<SmallZ>() == 20);
