//! Fallible paragraph storage. Text handles retain a single release obligation.

macro_rules! para_trace {
    ($m:expr, $($arg:tt)*) => { #[cfg(feature = "chapter-trace")] {
        $crate::para::paragraph::trace($m, format_args!($($arg)*));
    } };
}
pub mod layout;
pub mod layout_extract;
pub mod layout_line;
pub mod layout_split;
pub mod paragraph;
pub mod paragraph_extract;
mod paragraph_layout;
mod ruby;
mod tokens;
pub use tokens::{Token, Tokens};

use core::{mem, ptr, slice};

#[repr(C)]
pub struct Allocator {
    pub allocate: unsafe extern "C" fn(usize) -> *mut u8,
    pub release: unsafe extern "C" fn(*mut u8),
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Word {
    pub chunk: u32,
    pub off: u16,
    pub len: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct Chunk {
    data: *mut u8,
    capacity: u16,
    used: u16,
    live: u16,
}

pub struct WordStore {
    chunks: *mut Chunk,
    count: usize,
    capacity: usize,
    allocator: *const Allocator,
}

impl WordStore {
    /// # Safety
    /// allocator and its callbacks must remain valid until this store is dropped.
    /// Allocations must be disjoint, pointer-aligned, and released by the paired callback.
    pub unsafe fn new(allocator: *const Allocator) -> Self {
        Self {
            chunks: ptr::null_mut(),
            count: 0,
            capacity: 0,
            allocator,
        }
    }

    fn ensure_slot(&mut self) -> bool {
        if self.count < self.capacity {
            return true;
        }
        let Some(capacity) = (if self.capacity == 0 {
            Some(8)
        } else {
            self.capacity.checked_mul(2)
        }) else {
            return false;
        };
        let Some(bytes) = capacity
            .checked_mul(mem::size_of::<Chunk>())
            .filter(|n| *n <= isize::MAX as usize)
        else {
            return false;
        };
        unsafe {
            let grown = ((*self.allocator).allocate)(bytes).cast::<Chunk>();
            if grown.is_null() {
                return false;
            }
            if self.count != 0 {
                ptr::copy_nonoverlapping(self.chunks, grown, self.count);
            }
            if !self.chunks.is_null() {
                ((*self.allocator).release)(self.chunks.cast());
            }
            self.chunks = grown;
            self.capacity = capacity;
        }
        true
    }

    pub fn append(&mut self, text: &[u8]) -> Option<Word> {
        let len = text.len().min(65534);
        let need = len + 1;
        unsafe {
            let fits = self.count != 0 && {
                let tail = &*self.chunks.add(self.count - 1);
                !tail.data.is_null() && usize::from(tail.capacity - tail.used) >= need
            };
            if !fits {
                if self.count >= u32::MAX as usize || !self.ensure_slot() {
                    return None;
                }
                let capacity = need.max(2048);
                let data = ((*self.allocator).allocate)(capacity);
                if data.is_null() {
                    return None;
                }
                self.chunks.add(self.count).write(Chunk {
                    data,
                    capacity: capacity as u16,
                    used: 0,
                    live: 0,
                });
                self.count += 1;
            }
            let tail = &mut *self.chunks.add(self.count - 1);
            let word = Word {
                chunk: (self.count - 1) as u32,
                off: tail.used,
                len: len as u16,
            };
            ptr::copy_nonoverlapping(text.as_ptr(), tail.data.add(tail.used as usize), len);
            tail.data.add(tail.used as usize + len).write(0);
            tail.used += need as u16;
            tail.live += 1;
            Some(word)
        }
    }

    pub fn view(&self, word: Word) -> Option<&[u8]> {
        if word.chunk as usize >= self.count {
            return None;
        }
        unsafe {
            let chunk = &*self.chunks.add(word.chunk as usize);
            let end = usize::from(word.off) + usize::from(word.len);
            if chunk.data.is_null() || end >= chunk.used as usize {
                return None;
            }
            Some(slice::from_raw_parts(
                chunk.data.add(word.off as usize),
                word.len as usize,
            ))
        }
    }

    pub fn release(&mut self, word: Word) {
        if word.chunk as usize >= self.count {
            return;
        }
        unsafe {
            let chunk = &mut *self.chunks.add(word.chunk as usize);
            chunk.live = chunk.live.saturating_sub(1);
            if chunk.live == 0 && !chunk.data.is_null() && word.chunk as usize + 1 != self.count {
                ((*self.allocator).release)(chunk.data);
                chunk.data = ptr::null_mut();
            }
        }
    }

    pub fn chunk_count(&self) -> usize {
        self.count
    }
    pub fn chunk(&self, index: usize) -> (*const u8, usize) {
        if index >= self.count {
            return (ptr::null(), 0);
        }
        unsafe {
            let chunk = &*self.chunks.add(index);
            (chunk.data, chunk.used as usize)
        }
    }
}

impl Drop for WordStore {
    fn drop(&mut self) {
        unsafe {
            for i in 0..self.count {
                let data = (*self.chunks.add(i)).data;
                if !data.is_null() {
                    ((*self.allocator).release)(data);
                }
            }
            if !self.chunks.is_null() {
                ((*self.allocator).release)(self.chunks.cast());
            }
        }
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::{
        alloc::{alloc, dealloc, Layout},
        cell::RefCell,
        collections::BTreeMap,
        vec,
        vec::Vec,
    };

    #[derive(Default)]
    struct Heap {
        live: BTreeMap<usize, usize>,
        requests: Vec<usize>,
        fail_at: Option<usize>,
    }
    std::thread_local! { static HEAP: RefCell<Heap> = RefCell::new(Heap::default()); }
    unsafe extern "C" fn allocate(bytes: usize) -> *mut u8 {
        HEAP.with(|heap| {
            let mut heap = heap.borrow_mut();
            heap.requests.push(bytes);
            if heap.fail_at == Some(heap.requests.len()) {
                return ptr::null_mut();
            }
            let p = alloc(Layout::from_size_align(bytes, mem::align_of::<usize>()).unwrap());
            if !p.is_null() {
                heap.live.insert(p as usize, bytes);
            }
            p
        })
    }
    unsafe extern "C" fn release(p: *mut u8) {
        HEAP.with(|heap| {
            let bytes = heap.borrow_mut().live.remove(&(p as usize)).unwrap();
            dealloc(
                p,
                Layout::from_size_align(bytes, mem::align_of::<usize>()).unwrap(),
            );
        });
    }
    static ALLOC: Allocator = Allocator { allocate, release };
    fn reset(fail_at: Option<usize>) {
        HEAP.with(|heap| {
            assert!(heap.borrow().live.is_empty());
            *heap.borrow_mut() = Heap {
                fail_at,
                ..Heap::default()
            };
        });
    }
    fn store() -> WordStore {
        unsafe { WordStore::new(&ALLOC) }
    }

    #[test]
    fn split_owns_prefix_and_suffix_and_survives_every_allocation_failure() {
        use super::layout::Widths;
        use super::layout_split::{split, SplitRequest, SplitResult};
        unsafe extern "C" fn measure(
            _: *mut core::ffi::c_void,
            _: *const u8,
            n: usize,
            _: u8,
            _: u8,
            hyphen: u8,
        ) -> u16 {
            (n + usize::from(hyphen)) as u16
        }
        unsafe extern "C" fn boundary(_: u32, _: u32) -> u8 {
            1
        }
        unsafe extern "C" fn inserted(_: *mut core::ffi::c_void, _: usize) {}
        for fail in 1..=20 {
            reset(None);
            let mut s = store();
            let mut t = unsafe { Tokens::new(&ALLOC) };
            let original = s.append(b"abcdefghij").unwrap();
            assert!(t.insert(
                0,
                Token {
                    word: original,
                    focus: 4,
                    flags: 3,
                    link: 2,
                    ..Token::default()
                },
                50
            ));
            // Fill metadata capacity so split exercises growth as well as prefix storage.
            for i in 1..16 {
                assert!(t.insert(i, token(i as u32), 60 + i as u32));
            }
            let mut w = Widths {
                data: ptr::null_mut(),
                len: 0,
                capacity: 0,
                allocator: &ALLOC,
                failed: 0,
            };
            unsafe {
                assert!(w.reserve(16));
                for _ in 0..16 {
                    assert!(w.insert(w.len, 10));
                }
            }
            // Ensure the prefix needs a fresh arena chunk.
            let _padding = s.append(&vec![b'x'; 2036]).unwrap();
            HEAP.with(|h| {
                let mut h = h.borrow_mut();
                h.fail_at = Some(h.requests.len() + fail);
            });
            let request = SplitRequest {
                context: ptr::null_mut(),
                measure,
                boundary,
                inserted,
                index: 0,
                available: 6,
                language: -1,
                style: 0,
                fallback: 1,
            };
            let mut result = SplitResult::default();
            if unsafe { split(&mut s, &mut t, &mut w, &request, &mut result) } {
                assert_eq!(result.offset, 5);
                assert_eq!(s.view(t.get(0).unwrap().word), Some(&b"abcde-"[..]));
                assert_eq!(s.view(t.get(1).unwrap().word), Some(&b"fghij"[..]));
                assert_eq!(t.get(0).unwrap().flags, 3);
                assert_eq!(t.get(0).unwrap().focus, 4);
                assert_eq!(t.get(0).unwrap().style, 128);
                assert_eq!(t.get(1).unwrap().flags, 0);
                assert_eq!(t.get(1).unwrap().link, 2);
                assert_eq!(t.offset(1), 55);
                assert_eq!(w.len, 17);
            } else {
                assert_eq!(t.len(), 16);
                assert_eq!(t.get(0).unwrap().word, original);
                assert_eq!(s.view(original), Some(&b"abcdefghij"[..]));
                assert_eq!(w.len, 16);
            }
            unsafe {
                w.destroy();
            }
            drop(t);
            drop(s);
            HEAP.with(|h| assert!(h.borrow().live.is_empty()));
        }
    }

    #[test]
    fn extracted_sources_preserve_nfc_discretionary_and_visual_order() {
        use super::layout_extract::{sources, Line};
        reset(None);
        let mut s = store();
        let mut t = unsafe { Tokens::new(&ALLOC) };
        let first = s.append("é".as_bytes()).unwrap();
        let second = s.append(b"text-").unwrap();
        assert!(t.insert(
            0,
            Token {
                word: first,
                focus: 1,
                ..Token::default()
            },
            10
        ));
        assert!(t.insert(
            1,
            Token {
                word: second,
                style: 128,
                ..Token::default()
            },
            12
        ));
        assert!(t.record_absorbed(11));
        let mut line = Line {
            data: ptr::null_mut(),
            count: 0,
            allocator: &ALLOC,
            focus: 0,
        };
        let order = [1u16, 0];
        let mut paragraph = u16::MAX;
        unsafe {
            assert!(line.init(2, true));
            assert!(sources(
                &mut line,
                &s,
                &mut t,
                0,
                order.as_ptr(),
                false,
                &mut paragraph
            ));
            assert_eq!(
                core::slice::from_raw_parts(line.data.cast::<u32>(), 4),
                [12, 16, 10, 12]
            );
            assert_eq!(
                core::slice::from_raw_parts(line.data.add(20), 4),
                [128, 0, 0, 1]
            );
            assert_eq!(paragraph, 1);
            line.destroy();
        }
        drop(t);
        drop(s);
        HEAP.with(|h| assert!(h.borrow().live.is_empty()));
    }
    #[test]
    fn line_allocation_failure_and_bound_are_explicit() {
        use super::layout_extract::Line;
        reset(Some(1));
        let mut line = Line {
            data: ptr::null_mut(),
            count: 0,
            allocator: &ALLOC,
            focus: 0,
        };
        unsafe {
            assert!(!line.init(10001, true));
            assert!(!line.init(3, true));
            assert!(line.data.is_null());
            line.destroy();
        }
        HEAP.with(|h| {
            assert!(h.borrow().live.is_empty());
            assert_eq!(h.borrow().requests, [42]);
        });
    }

    #[test]
    fn chunk_sizes_nuls_and_shared_suffix_retirement() {
        reset(None);
        let mut s = store();
        let a = s.append(b"alpha").unwrap();
        let b = s.append(&vec![b'x'; 2041]).unwrap();
        assert_eq!((a.chunk, a.off, b.chunk, b.off), (0, 0, 0, 6));
        assert_eq!(s.chunk(0).1, 2048);
        let c = s.append(b"tail").unwrap();
        let suffix = Word {
            off: a.off + 2,
            len: a.len - 2,
            ..a
        };
        assert_eq!(s.view(suffix), Some(&b"pha"[..]));
        unsafe {
            assert_eq!(*s.view(a).unwrap().as_ptr().add(a.len as usize), 0);
        }
        s.release(suffix);
        assert!(!s.chunk(0).0.is_null());
        s.release(b);
        assert!(s.chunk(0).0.is_null());
        s.release(c);
        assert!(!s.chunk(1).0.is_null());
        assert_eq!(s.append(b"again").unwrap().off, 5);
        HEAP.with(|heap| {
            assert_eq!(
                heap.borrow().requests,
                [8 * mem::size_of::<Chunk>(), 2048, 2048]
            )
        });
        drop(s);
        HEAP.with(|heap| assert!(heap.borrow().live.is_empty()));
    }

    #[test]
    fn exact_large_words_truncate_and_descriptor_growth() {
        reset(None);
        let mut s = store();
        let long = s.append(&vec![b'z'; 70000]).unwrap();
        assert_eq!(long.len, 65534);
        assert_eq!(s.chunk(0).1, 65535);
        for _ in 0..8 {
            s.append(&vec![b'y'; 2048]).unwrap();
        }
        assert_eq!(s.chunk_count(), 9);
        HEAP.with(|heap| {
            let h = heap.borrow();
            assert_eq!(h.requests[1], 65535);
            assert_eq!(h.requests[2], 2049);
            assert!(h.requests.contains(&(16 * mem::size_of::<Chunk>())));
        });
        drop(s);
        HEAP.with(|heap| assert!(heap.borrow().live.is_empty()));
    }

    #[test]
    fn every_allocation_failure_preserves_prior_words_and_drops_cleanly() {
        for fail_at in 1..=12 {
            reset(Some(fail_at));
            let mut s = store();
            let mut words = Vec::new();
            for _ in 0..10 {
                match s.append(&vec![b'a'; 2048]) {
                    Some(w) => words.push(w),
                    None => break,
                }
            }
            for word in words {
                assert_eq!(s.view(word).unwrap(), &vec![b'a'; 2048]);
            }
            drop(s);
            HEAP.with(|heap| assert!(heap.borrow().live.is_empty()));
        }
    }
    fn token(index: u32) -> Token {
        Token {
            word: Word {
                chunk: index,
                off: index as u16,
                len: 7,
            },
            style: index as u8,
            focus: (index % 5) as u8,
            link: (index % 9) as u8,
            slot: (index % 4) as u8,
            flags: (index % 4) as u8,
        }
    }

    #[test]
    fn tokens_insert_erase_across_chunk_and_flag_boundaries() {
        reset(None);
        let mut tokens = unsafe { Tokens::new(&ALLOC) };
        let mut expected = Vec::new();
        for i in 0..150 {
            assert!(tokens.insert(tokens.len(), token(i), 100 + i));
            expected.push(token(i));
        }
        for index in [0, 3, 63, 64, 126, 149] {
            assert!(tokens.insert(index, token(250), 100 + index as u32));
            expected.insert(index, token(250));
        }
        for count in [3, 61, 2, 63, 27] {
            for (i, t) in expected.iter().enumerate() {
                assert_eq!(tokens.get(i), Some(*t));
            }
            tokens.erase_prefix(count);
            expected.drain(..count);
        }
        assert!(tokens.is_empty());
        assert!(tokens.insert(0, token(1), 90000));
        assert_eq!(tokens.get(0), Some(token(1)));
        assert_eq!(tokens.offset(0), 90000);
        drop(tokens);
        HEAP.with(|heap| assert!(heap.borrow().live.is_empty()));
    }

    #[test]
    fn sparse_rebases_survive_insert_and_retirement() {
        reset(None);
        let mut tokens = unsafe { Tokens::new(&ALLOC) };
        for offset in [10, 11, 65545, 65546, 131082, 8] {
            assert!(tokens.insert(tokens.len(), token(1), offset));
        }
        assert_eq!(
            (0..6).map(|i| tokens.offset(i)).collect::<Vec<_>>(),
            [10, 11, 65545, 65546, 131082, 8]
        );
        assert!(tokens.insert(3, token(2), 65545));
        assert_eq!(
            (0..7).map(|i| tokens.offset(i)).collect::<Vec<_>>(),
            [10, 11, 65545, 65545, 65546, 131082, 8]
        );
        tokens.erase_prefix(4);
        assert_eq!(
            (0..3).map(|i| tokens.offset(i)).collect::<Vec<_>>(),
            [65546, 131082, 8]
        );
        tokens.erase_prefix(3);
        assert!(tokens.insert(0, token(3), 42));
        assert_eq!(tokens.offset(0), 42);
        drop(tokens);
        HEAP.with(|heap| assert!(heap.borrow().live.is_empty()));
    }

    #[test]
    fn absorbed_positions_extend_ranges_and_reset_cursor() {
        reset(None);
        let mut tokens = unsafe { Tokens::new(&ALLOC) };
        for i in 1..=140 {
            assert!(tokens.record_absorbed(i * 2));
        }
        assert_eq!(tokens.source_after(0, 1), 1);
        assert_eq!(tokens.source_after(0, 2), 3);
        assert_eq!(tokens.source_after(100, 50), 199);
        assert_eq!(tokens.source_after(0, 2), 3);
        assert!(tokens.record_absorbed(90000));
        assert_eq!(tokens.source_after(89999, 1), 90001);
        assert!(tokens.insert(0, token(0), 0));
        assert!(tokens.insert(1, token(1), 200));
        tokens.erase_prefix(1);
        assert_eq!(tokens.source_after(200, 2), 203);
        tokens.erase_prefix(1);
        assert_eq!(tokens.source_after(0, 280), 280);
        drop(tokens);
        HEAP.with(|heap| assert!(heap.borrow().live.is_empty()));
    }

    #[test]
    fn token_allocation_failure_never_changes_logical_sequence() {
        for fail_at in 1..=40 {
            reset(Some(fail_at));
            let mut tokens = unsafe { Tokens::new(&ALLOC) };
            let mut expected = Vec::new();
            for i in 0..140 {
                let index = expected.len() / 2;
                let before = (0..tokens.len())
                    .map(|j| tokens.offset(j))
                    .collect::<Vec<_>>();
                if !tokens.insert(index, token(i), i * 70000) {
                    assert_eq!(
                        (0..tokens.len())
                            .map(|j| tokens.offset(j))
                            .collect::<Vec<_>>(),
                        before
                    );
                    break;
                }
                expected.insert(index, token(i));
            }
            assert_eq!(tokens.len(), expected.len());
            for (i, t) in expected.iter().enumerate() {
                assert_eq!(tokens.get(i), Some(*t));
            }
            drop(tokens);
            HEAP.with(|heap| assert!(heap.borrow().live.is_empty()));
        }
    }
}
