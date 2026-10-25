use super::{Allocator, Word};
use core::{mem, ptr};

struct Buffer<T> {
    data: *mut T,
    capacity: usize,
}
impl<T: Copy> Buffer<T> {
    fn new() -> Self {
        Self {
            data: ptr::null_mut(),
            capacity: 0,
        }
    }
    unsafe fn reserve(&mut self, required: usize, used: usize, allocator: &Allocator) -> bool {
        if required <= self.capacity {
            return true;
        }
        let Some(bytes) = required
            .checked_mul(mem::size_of::<T>())
            .filter(|n| *n <= isize::MAX as usize)
        else {
            return false;
        };
        let data = (allocator.allocate)(bytes).cast::<T>();
        if data.is_null() {
            return false;
        }
        if used != 0 {
            ptr::copy_nonoverlapping(self.data, data, used);
        }
        if !self.data.is_null() {
            (allocator.release)(self.data.cast());
        }
        self.data = data;
        self.capacity = required;
        true
    }
    unsafe fn free(&mut self, allocator: &Allocator) {
        if !self.data.is_null() {
            (allocator.release)(self.data.cast());
        }
        self.data = ptr::null_mut();
        self.capacity = 0;
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Token {
    pub word: Word,
    pub style: u8,
    pub focus: u8,
    pub link: u8,
    pub slot: u8,
    pub flags: u8,
}
#[derive(Clone, Copy)]
struct Rebase {
    index: usize,
    base: u32,
}
struct Absorbed {
    next: *mut Absorbed,
    base: u32,
    deltas: [u16; 64],
    begin: u8,
    count: u8,
}

pub struct Tokens {
    allocator: *const Allocator,
    len: usize,
    chunks: Buffer<*mut Word>,
    chunk_count: usize,
    head: usize,
    styles: Buffer<u8>,
    focus: Buffer<u8>,
    links: Buffer<u8>,
    slots: Buffer<u8>,
    flags: Buffer<u8>,
    deltas: Buffer<u16>,
    rebases: Buffer<Rebase>,
    rebase_count: usize,
    base: u32,
    absorbed_head: *mut Absorbed,
    absorbed_tail: *mut Absorbed,
    cursor: *mut Absorbed,
    cursor_start: u32,
}

impl Tokens {
    /// # Safety
    /// The allocator record/callbacks outlive this owner, with the WordStore allocator contract.
    pub unsafe fn new(allocator: *const Allocator) -> Self {
        Self {
            allocator,
            len: 0,
            chunks: Buffer::new(),
            chunk_count: 0,
            head: 0,
            styles: Buffer::new(),
            focus: Buffer::new(),
            links: Buffer::new(),
            slots: Buffer::new(),
            flags: Buffer::new(),
            deltas: Buffer::new(),
            rebases: Buffer::new(),
            rebase_count: 0,
            base: 0,
            absorbed_head: ptr::null_mut(),
            absorbed_tail: ptr::null_mut(),
            cursor: ptr::null_mut(),
            cursor_start: 0,
        }
    }
    pub fn len(&self) -> usize {
        self.len
    }
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    pub fn reserve(&mut self, required: usize) -> bool {
        if required
            <= self
                .styles
                .capacity
                .min(self.focus.capacity)
                .min(self.links.capacity)
                .min(self.slots.capacity)
                .min(self.deltas.capacity)
                .min(self.flags.capacity.saturating_mul(4))
        {
            return true;
        }
        let Some(capacity) = required.max(16).checked_next_power_of_two() else {
            return false;
        };
        unsafe {
            let a = &*self.allocator;
            self.styles.reserve(capacity, self.len, a)
                && self.focus.reserve(capacity, self.len, a)
                && self.links.reserve(capacity, self.len, a)
                && self.slots.reserve(capacity, self.len, a)
                && self.deltas.reserve(capacity, self.len, a)
                && self.flags.reserve(capacity / 4, self.len.div_ceil(4), a)
        }
    }
    unsafe fn word_ptr(&self, index: usize) -> *mut Word {
        (*self.chunks.data.add((self.head + index) / 64)).add((self.head + index) % 64)
    }
    unsafe fn flag(&self, index: usize) -> u8 {
        (*self.flags.data.add(index / 4) >> ((index % 4) * 2)) & 3
    }
    unsafe fn set_flag(&mut self, index: usize, flags: u8) {
        let byte = self.flags.data.add(index / 4);
        let shift = (index % 4) * 2;
        // The byte holding a new group has no prior initialized flags.
        let old = if index >= self.len && index.is_multiple_of(4) {
            0
        } else {
            *byte
        };
        byte.write((old & !(3 << shift)) | ((flags & 3) << shift));
    }
    pub fn get(&self, index: usize) -> Option<Token> {
        if index >= self.len {
            return None;
        }
        unsafe {
            Some(Token {
                word: *self.word_ptr(index),
                style: *self.styles.data.add(index),
                focus: *self.focus.data.add(index),
                link: *self.links.data.add(index),
                slot: *self.slots.data.add(index),
                flags: self.flag(index),
            })
        }
    }
    pub fn set(&mut self, index: usize, token: Token) -> bool {
        if index >= self.len {
            return false;
        }
        unsafe {
            self.write_token(index, token);
        }
        true
    }
    unsafe fn write_token(&mut self, index: usize, t: Token) {
        self.word_ptr(index).write(t.word);
        self.styles.data.add(index).write(t.style);
        self.focus.data.add(index).write(t.focus);
        self.links.data.add(index).write(t.link);
        self.slots.data.add(index).write(t.slot);
        self.set_flag(index, t.flags);
    }
    fn base_at(&self, index: usize) -> u32 {
        let mut base = self.base;
        unsafe {
            for i in 0..self.rebase_count {
                let r = &*self.rebases.data.add(i);
                if r.index > index {
                    break;
                }
                base = r.base;
            }
        }
        base
    }
    pub fn offset(&self, index: usize) -> u32 {
        if index >= self.len {
            return 0;
        }
        self.base_at(index)
            .wrapping_add(unsafe { *self.deltas.data.add(index) } as u32)
    }
    pub fn insert(&mut self, index: usize, token: Token, offset: u32) -> bool {
        if index > self.len || self.len == usize::MAX || !self.reserve(self.len + 1) {
            return false;
        }
        let base = if self.len == 0 {
            offset
        } else if index == 0 {
            self.base
        } else {
            self.base_at(index - 1)
        };
        let rebased = offset < base || offset - base > u16::MAX as u32;
        unsafe {
            let a = &*self.allocator;
            if rebased && self.rebase_count == self.rebases.capacity {
                let Some(capacity) = self.rebase_count.max(1).checked_mul(2) else {
                    return false;
                };
                if !self.rebases.reserve(capacity, self.rebase_count, a) {
                    return false;
                }
            }
            if self.head + self.len == self.chunk_count * 64 {
                if self.chunk_count == self.chunks.capacity {
                    let Some(capacity) = self.chunk_count.max(4).checked_mul(2) else {
                        return false;
                    };
                    if !self.chunks.reserve(capacity, self.chunk_count, a) {
                        return false;
                    }
                }
                let chunk = (a.allocate)(64 * mem::size_of::<Word>()).cast::<Word>();
                if chunk.is_null() {
                    return false;
                }
                self.chunks.data.add(self.chunk_count).write(chunk);
                self.chunk_count += 1;
            }
            for i in (index..self.len).rev() {
                self.write_token(i + 1, self.get(i).unwrap());
                self.deltas.data.add(i + 1).write(*self.deltas.data.add(i));
            }
            self.write_token(index, token);
            if self.len == 0 {
                self.base = offset;
            }
            let mut insertion = self.rebase_count;
            for i in 0..self.rebase_count {
                let r = &mut *self.rebases.data.add(i);
                if r.index >= index {
                    r.index += 1;
                    insertion = insertion.min(i);
                }
            }
            if rebased {
                ptr::copy(
                    self.rebases.data.add(insertion),
                    self.rebases.data.add(insertion + 1),
                    self.rebase_count - insertion,
                );
                self.rebases.data.add(insertion).write(Rebase {
                    index,
                    base: offset,
                });
                self.rebase_count += 1;
            }
            self.deltas
                .data
                .add(index)
                .write(if rebased { 0 } else { (offset - base) as u16 });
            self.len += 1;
        }
        true
    }
    pub fn erase_prefix(&mut self, count: usize) {
        let count = count.min(self.len);
        if count == 0 {
            return;
        }
        let remaining = self.len - count;
        let new_base = if remaining == 0 {
            0
        } else {
            self.base_at(count)
        };
        self.retire_absorbed(if remaining == 0 {
            u32::MAX
        } else {
            self.offset(count)
        });
        unsafe {
            for i in 0..remaining {
                self.styles
                    .data
                    .add(i)
                    .write(*self.styles.data.add(i + count));
                self.focus
                    .data
                    .add(i)
                    .write(*self.focus.data.add(i + count));
                self.links
                    .data
                    .add(i)
                    .write(*self.links.data.add(i + count));
                self.slots
                    .data
                    .add(i)
                    .write(*self.slots.data.add(i + count));
                self.deltas
                    .data
                    .add(i)
                    .write(*self.deltas.data.add(i + count));
                self.set_flag(i, self.flag(i + count));
            }
            let retired = (self.head + count) / 64;
            for i in 0..retired {
                ((*self.allocator).release)((*self.chunks.data.add(i)).cast());
            }
            ptr::copy(
                self.chunks.data.add(retired),
                self.chunks.data,
                self.chunk_count - retired,
            );
            self.chunk_count -= retired;
            self.head = (self.head + count) % 64;
            let mut write = 0;
            for i in 0..self.rebase_count {
                let mut r = *self.rebases.data.add(i);
                if r.index <= count {
                    continue;
                }
                r.index -= count;
                self.rebases.data.add(write).write(r);
                write += 1;
            }
            self.rebase_count = if remaining == 0 { 0 } else { write };
        }
        self.base = new_base;
        self.len = remaining;
    }
    pub fn record_absorbed(&mut self, offset: u32) -> bool {
        unsafe {
            if self.absorbed_tail.is_null()
                || (*self.absorbed_tail).count == 64
                || offset < (*self.absorbed_tail).base
                || offset - (*self.absorbed_tail).base > u16::MAX as u32
            {
                let chunk =
                    ((*self.allocator).allocate)(mem::size_of::<Absorbed>()).cast::<Absorbed>();
                if chunk.is_null() {
                    return false;
                }
                // Initialize in place, avoiding a 140-byte temporary on the render task stack.
                ptr::addr_of_mut!((*chunk).next).write(ptr::null_mut());
                ptr::addr_of_mut!((*chunk).base).write(offset);
                ptr::addr_of_mut!((*chunk).deltas)
                    .cast::<u16>()
                    .write_bytes(0, 64);
                ptr::addr_of_mut!((*chunk).begin).write(0);
                ptr::addr_of_mut!((*chunk).count).write(0);
                if self.absorbed_tail.is_null() {
                    self.absorbed_head = chunk;
                } else {
                    (*self.absorbed_tail).next = chunk;
                }
                self.absorbed_tail = chunk;
            }
            let tail = &mut *self.absorbed_tail;
            tail.deltas[tail.count as usize] = (offset - tail.base) as u16;
            tail.count += 1;
        }
        true
    }
    pub fn source_after(&mut self, start: u32, rendered: u32) -> u32 {
        let mut end = start.wrapping_add(rendered);
        unsafe {
            let mut chunk = if !self.cursor.is_null() && start >= self.cursor_start {
                self.cursor
            } else {
                self.absorbed_head
            };
            while !chunk.is_null()
                && (*chunk)
                    .base
                    .wrapping_add((*chunk).deltas[(*chunk).count as usize - 1] as u32)
                    <= start
            {
                chunk = (*chunk).next;
            }
            self.cursor = chunk;
            self.cursor_start = start;
            while !chunk.is_null() {
                let c = &*chunk;
                for &delta in &c.deltas[c.begin as usize..c.count as usize] {
                    if start >= c.base && delta as u32 <= start - c.base {
                        continue;
                    }
                    if c.base.wrapping_add(delta as u32) > end {
                        return end;
                    }
                    end = end.wrapping_add(1);
                }
                chunk = c.next;
            }
        }
        end
    }
    fn retire_absorbed(&mut self, start: u32) {
        self.cursor = ptr::null_mut();
        unsafe {
            while !self.absorbed_head.is_null() {
                let c = &mut *self.absorbed_head;
                if c.base.wrapping_add(c.deltas[c.count as usize - 1] as u32) > start {
                    if start >= c.base {
                        c.begin = c.deltas[..c.count as usize]
                            .partition_point(|d| *d as u32 <= start - c.base)
                            as u8;
                    }
                    break;
                }
                let retired = self.absorbed_head;
                self.absorbed_head = c.next;
                ((*self.allocator).release)(retired.cast());
            }
            if self.absorbed_head.is_null() {
                self.absorbed_tail = ptr::null_mut();
            }
        }
    }
}
impl Drop for Tokens {
    fn drop(&mut self) {
        unsafe {
            let a = &*self.allocator;
            for i in 0..self.chunk_count {
                (a.release)((*self.chunks.data.add(i)).cast());
            }
            self.chunks.free(a);
            self.styles.free(a);
            self.focus.free(a);
            self.links.free(a);
            self.slots.free(a);
            self.flags.free(a);
            self.deltas.free(a);
            self.rebases.free(a);
            while !self.absorbed_head.is_null() {
                let c = self.absorbed_head;
                self.absorbed_head = (*c).next;
                (a.release)(c.cast());
            }
        }
    }
}
