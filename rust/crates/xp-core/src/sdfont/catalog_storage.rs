//! Internal-heap containers matching the native small-string and vector requests.
use super::parser::Allocator;
use core::{ffi::CStr, mem, ptr, slice};

union Text {
    inline: [u8; 16],
    heap: *mut u8,
}
#[repr(C)]
pub(super) struct Name {
    text: Text,
    length: usize,
    capacity: usize,
}
impl Name {
    pub const fn new() -> Self {
        Self {
            text: Text { inline: [0; 16] },
            length: 0,
            capacity: 15,
        }
    }
    fn pointer(&self) -> *const u8 {
        unsafe {
            if self.capacity == 15 {
                self.text.inline.as_ptr()
            } else {
                self.text.heap
            }
        }
    }
    pub fn bytes(&self) -> &[u8] {
        unsafe { slice::from_raw_parts(self.pointer(), self.length) }
    }
    pub fn cstr(&self) -> &CStr {
        unsafe { CStr::from_ptr(self.pointer().cast()) }
    }
    pub fn assign(&mut self, bytes: &[u8], memory: &Allocator) -> bool {
        if bytes.len() > self.capacity {
            let Some(capacity) = self.capacity.checked_mul(2).map(|n| n.max(bytes.len())) else {
                return false;
            };
            let Some(request) = capacity.checked_add(1) else {
                return false;
            };
            let data = unsafe { (memory.styles)(request) };
            if data.is_null() {
                return false;
            }
            unsafe {
                ptr::copy_nonoverlapping(bytes.as_ptr(), data, bytes.len());
                data.add(bytes.len()).write(0);
                if self.capacity != 15 {
                    (memory.free_styles)(self.text.heap, self.capacity + 1);
                }
            }
            self.text = Text { heap: data };
            self.capacity = capacity;
        } else {
            let data = unsafe {
                if self.capacity == 15 {
                    self.text.inline.as_mut_ptr()
                } else {
                    self.text.heap
                }
            };
            unsafe {
                ptr::copy_nonoverlapping(bytes.as_ptr(), data, bytes.len());
                data.add(bytes.len()).write(0);
            }
        }
        self.length = bytes.len();
        true
    }
    pub fn release(&mut self, memory: &Allocator) {
        if self.capacity != 15 {
            unsafe { (memory.free_styles)(self.text.heap, self.capacity + 1) }
        }
        *self = Self::new();
    }
}

#[repr(C)]
pub(super) struct Array<T> {
    data: *mut T,
    length: usize,
    capacity: usize,
}
impl<T> Array<T> {
    pub const fn new() -> Self {
        Self {
            data: ptr::null_mut(),
            length: 0,
            capacity: 0,
        }
    }
    pub fn values(&self) -> &[T] {
        if self.length == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(self.data, self.length) }
        }
    }
    pub fn values_mut(&mut self) -> &mut [T] {
        if self.length == 0 {
            &mut []
        } else {
            unsafe { slice::from_raw_parts_mut(self.data, self.length) }
        }
    }
    fn resize(&mut self, capacity: usize, memory: &Allocator) -> bool {
        if capacity == self.capacity {
            return true;
        }
        let Some(bytes) = capacity.checked_mul(mem::size_of::<T>()) else {
            return false;
        };
        let data = if bytes == 0 {
            ptr::null_mut()
        } else {
            unsafe { (memory.styles)(bytes).cast::<T>() }
        };
        if bytes != 0 && data.is_null() {
            return false;
        }
        unsafe {
            if self.length != 0 {
                ptr::copy_nonoverlapping(self.data, data, self.length);
            }
            if !self.data.is_null() {
                (memory.free_styles)(self.data.cast(), self.capacity * mem::size_of::<T>());
            }
        }
        self.data = data;
        self.capacity = capacity;
        true
    }
    pub fn capacity(&self) -> usize {
        self.capacity
    }
    pub fn reserve_exact(&mut self, capacity: usize, memory: &Allocator) -> bool {
        capacity <= self.capacity || self.resize(capacity, memory)
    }
    pub fn push(&mut self, value: T, memory: &Allocator) -> Result<(), T> {
        if self.length == self.capacity {
            let Some(capacity) =
                self.capacity
                    .max(1)
                    .checked_mul(if self.capacity == 0 { 1 } else { 2 })
            else {
                return Err(value);
            };
            if !self.resize(capacity, memory) {
                return Err(value);
            }
        }
        unsafe { self.data.add(self.length).write(value) };
        self.length += 1;
        Ok(())
    }
    pub fn remove(&mut self, index: usize) -> T {
        assert!(index < self.length);
        unsafe {
            let value = self.data.add(index).read();
            ptr::copy(
                self.data.add(index + 1),
                self.data.add(index),
                self.length - index - 1,
            );
            self.length -= 1;
            value
        }
    }
    pub fn clear(&mut self) {
        self.truncate(0);
    }
    pub fn truncate(&mut self, length: usize) {
        assert!(length <= self.length);
        for index in length..self.length {
            unsafe { ptr::drop_in_place(self.data.add(index)) }
        }
        self.length = length;
    }
    pub fn shrink(&mut self, memory: &Allocator) -> bool {
        self.resize(self.length, memory)
    }
    pub fn release(&mut self, memory: &Allocator) {
        self.clear();
        let _ = self.resize(0, memory);
    }
}

const _: () = {
    assert!(mem::size_of::<Array<u8>>() == 3 * mem::size_of::<usize>());
    assert!(!cfg!(target_pointer_width = "32") || mem::size_of::<Name>() == 24);
};
