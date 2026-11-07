use super::abi::*;
use core::{
    cell::{Cell, UnsafeCell},
    mem::MaybeUninit,
    ptr,
};

// At most ten native temporaries coexist in redirect resolution. These are
// call-local placement slots, not another string/body allocation.
const SLOTS: usize = 12;
pub struct Pool<'a> {
    slots: [UnsafeCell<MaybeUninit<TextStorage>>; SLOTS],
    used: Cell<u16>,
    small: usize,
    pub leaves: &'a TextLeaves,
}
pub struct Text<'a> {
    pool: &'a Pool<'a>,
    slot: usize,
    pub value: MutableText,
}
impl<'a> Pool<'a> {
    pub unsafe fn new(leaves: &'a TextLeaves) -> Self {
        let mut slot = MaybeUninit::<TextStorage>::uninit();
        let mut value = MaybeUninit::<MutableText>::uninit();
        (leaves.initialize)(slot.as_mut_ptr(), 0, value.as_mut_ptr());
        let mut value = value.assume_init();
        let small = value.capacity;
        (leaves.release)(&mut value);
        Self {
            slots: [const { UnsafeCell::new(MaybeUninit::uninit()) }; SLOTS],
            used: Cell::new(0),
            small,
            leaves,
        }
    }
    fn take(&self) -> usize {
        let slot = (!self.used.get()).trailing_zeros() as usize;
        assert!(slot < SLOTS, "HTTP native text temporary bound");
        self.used.set(self.used.get() | (1 << slot));
        slot
    }
    unsafe fn storage(&self, slot: usize) -> *mut TextStorage {
        (*self.slots[slot].get()).as_mut_ptr()
    }
    pub unsafe fn copy(&'a self, bytes: Bytes) -> Text<'a> {
        let slot = self.take();
        let mut value = MaybeUninit::<MutableText>::uninit();
        (self.leaves.initialize)(self.storage(slot), bytes.length, value.as_mut_ptr());
        let value = value.assume_init();
        if bytes.length != 0 {
            ptr::copy_nonoverlapping(bytes.data, value.data, bytes.length);
        }
        Text {
            pool: self,
            slot,
            value,
        }
    }
    pub unsafe fn empty(&'a self) -> Text<'a> {
        self.copy(Bytes::EMPTY)
    }
    pub unsafe fn concat(&'a self, a: Bytes, b: Bytes) -> Text<'a> {
        let mut result = self.empty();
        result.resize(a.length + b.length);
        if a.length != 0 {
            ptr::copy_nonoverlapping(a.data, result.value.data, a.length);
        }
        if b.length != 0 {
            ptr::copy_nonoverlapping(b.data, result.value.data.add(a.length), b.length);
        }
        result
    }
    pub unsafe fn header(
        &'a self,
        sdk: &SdkLeaves,
        client: *mut core::ffi::c_void,
        name: *const core::ffi::c_char,
    ) -> Text<'a> {
        let slot = self.take();
        let mut value = MaybeUninit::<MutableText>::uninit();
        (sdk.response_header)(client, name, self.storage(slot), value.as_mut_ptr());
        Text {
            pool: self,
            slot,
            value: value.assume_init(),
        }
    }
}
impl Drop for Pool<'_> {
    fn drop(&mut self) {
        debug_assert_eq!(self.used.get(), 0);
    }
}
impl<'a> Text<'a> {
    pub fn native(&self) -> NativeText {
        self.value.view()
    }
    pub fn bytes(&self) -> Bytes {
        self.native().bytes
    }
    pub fn slice(&self) -> &[u8] {
        unsafe { self.bytes().slice() }
    }
    pub unsafe fn resize(&mut self, length: usize) {
        (self.pool.leaves.resize)(&mut self.value, length);
    }
    pub unsafe fn append(&mut self, bytes: Bytes) {
        let old = self.value.length;
        self.resize(old + bytes.length);
        if bytes.length != 0 {
            ptr::copy_nonoverlapping(bytes.data, self.value.data.add(old), bytes.length);
        }
    }
    pub unsafe fn assign(&mut self, bytes: Bytes) {
        assign(self.pool.leaves, &mut self.value, bytes);
    }
    pub unsafe fn assign_move(&mut self, mut other: Self) {
        if other.value.capacity == self.pool.small {
            self.assign(other.bytes());
        } else {
            core::mem::swap(self, &mut other);
        }
    }
    pub unsafe fn remove_prefix(&mut self, count: usize) {
        let length = self.value.length - count;
        ptr::copy(self.value.data.add(count), self.value.data, length);
        self.resize(length);
    }
    pub unsafe fn lower(&mut self) {
        for i in 0..self.value.length {
            let p = self.value.data.add(i);
            p.write(p.read().to_ascii_lowercase());
        }
    }
}
impl Drop for Text<'_> {
    fn drop(&mut self) {
        unsafe {
            (self.pool.leaves.release)(&mut self.value);
        }
        self.pool.used.set(self.pool.used.get() & !(1 << self.slot));
    }
}
pub unsafe fn assign(leaves: &TextLeaves, out: &mut MutableText, bytes: Bytes) {
    // A shrinking self-substring can alias; keep its bytes before terminating.
    if bytes.length <= out.length {
        if bytes.length != 0 {
            ptr::copy(bytes.data, out.data, bytes.length);
        }
        (leaves.resize)(out, bytes.length);
    } else {
        (leaves.resize)(out, bytes.length);
        ptr::copy_nonoverlapping(bytes.data, out.data, bytes.length);
    }
}
pub unsafe fn initialize(leaves: &TextLeaves, out: *mut MutableText, length: usize) {
    (leaves.initialize)((*out).native_string.cast(), length, out);
}
pub unsafe fn output_copy(leaves: &TextLeaves, out: *mut MutableText, bytes: Bytes) {
    initialize(leaves, out, bytes.length);
    if bytes.length != 0 {
        ptr::copy_nonoverlapping(bytes.data, (*out).data, bytes.length);
    }
}
pub unsafe fn output_append(leaves: &TextLeaves, out: *mut MutableText, bytes: Bytes) {
    let old = (*out).length;
    (leaves.resize)(out, old + bytes.length);
    if bytes.length != 0 {
        ptr::copy_nonoverlapping(bytes.data, (*out).data.add(old), bytes.length);
    }
}
