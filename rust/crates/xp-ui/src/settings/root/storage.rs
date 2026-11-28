use super::Memory;
use crate::scene::Command;
use core::{alloc::Layout, marker::PhantomData, ptr};

pub(super) struct Commands<M: Memory> {
    pointer: *mut Command,
    capacity: u16,
    memory: PhantomData<M>,
}
impl<M: Memory> Commands<M> {
    pub fn new() -> Self {
        Self {
            pointer: ptr::null_mut(),
            capacity: 0,
            memory: PhantomData,
        }
    }
    pub fn present(&self) -> bool {
        !self.pointer.is_null()
    }
    pub fn capacity(&self) -> u16 {
        self.capacity
    }
    fn release(&mut self) {
        if self.present() {
            unsafe {
                M::release(
                    self.pointer.cast(),
                    Layout::array::<Command>(self.capacity.into()).unwrap(),
                )
            };
            self.pointer = ptr::null_mut();
        }
    }
    pub fn reserve(&mut self, count: u16) -> bool {
        if self.present() && count <= self.capacity {
            return true;
        }
        if count > 128 {
            return false;
        }
        self.release();
        self.capacity = count;
        self.pointer = M::allocate(Layout::array::<Command>(count.into()).unwrap()).cast();
        self.present()
    }
    pub fn slice(&mut self) -> &mut [Command] {
        assert!(self.present());
        unsafe { core::slice::from_raw_parts_mut(self.pointer, self.capacity.into()) }
    }
    pub fn read(&self) -> &[Command] {
        assert!(self.present());
        unsafe { core::slice::from_raw_parts(self.pointer, self.capacity.into()) }
    }
    pub fn clear(&mut self) {
        self.release();
        self.capacity = 0;
    }
}
impl<M: Memory> Drop for Commands<M> {
    fn drop(&mut self) {
        self.release();
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::{
        cell::{Cell, RefCell},
        vec::Vec,
    };
    std::thread_local! {
        static FAIL: Cell<bool> = const { Cell::new(false) };
        static TRACE: RefCell<Vec<(bool, usize)>> = const { RefCell::new(Vec::new()) };
    }
    struct Heap;
    unsafe impl Memory for Heap {
        fn allocate(layout: Layout) -> *mut u8 {
            TRACE.with(|t| t.borrow_mut().push((true, layout.size())));
            if FAIL.with(|f| f.replace(false)) {
                ptr::null_mut()
            } else {
                unsafe { std::alloc::alloc_zeroed(layout) }
            }
        }
        unsafe fn release(pointer: *mut u8, layout: Layout) {
            TRACE.with(|t| t.borrow_mut().push((false, layout.size())));
            unsafe { std::alloc::dealloc(pointer, layout) };
        }
    }
    #[test]
    fn every_growth_failure_releases_first_and_retries_the_remembered_count() {
        for failure in [32, 48, 64, 80, 96, 112, 128] {
            TRACE.with(|t| t.borrow_mut().clear());
            let mut storage = Commands::<Heap>::new();
            let mut expected = Vec::new();
            for count in (32..=128).step_by(16) {
                if count > 32 {
                    expected.push((false, usize::from(count - 16) * 32));
                }
                expected.push((true, usize::from(count) * 32));
                FAIL.with(|f| f.set(count == failure));
                assert_eq!(storage.reserve(count), count != failure);
                assert_eq!(storage.capacity(), count);
                if count == failure {
                    assert!(!storage.present());
                    assert!(storage.reserve(storage.capacity()));
                    expected.push((true, usize::from(count) * 32));
                }
            }
            let pointer = storage.pointer;
            assert!(!storage.reserve(144));
            assert_eq!(storage.pointer, pointer);
            assert_eq!(storage.capacity(), 128);
            storage.clear();
            expected.push((false, 4096));
            TRACE.with(|t| assert_eq!(*t.borrow(), expected));
        }
    }
}
