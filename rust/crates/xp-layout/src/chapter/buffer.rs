//! Match the existing C++ short-string storage for ruby and synthetic alt text.
use super::Vec;
use core::ops::{Deref, DerefMut};

pub struct TextBuffer {
    inline: [u8; 15],
    length: usize,
    heap: Vec<u8>,
}
impl TextBuffer {
    pub const fn new() -> Self {
        Self {
            inline: [0; 15],
            length: 0,
            heap: Vec::new(),
        }
    }
    pub fn reserve(&mut self, additional: usize) -> bool {
        let Some(total) = self.len().checked_add(additional) else {
            return false;
        };
        if self.heap.capacity() == 0 && total <= self.inline.len() {
            return true;
        }
        if total <= self.heap.capacity() {
            return true;
        }
        let capacity = total.max(self.heap.capacity().max(15).saturating_mul(2));
        if self
            .heap
            .try_reserve_exact(capacity - self.heap.len())
            .is_err()
        {
            return false;
        }
        if self.length != 0 {
            self.heap.extend_from_slice(&self.inline[..self.length]);
            self.length = 0;
        }
        true
    }
    pub fn append(&mut self, bytes: &[u8]) -> bool {
        if !self.reserve(bytes.len()) {
            return false;
        }
        if self.heap.capacity() == 0 {
            self.inline[self.length..self.length + bytes.len()].copy_from_slice(bytes);
            self.length += bytes.len();
        } else {
            self.heap.extend_from_slice(bytes);
        }
        true
    }
    pub fn clear(&mut self) {
        self.length = 0;
        self.heap.clear();
    }
    pub fn truncate(&mut self, length: usize) {
        if self.heap.capacity() == 0 {
            self.length = self.length.min(length);
        } else {
            self.heap.truncate(length);
        }
    }
}
impl Deref for TextBuffer {
    type Target = [u8];
    fn deref(&self) -> &[u8] {
        if self.heap.capacity() == 0 {
            &self.inline[..self.length]
        } else {
            &self.heap
        }
    }
}
impl DerefMut for TextBuffer {
    fn deref_mut(&mut self) -> &mut [u8] {
        if self.heap.capacity() == 0 {
            &mut self.inline[..self.length]
        } else {
            &mut self.heap
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn short_ruby_stays_inline_and_growth_preserves_bytes() {
        let mut text = TextBuffer::new();
        assert!(text.append(b"123456789012345"));
        assert_eq!(text.heap.capacity(), 0);
        assert!(text.append(b"6"));
        assert_eq!(&*text, b"1234567890123456");
        text.truncate(3);
        assert_eq!(&*text, b"123");
        let capacity = text.heap.capacity();
        text.clear();
        assert!(text.append(b"new"));
        assert_eq!(&*text, b"new");
        assert_eq!(text.heap.capacity(), capacity);
        assert!(!text.reserve(usize::MAX));
        assert_eq!(&*text, b"new");
    }
}
