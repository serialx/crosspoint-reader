extern crate alloc;
use crate::{
    runtime::next_capacity,
    scene::{Command, Interaction},
};
use core::{alloc::Layout, marker::PhantomData, ptr, slice};

/// # Safety
/// Allocations satisfy Layout and are zero-filled; release matches allocate.
/// Owner allocations must be compatible with the Rust global allocator.
pub unsafe trait Memory {
    fn allocate(layout: Layout) -> *mut u8;
    /// # Safety
    /// The pointer is live and was allocated with this exact layout.
    unsafe fn release(pointer: *mut u8, layout: Layout);
    fn owner(layout: Layout) -> *mut u8 {
        unsafe { alloc::alloc::alloc_zeroed(layout) }
    }
}
pub struct Text<M: Memory> {
    pointer: *mut u8,
    length: usize,
    memory: PhantomData<M>,
}
impl<M: Memory> Text<M> {
    pub fn copy(bytes: &[u8]) -> Option<Self> {
        if bytes.is_empty() {
            return Some(Self {
                pointer: ptr::null_mut(),
                length: 0,
                memory: PhantomData,
            });
        }
        let length = bytes.len();
        let layout = Layout::array::<u8>(length.checked_add(1)?).ok()?;
        let pointer = M::allocate(layout);
        if pointer.is_null() {
            return None;
        }
        unsafe { ptr::copy_nonoverlapping(bytes.as_ptr(), pointer, length) };
        Some(Self {
            pointer,
            length,
            memory: PhantomData,
        })
    }
    pub fn bytes(&self) -> &[u8] {
        if self.pointer.is_null() {
            &[]
        } else {
            unsafe { slice::from_raw_parts(self.pointer, self.length) }
        }
    }
    pub fn terminated(&self) -> &[u8] {
        if self.pointer.is_null() {
            b"\0"
        } else {
            unsafe { slice::from_raw_parts(self.pointer, self.length + 1) }
        }
    }
}
impl<M: Memory> Drop for Text<M> {
    fn drop(&mut self) {
        if !self.pointer.is_null() {
            unsafe { M::release(self.pointer, Layout::array::<u8>(self.length + 1).unwrap()) }
        }
    }
}

pub(super) struct Storage<M: Memory> {
    pointer: *mut u8,
    commands: u16,
    hits: u16,
    memory: PhantomData<M>,
}
impl<M: Memory> Storage<M> {
    pub(super) fn new(commands: u16, hits: u16) -> Self {
        Self {
            pointer: ptr::null_mut(),
            commands,
            hits,
            memory: PhantomData,
        }
    }
    pub(super) fn bytes(&self) -> usize {
        usize::from(self.commands) * 32 + 2 * usize::from(self.hits) * 24
    }
    fn layout(&self) -> Layout {
        Layout::from_size_align(self.bytes(), core::mem::align_of::<Command>()).unwrap()
    }
    pub(super) fn allocate(&mut self) -> bool {
        if next_capacity(0, self.commands, self.commands, self.commands) != Some(self.commands) {
            return false;
        }
        let next = M::allocate(self.layout());
        // Native unique_ptr assignment releases even if replacement failed.
        self.clear();
        self.pointer = next;
        !next.is_null()
    }
    pub(super) fn present(&self) -> bool {
        !self.pointer.is_null()
    }
    pub(super) fn clear(&mut self) {
        if self.present() {
            unsafe { M::release(self.pointer, self.layout()) };
            self.pointer = ptr::null_mut();
        }
    }
    /// Caller excludes other writers and only borrows the unpublished hit bank.
    pub(super) fn build(&self, bank: usize) -> (*mut Command, *mut Interaction) {
        (self.pointer.cast(), self.hit_pointer(bank))
    }
    fn hit_pointer(&self, bank: usize) -> *mut Interaction {
        unsafe {
            self.pointer
                .add(self.commands as usize * 32)
                .cast::<Interaction>()
                .add(bank * self.hits as usize)
        }
    }
    pub(super) unsafe fn hits(&self, bank: usize, count: usize) -> &[Interaction] {
        unsafe { slice::from_raw_parts(self.hit_pointer(bank), count) }
    }
}
impl<M: Memory> Drop for Storage<M> {
    fn drop(&mut self) {
        self.clear();
    }
}
