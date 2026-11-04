//! Fixed payload chunks with a separately grown directory of owning pointers.
extern crate alloc;

use crate::app::abi;
use alloc::vec::Vec;
use core::{alloc::Layout, mem, ptr::NonNull};

pub(super) struct Records<T, const N: usize> {
    chunks: Vec<NonNull<T>>,
    len: usize,
    resources: &'static abi::Resources,
}

impl<T, const N: usize> Records<T, N> {
    pub(super) fn new(resources: &'static abi::Resources) -> Self {
        // DEFAULT storage follows malloc alignment, not arbitrary Rust alignment.
        assert!(N != 0 && mem::size_of::<T>() != 0);
        assert!(mem::align_of::<T>() <= mem::align_of::<usize>());
        Self {
            chunks: Vec::new(),
            len: 0,
            resources,
        }
    }

    pub(super) fn len(&self) -> usize {
        self.len
    }

    pub(super) fn reserve_one(&mut self) -> Result<(), ()> {
        self.len.checked_add(1).ok_or(())?;
        if self.len / N < self.chunks.len() {
            return Ok(());
        }
        let layout = Layout::array::<T>(N).map_err(|_| ())?;
        // Release an old directory before requesting the next payload chunk.
        self.chunks.try_reserve_exact(1).map_err(|_| ())?;
        let data = unsafe { (self.resources.buffer_allocate)(abi::MEMORY_DEFAULT, layout.size()) };
        let chunk = NonNull::new(data.cast::<T>()).ok_or(())?;
        self.chunks.push(chunk);
        Ok(())
    }

    pub(super) fn push_reserved(&mut self, value: T) {
        assert!(self.len / N < self.chunks.len());
        let chunk = self.chunks[self.len / N];
        // reserve_one allocated this slot; only indices below len are initialized.
        unsafe { chunk.as_ptr().add(self.len % N).write(value) };
        self.len += 1;
    }

    pub(super) fn get(&self, index: usize) -> Option<&T> {
        if index >= self.len {
            return None;
        }
        let chunk = self.chunks[index / N];
        Some(unsafe { &*chunk.as_ptr().add(index % N) })
    }

    pub(super) fn allocated_capacity(&self) -> (usize, usize) {
        (self.chunks.len(), self.chunks.capacity())
    }
}

impl<T, const N: usize> Drop for Records<T, N> {
    fn drop(&mut self) {
        for index in 0..self.len {
            let chunk = self.chunks[index / N];
            unsafe { chunk.as_ptr().add(index % N).drop_in_place() };
        }
        for chunk in &self.chunks {
            unsafe { (self.resources.buffer_release)(abi::MEMORY_DEFAULT, chunk.as_ptr().cast()) };
        }
    }
}
