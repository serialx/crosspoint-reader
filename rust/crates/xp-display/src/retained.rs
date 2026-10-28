use core::ptr;

/// Platform allocation must return a unique, writable PSRAM block or null.
/// No fallback to internal RAM is permitted for these retained panel planes.
///
/// # Safety
/// Nonnull allocations cover the requested size and remain live until release.
pub unsafe trait Psram {
    fn allocate(&mut self, bytes: usize) -> *mut u8;
    /// # Safety
    /// Pointer came from this allocator and is no longer borrowed.
    unsafe fn release(&mut self, pointer: *mut u8);
}

pub(crate) struct Plane {
    pointer: *mut u8,
    size: usize,
}
impl Plane {
    pub const fn new(size: usize) -> Self {
        Self {
            pointer: ptr::null_mut(),
            size,
        }
    }
    #[cfg(feature = "paper-mono")]
    pub fn as_ptr(&self) -> *const u8 {
        self.pointer
    }
    pub fn allocated(&self) -> bool {
        !self.pointer.is_null()
    }
    pub fn ensure(&mut self, memory: &mut impl Psram) -> bool {
        if !self.allocated() {
            self.pointer = memory.allocate(self.size);
        }
        self.allocated()
    }
    pub fn bytes(&self) -> &[u8] {
        if self.allocated() {
            unsafe { core::slice::from_raw_parts(self.pointer, self.size) }
        } else {
            &[]
        }
    }
    pub fn bytes_mut(&mut self) -> &mut [u8] {
        if self.allocated() {
            unsafe { core::slice::from_raw_parts_mut(self.pointer, self.size) }
        } else {
            &mut []
        }
    }
    pub unsafe fn release(&mut self, memory: &mut impl Psram) {
        if self.allocated() {
            unsafe {
                memory.release(self.pointer);
            }
            self.pointer = ptr::null_mut();
        }
    }
}
