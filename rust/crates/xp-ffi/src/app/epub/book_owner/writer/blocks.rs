extern crate alloc;
use crate::app::abi;
use alloc::vec::Vec;
use core::{marker::PhantomData, ptr::NonNull};
use xp_core::book_io::zip_index::Target;

pub(super) trait Element: Copy {}
impl Element for u32 {}
impl Element for Target {}

// Match the native deque's <=512-byte payloads without a whole-book allocation.
// Only the pointer map uses Rust allocation; payloads retain native malloc placement.
pub(super) struct Blocks<T: Element> {
    resources: &'static abi::Resources,
    nodes: Vec<NonNull<T>>,
    length: usize,
    marker: PhantomData<T>,
}

impl<T: Element> Blocks<T> {
    const CAPACITY: usize = 512 / core::mem::size_of::<T>();

    pub(super) fn new(resources: &'static abi::Resources) -> Self {
        Self {
            resources,
            nodes: Vec::new(),
            length: 0,
            marker: PhantomData,
        }
    }

    pub(super) fn len(&self) -> usize {
        self.length
    }

    pub(super) fn push(&mut self, value: T) -> Result<(), ()> {
        let node_index = self.length / Self::CAPACITY;
        if node_index == self.nodes.len() {
            self.nodes.try_reserve(1).map_err(|_| ())?;
            let bytes = Self::CAPACITY * core::mem::size_of::<T>();
            let node = NonNull::new(
                unsafe { (self.resources.buffer_allocate)(abi::MEMORY_DEFAULT, bytes) }.cast::<T>(),
            )
            .ok_or(())?;
            self.nodes.push(node);
        }
        unsafe {
            self.nodes[node_index]
                .as_ptr()
                .add(self.length % Self::CAPACITY)
                .write(value)
        };
        self.length += 1;
        Ok(())
    }

    pub(super) fn get(&self, index: usize) -> Option<T> {
        if index >= self.length {
            return None;
        }
        Some(unsafe {
            self.nodes[index / Self::CAPACITY]
                .as_ptr()
                .add(index % Self::CAPACITY)
                .read()
        })
    }

    pub(super) fn set(&mut self, index: usize, value: T) -> bool {
        if index >= self.length {
            return false;
        }
        unsafe {
            self.nodes[index / Self::CAPACITY]
                .as_ptr()
                .add(index % Self::CAPACITY)
                .write(value)
        };
        true
    }

    pub(super) fn clear(&mut self) {
        self.length = 0;
        if self.nodes.len() <= 1 {
            return;
        }
        for node in self.nodes.drain(1..) {
            unsafe { (self.resources.buffer_release)(abi::MEMORY_DEFAULT, node.as_ptr().cast()) };
        }
    }
}

impl<T: Element> Drop for Blocks<T> {
    fn drop(&mut self) {
        for node in &self.nodes {
            unsafe { (self.resources.buffer_release)(abi::MEMORY_DEFAULT, node.as_ptr().cast()) };
        }
    }
}
