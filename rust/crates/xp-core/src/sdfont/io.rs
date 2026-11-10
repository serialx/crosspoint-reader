//! Scoped physical handles. Pin the empty holder before native construction.
use core::{ffi::CStr, marker::PhantomPinned, mem::MaybeUninit, pin::Pin};

#[repr(C)]
pub struct Slot {
    words: [MaybeUninit<usize>; if cfg!(target_pointer_width = "32") {
        3
    } else {
        4
    }],
}

/// # Safety
/// Implementations use only the supplied slot, fit its size/alignment, and never
/// retain a loan or unwind. init/destroy run once per native object lifetime.
pub unsafe trait Physical {
    /// # Safety
    /// Slot is aligned, writable and contains no live native object.
    unsafe fn init(&mut self, slot: *mut Slot);
    /// # Safety
    /// Slot contains a live native object at its construction address.
    unsafe fn destroy(&mut self, slot: *mut Slot);
    /// # Safety
    /// Slot contains an initialized empty native handle.
    unsafe fn open(&mut self, slot: *mut Slot, path: &CStr) -> Option<u32>;
    /// # Safety
    /// Slot contains the current open handle; bytes are exclusively borrowed.
    unsafe fn read(&mut self, slot: *mut Slot, bytes: &mut [u8]) -> usize;
    /// # Safety
    /// Slot contains the current open handle.
    unsafe fn seek(&mut self, slot: *mut Slot, offset: u32) -> bool;
    /// # Safety
    /// Slot contains the current handle; its Impl stays owned until destroy.
    unsafe fn finish(&mut self, slot: *mut Slot);
}

pub struct Files<P: Physical> {
    slots: [Slot; 2],
    physical: P,
    initialized: bool,
    depth: u8,
    seek_failed: bool,
    _pinned: PhantomPinned,
}
impl<P: Physical> Files<P> {
    pub fn new(physical: P) -> Self {
        Self {
            slots: core::array::from_fn(|_| Slot {
                words: [MaybeUninit::uninit(); if cfg!(target_pointer_width = "32") {
                    3
                } else {
                    4
                }],
            }),
            physical,
            initialized: false,
            depth: 0,
            seek_failed: false,
            _pinned: PhantomPinned,
        }
    }
    fn initialize(self: Pin<&mut Self>) {
        // Only slot bytes are projected; neither a filled slot nor P can escape.
        let this = unsafe { self.get_unchecked_mut() };
        if !this.initialized {
            for slot in &mut this.slots {
                unsafe { this.physical.init(slot) };
            }
            this.initialized = true;
        }
    }
    pub fn open(mut self: Pin<&mut Self>, path: &CStr) -> Option<u32> {
        self.as_mut().initialize();
        let this = unsafe { self.get_unchecked_mut() };
        if this.depth == 2 {
            return None;
        }
        let slot = &mut this.slots[usize::from(this.depth)];
        let size = unsafe { this.physical.open(slot, path) };
        if size.is_some() {
            this.depth += 1;
        } else {
            unsafe {
                this.physical.destroy(slot);
                this.physical.init(slot);
            }
        }
        size
    }
    pub fn close(self: Pin<&mut Self>, release: bool) {
        let this = unsafe { self.get_unchecked_mut() };
        if this.depth == 0 {
            return;
        }
        let slot = &mut this.slots[usize::from(this.depth - 1)];
        unsafe {
            if release {
                this.depth -= 1;
                this.physical.destroy(slot);
                this.physical.init(slot);
            } else {
                this.physical.finish(slot);
            }
        }
    }
    pub fn read(self: Pin<&mut Self>, bytes: &mut [u8]) -> usize {
        let this = unsafe { self.get_unchecked_mut() };
        this.seek_failed = false;
        if this.depth == 0 {
            return 0;
        }
        unsafe {
            this.physical
                .read(&mut this.slots[usize::from(this.depth - 1)], bytes)
        }
    }
    pub fn seek(self: Pin<&mut Self>, offset: u32) -> bool {
        let this = unsafe { self.get_unchecked_mut() };
        this.seek_failed = this.depth == 0
            || !unsafe {
                this.physical
                    .seek(&mut this.slots[usize::from(this.depth - 1)], offset)
            };
        !this.seek_failed
    }
    pub fn seek_failed(&self) -> bool {
        self.seek_failed
    }
    pub fn reader(mut self: Pin<&mut Self>, path: &CStr) -> Option<Reader<'_, P>> {
        let size = self.as_mut().open(path)?;
        Some(Reader { files: self, size })
    }
}
impl<P: Physical> Drop for Files<P> {
    fn drop(&mut self) {
        if self.initialized {
            for slot in self.slots.iter_mut().rev() {
                unsafe { self.physical.destroy(slot) };
            }
        }
    }
}
pub struct Reader<'a, P: Physical> {
    files: Pin<&'a mut Files<P>>,
    pub size: u32,
}
impl<P: Physical> super::parser::Reader for Reader<'_, P> {
    fn seek(&mut self, offset: u32) -> bool {
        self.files.as_mut().seek(offset)
    }
    fn read(&mut self, bytes: &mut [u8]) -> usize {
        self.files.as_mut().read(bytes)
    }
}
impl<P: Physical> Drop for Reader<'_, P> {
    fn drop(&mut self) {
        self.files.as_mut().close(true);
    }
}
