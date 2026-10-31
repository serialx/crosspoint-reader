//! Fallible ruby metadata in fixed chunks; consumed prefixes release whole chunks.
use super::{paragraph::Bytes, Allocator};
use alloc::vec::Vec;
use core::{mem, ptr};
extern crate alloc;

const CHUNK: usize = 16;

pub struct Ruby {
    chunks: Vec<*mut Bytes>,
    head: usize,
    len: usize,
    allocator: *const Allocator,
}
impl Ruby {
    /// # Safety
    /// allocator remains live and returns aligned storage until this owner is dropped.
    pub unsafe fn new(allocator: *const Allocator) -> Self {
        Self {
            chunks: Vec::new(),
            head: 0,
            len: 0,
            allocator,
        }
    }
    pub fn len(&self) -> usize {
        self.len
    }
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    fn slot(&self, index: usize) -> *mut Bytes {
        let index = self.head + index;
        unsafe { self.chunks[index / CHUNK].add(index % CHUNK) }
    }
    pub fn get(&self, index: usize) -> Option<&Bytes> {
        (index < self.len).then(|| unsafe { &*self.slot(index) })
    }
    pub fn get_mut(&mut self, index: usize) -> Option<&mut Bytes> {
        (index < self.len).then(|| unsafe { &mut *self.slot(index) })
    }
    pub fn reserve(&mut self, len: usize) -> bool {
        let Some(end) = self.head.checked_add(len) else {
            return false;
        };
        let count = end.div_ceil(CHUNK);
        if self
            .chunks
            .try_reserve(count.saturating_sub(self.chunks.len()))
            .is_err()
        {
            return false;
        }
        while self.chunks.len() < count {
            unsafe {
                let chunk = (*self.allocator)
                    .allocate_bytes(CHUNK * mem::size_of::<Bytes>(), 0)
                    .cast::<Bytes>();
                if chunk.is_null() {
                    (*self.allocator).diagnose(
                        super::Diagnostic::Storage,
                        (CHUNK * mem::size_of::<Bytes>()) as u32,
                    );
                    return false;
                }
                // Initialize in place: no chunk-sized render-task stack temporary.
                for i in 0..CHUNK {
                    chunk.add(i).write(Bytes::default());
                }
                self.chunks.push(chunk);
            }
        }
        true
    }
    pub fn resize(&mut self, len: usize) -> bool {
        if len < self.len {
            return false;
        }
        if !self.reserve(len) {
            return false;
        }
        self.len = len;
        true
    }
    // The split transaction reserves before mutating token storage.
    pub fn insert_reserved(&mut self, index: usize) {
        debug_assert!(index <= self.len);
        debug_assert!(self.head + self.len < self.chunks.len() * CHUNK);
        unsafe {
            for i in (index..self.len).rev() {
                ptr::swap(self.slot(i), self.slot(i + 1));
            }
        }
        self.len += 1;
    }
    pub fn erase_prefix(&mut self, count: usize) {
        let count = count.min(self.len);
        for i in 0..count {
            unsafe {
                *self.slot(i) = Bytes::default();
            }
        }
        self.len -= count;
        self.head += count;
        let retired = if self.len == 0 {
            self.chunks.len()
        } else {
            self.head / CHUNK
        };
        for chunk in self.chunks.drain(..retired) {
            unsafe {
                ptr::drop_in_place(ptr::slice_from_raw_parts_mut(chunk, CHUNK));
                ((*self.allocator).release)(chunk.cast());
            }
        }
        self.head %= CHUNK;
        if self.len == 0 {
            self.head = 0;
            self.chunks = Vec::new();
        }
    }
}
impl Drop for Ruby {
    fn drop(&mut self) {
        self.erase_prefix(self.len);
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::{
        alloc::{alloc, dealloc, Layout},
        cell::Cell,
    };
    std::thread_local! {
        static STATE: Cell<(usize, usize, usize, usize)> = const { Cell::new((usize::MAX, 0, 0, 0)) };
    }
    unsafe extern "C" fn allocate(size: usize, _: u8) -> *mut u8 {
        STATE.with(|state| {
            let (budget, live, peak, largest) = state.get();
            if budget == 0 {
                return ptr::null_mut();
            }
            assert_eq!(size, CHUNK * mem::size_of::<Bytes>());
            let p = alloc(Layout::new::<[Bytes; CHUNK]>());
            assert!(!p.is_null());
            state.set((
                budget.saturating_sub(1),
                live + size,
                peak.max(live + size),
                largest.max(size),
            ));
            p
        })
    }
    unsafe extern "C" fn release(p: *mut u8) {
        STATE.with(|state| {
            let (budget, live, peak, largest) = state.get();
            state.set((
                budget,
                live - CHUNK * mem::size_of::<Bytes>(),
                peak,
                largest,
            ));
        });
        dealloc(p, Layout::new::<[Bytes; CHUNK]>());
    }
    static ALLOCATOR: Allocator = Allocator {
        allocate,
        release,
        diagnostic: None,
        reclaim: None,
    };

    #[test]
    fn growth_boundaries_and_consumption_release_chunks() {
        for len in [512usize, 513, 1024, 1025, 2048, 2049] {
            STATE.with(|s| s.set((usize::MAX, 0, 0, 0)));
            let mut ruby = unsafe { Ruby::new(&ALLOCATOR) };
            let mut peak_with_table = 0;
            for i in 0..len {
                let old_table = ruby.chunks.capacity() * mem::size_of::<*mut Bytes>();
                let old_chunks = STATE.with(Cell::get).1;
                assert!(ruby.resize(i + 1));
                assert!(ruby.get_mut(i).unwrap().set(b"ruby"));
                let table = ruby.chunks.capacity() * mem::size_of::<*mut Bytes>();
                if table != old_table {
                    peak_with_table = peak_with_table.max(old_chunks + old_table + table);
                }
                peak_with_table = peak_with_table.max(STATE.with(Cell::get).1 + table);
            }
            let mut old_vector = Vec::new();
            let mut old_peak = 0;
            let mut old_largest = 0;
            for _ in 0..len {
                let old_bytes = old_vector.capacity() * mem::size_of::<Bytes>();
                old_vector.try_reserve(1).unwrap();
                let new_bytes = old_vector.capacity() * mem::size_of::<Bytes>();
                if new_bytes != old_bytes {
                    old_peak = old_peak.max(old_bytes + new_bytes);
                    old_largest = old_largest.max(new_bytes);
                }
                old_vector.push(Bytes::default());
            }
            std::println!("entries={len} old_vector_live={} old_vector_peak={old_peak} old_largest={old_largest} new_peak_with_table={peak_with_table}", old_vector.capacity() * mem::size_of::<Bytes>());
            let (_, live, peak, largest) = STATE.with(Cell::get);
            assert_eq!(live, len.div_ceil(CHUNK) * CHUNK * mem::size_of::<Bytes>());
            assert_eq!(peak, live);
            assert_eq!(largest, CHUNK * mem::size_of::<Bytes>());
            std::println!(
                "entries={len} chunk_live={live} peak={peak} largest={largest} table_bytes={}",
                ruby.chunks.capacity() * mem::size_of::<*mut Bytes>()
            );
            ruby.erase_prefix(len - 17);
            assert_eq!(ruby.len(), 17);
            assert_eq!(ruby.get(0).unwrap().bytes(), b"ruby");
            assert!(STATE.with(Cell::get).1 <= 3 * CHUNK * mem::size_of::<Bytes>());
            ruby.erase_prefix(17);
            assert_eq!(STATE.with(Cell::get).1, 0);
            assert_eq!(ruby.chunks.capacity(), 0);
        }
    }

    #[test]
    fn every_chunk_failure_cleans_up() {
        for budget in 0..=129 {
            STATE.with(|s| s.set((budget, 0, 0, 0)));
            {
                let mut ruby = unsafe { Ruby::new(&ALLOCATOR) };
                assert_eq!(ruby.resize(2049), budget == 129);
            }
            assert_eq!(STATE.with(Cell::get).1, 0);
        }
    }

    #[test]
    fn failed_growth_preserves_existing_annotations() {
        for len in (CHUNK..=2048).step_by(CHUNK) {
            STATE.with(|s| s.set((usize::MAX, 0, 0, 0)));
            {
                let mut ruby = unsafe { Ruby::new(&ALLOCATOR) };
                assert!(ruby.resize(len));
                assert!(ruby.get_mut(len - 1).unwrap().set(b"last"));
                STATE.with(|s| {
                    let (_, live, peak, largest) = s.get();
                    s.set((0, live, peak, largest));
                });
                assert!(!ruby.reserve(len + 1));
                assert_eq!(ruby.len(), len);
                assert_eq!(ruby.get(len - 1).unwrap().bytes(), b"last");
            }
            assert_eq!(STATE.with(Cell::get).1, 0);
        }
    }

    #[test]
    fn split_insertion_preserves_ruby_across_chunks_and_consumed_head() {
        STATE.with(|s| s.set((usize::MAX, 0, 0, 0)));
        {
            let mut ruby = unsafe { Ruby::new(&ALLOCATOR) };
            assert!(ruby.resize(34));
            assert!(ruby.get_mut(16).unwrap().set(b"marker"));
            ruby.erase_prefix(1);
            assert!(ruby.reserve(34));
            ruby.insert_reserved(15);
            assert!(ruby.get(15).unwrap().bytes().is_empty());
            assert_eq!(ruby.get(16).unwrap().bytes(), b"marker");
        }
        assert_eq!(STATE.with(Cell::get).1, 0);
    }
}
