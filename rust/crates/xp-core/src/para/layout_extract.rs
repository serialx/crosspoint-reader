//! Bounded, fallible completed-line metadata, independent of TextBlock serialization.
use super::{Allocator, Tokens, WordStore};
use core::{ptr, slice};

#[repr(C)]
pub struct Line {
    pub data: *mut u8,
    pub count: usize,
    pub allocator: *const Allocator,
    pub focus: u8,
}
impl Line {
    /// # Safety
    /// Zero-initialized owner with a live allocator, exclusively accessed. No
    /// previous allocation. count is the final line size (TextBlock limit 10000).
    pub unsafe fn init(&mut self, count: usize, focus: bool) -> bool {
        if count == 0 || count > 10000 || !self.data.is_null() {
            return false;
        }
        let bytes = count * if focus { 14 } else { 11 };
        let data = ((*self.allocator).allocate)(bytes);
        if data.is_null() {
            return false;
        }
        ptr::write_bytes(data, 0, bytes);
        self.data = data;
        self.count = count;
        self.focus = u8::from(focus);
        true
    }
    /// # Safety
    /// Live initialized owner; no outstanding borrows. Destroy exactly once.
    pub unsafe fn destroy(&mut self) {
        if !self.data.is_null() {
            ((*self.allocator).release)(self.data);
        }
        self.data = ptr::null_mut();
        self.count = 0;
    }
}

/// # Safety
/// Live disjoint exclusive owners. order is null (logical order) or count u16
/// indices into [start,start+count). Line initialized for count words. paragraph
/// output is writable/disjoint. Borrowed arrays remain valid until line destruction.
pub unsafe fn sources(
    line: &mut Line,
    store: &WordStore,
    tokens: &mut Tokens,
    start: usize,
    order: *const u16,
    paragraph_emitted: bool,
    paragraph_start: &mut u16,
) -> bool {
    if line.data.is_null() || start > tokens.len() || line.count > tokens.len() - start {
        return false;
    }
    *paragraph_start = u16::MAX;
    for i in 0..line.count {
        let local = if order.is_null() {
            i
        } else {
            usize::from(*order.add(i))
        };
        if local >= line.count {
            return false;
        }
        let logical = start + local;
        let token = tokens.get(logical).unwrap();
        if !paragraph_emitted && logical == 0 {
            *paragraph_start = i as u16;
        }
        let offset = tokens.offset(logical);
        let Some(word) = store.view(token.word) else {
            return false;
        };
        let mut position = 0;
        let mut count = 0u32;
        while position < word.len() {
            if crate::utf8::next(word, &mut position).is_none() {
                return false;
            }
            count += 1;
        }
        let discretionary = token.style & 128 != 0;
        let mut end = tokens.source_after(offset, count.wrapping_sub(u32::from(discretionary)));
        if logical + 1 < tokens.len() && tokens.offset(logical + 1) > offset {
            end = end.min(tokens.offset(logical + 1));
        }
        let ranges = line.data.cast::<u32>();
        ranges.add(2 * i).write(offset);
        ranges.add(2 * i + 1).write(end);
        // Tokens store the discretionary bit separately from small-caps font slots.
        line.data.add(10 * line.count + i).write(token.style);
        if line.focus != 0 {
            line.data.add(11 * line.count + i).write(token.focus);
        }
    }
    true
}

/// Removes UTF-8 soft-hyphen byte pairs in-place, preserving other bytes exactly.
/// # Safety
/// data borrows len writable initialized bytes exclusively; null only if len=0.
pub unsafe fn strip_soft_hyphens(data: *mut u8, len: usize) -> usize {
    if len == 0 {
        return 0;
    }
    let bytes = slice::from_raw_parts_mut(data, len);
    let mut read = 0;
    let mut write = 0;
    while read < len {
        if read + 1 < len && bytes[read] == 0xc2 && bytes[read + 1] == 0xad {
            read += 2;
            continue;
        }
        bytes[write] = bytes[read];
        write += 1;
        read += 1;
    }
    write
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    #[test]
    fn line_storage_checks_limits_oom_and_releases_exact_request() {
        std::thread_local! {
            static STATE: std::cell::Cell<(bool, usize, usize)> = const { std::cell::Cell::new((false, 0, 0)) };
        }
        unsafe extern "C" fn allocate(bytes: usize) -> *mut u8 {
            STATE.with(|state| {
                let (fail, calls, live) = state.get();
                state.set((fail, calls + 1, live));
                if fail {
                    return ptr::null_mut();
                }
                let p =
                    std::alloc::alloc(std::alloc::Layout::from_size_align(bytes + 8, 8).unwrap());
                assert!(!p.is_null());
                p.cast::<usize>().write(bytes);
                state.set((fail, calls + 1, live + 1));
                p.add(8)
            })
        }
        unsafe extern "C" fn release(p: *mut u8) {
            let p = p.sub(8);
            let bytes = p.cast::<usize>().read();
            std::alloc::dealloc(
                p,
                std::alloc::Layout::from_size_align(bytes + 8, 8).unwrap(),
            );
            STATE.with(|s| {
                let (f, c, l) = s.get();
                s.set((f, c, l - 1));
            });
        }
        let allocator = Allocator { allocate, release };
        let mut line = Line {
            data: ptr::null_mut(),
            count: 0,
            allocator: &allocator,
            focus: 0,
        };
        unsafe {
            for n in [0, 10001, usize::MAX] {
                assert!(!line.init(n, true));
            }
            STATE.with(|s| {
                assert_eq!(s.get(), (false, 0, 0));
                s.set((true, 0, 0));
            });
            assert!(!line.init(10000, true));
            assert!(line.data.is_null());
            assert_eq!(line.count, 0);
            STATE.with(|s| {
                assert_eq!(s.get(), (true, 1, 0));
                s.set((false, 1, 0));
            });
            for focus in [false, true] {
                assert!(line.init(10000, focus));
                let bytes = if focus { 140000 } else { 110000 };
                assert_eq!(line.data.sub(8).cast::<usize>().read(), bytes);
                assert!(slice::from_raw_parts(line.data, bytes)
                    .iter()
                    .all(|&b| b == 0));
                assert!(!line.init(1, focus));
                line.destroy();
                assert!(line.data.is_null());
                assert_eq!(line.count, 0);
            }
        }
        STATE.with(|s| assert_eq!(s.get(), (false, 3, 0)));
    }

    #[test]
    fn stripping_removes_only_soft_hyphen_pairs() {
        let mut bytes = *b"a\xc2\xadb\xc2\xad\xc2x";
        let len = unsafe { strip_soft_hyphens(bytes.as_mut_ptr(), bytes.len()) };
        assert_eq!(&bytes[..len], b"ab\xc2x");
        assert_eq!(unsafe { strip_soft_hyphens(ptr::null_mut(), 0) }, 0);
    }
}
