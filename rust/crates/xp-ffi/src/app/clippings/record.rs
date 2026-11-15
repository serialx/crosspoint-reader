extern crate alloc;

use alloc::{alloc::alloc, boxed::Box};
use core::{alloc::Layout, ptr::NonNull};

pub const PER_BLOCK: usize = 16;
pub const TEXT_MAX: usize = 4096;

// Memory order is independent of the explicit on-disk field order.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Record {
    pub revision: u64,
    pub timestamp: u32,
    pub signature: u32,
    pub start_offset: u32,
    pub end_offset: u32,
    pub text_offset: u32,
    pub spine: u16,
    pub start_page: u16,
    pub end_page: u16,
    pub page_count: u16,
    pub start_word: u16,
    pub end_word: u16,
    pub word_count: u16,
    pub paragraph: u16,
    pub text_length: u16,
    pub pending: u8,
    pub id: [u8; 65],
    pub chapter: [u8; 48],
}
const _: () = assert!(core::mem::size_of::<Record>() == 160);

impl Default for Record {
    fn default() -> Self {
        Self {
            revision: 0,
            timestamp: 0,
            signature: 0,
            start_offset: u32::MAX,
            end_offset: u32::MAX,
            text_offset: 0,
            spine: 0,
            start_page: 0,
            end_page: 0,
            page_count: 1,
            start_word: 0,
            end_word: 0,
            word_count: 0,
            paragraph: u16::MAX,
            text_length: 0,
            pending: 1,
            id: [0; 65],
            chapter: [0; 48],
        }
    }
}

pub struct Index<const BLOCKS: usize> {
    blocks: [Option<Box<[Record; PER_BLOCK]>>; BLOCKS],
    capacity: usize,
}

impl<const BLOCKS: usize> Default for Index<BLOCKS> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const BLOCKS: usize> Index<BLOCKS> {
    pub fn new() -> Self {
        Self {
            blocks: core::array::from_fn(|_| None),
            capacity: 0,
        }
    }

    #[cfg(test)]
    pub fn capacity(&self) -> usize {
        self.capacity
    }

    pub fn reserve(&mut self, count: usize) -> bool {
        if count > BLOCKS * PER_BLOCK {
            return false;
        }
        while self.capacity < count {
            let Some(pointer) = NonNull::new(unsafe {
                alloc(Layout::new::<[Record; PER_BLOCK]>()).cast::<Record>()
            }) else {
                return false;
            };
            // Initialize one record at a time; never put the slab on the task stack.
            for slot in 0..PER_BLOCK {
                unsafe {
                    pointer.as_ptr().add(slot).write(Record::default());
                }
            }
            self.blocks[self.capacity / PER_BLOCK] =
                Some(unsafe { Box::from_raw(pointer.as_ptr().cast::<[Record; PER_BLOCK]>()) });
            self.capacity += PER_BLOCK;
        }
        true
    }

    pub fn slot(&self, index: usize) -> &Record {
        &self.blocks[index / PER_BLOCK]
            .as_ref()
            .expect("reserved slab")[index % PER_BLOCK]
    }

    pub fn slot_mut(&mut self, index: usize) -> &mut Record {
        &mut self.blocks[index / PER_BLOCK]
            .as_mut()
            .expect("reserved slab")[index % PER_BLOCK]
    }

    pub fn clear(&mut self) {
        self.blocks.fill_with(|| None);
        self.capacity = 0;
    }
}
