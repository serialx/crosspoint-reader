//! Paragraph breakers. The synchronous host supplies metrics and token mutations.
use super::Allocator;
use core::{ffi::c_void, mem, ptr};

#[repr(C)]
pub struct Callbacks {
    pub context: *mut c_void,
    pub count: unsafe extern "C" fn(*mut c_void) -> usize,
    pub width: unsafe extern "C" fn(*mut c_void, usize) -> u16,
    pub allows_break: unsafe extern "C" fn(*mut c_void, usize) -> u8,
    pub gap: unsafe extern "C" fn(*mut c_void, usize) -> i32,
    pub ruby_start: unsafe extern "C" fn(*mut c_void, usize, usize) -> i32,
    pub ruby_end: unsafe extern "C" fn(*mut c_void, usize, usize) -> i32,
    pub split: unsafe extern "C" fn(*mut c_void, usize, i32, u8) -> u8,
    pub emit: unsafe extern "C" fn(*mut c_void, usize) -> u8,
}

// Each array owns one exact-sized allocation, with no growth or stack-sized copy.
struct Array<T> {
    data: *mut T,
    allocator: *const Allocator,
}
impl<T: Copy> Array<T> {
    unsafe fn new(count: usize, allocator: *const Allocator) -> Option<Self> {
        let bytes = count.checked_mul(mem::size_of::<T>())?;
        if bytes == 0 || bytes > isize::MAX as usize {
            return None;
        }
        let data = ((*allocator).allocate)(bytes).cast::<T>();
        if data.is_null() {
            return None;
        }
        // DP fallback reads initialized answers, including unreachable line ends.
        ptr::write_bytes(data, 0, count);
        Some(Self { data, allocator })
    }
    unsafe fn get(&self, index: usize) -> T {
        *self.data.add(index)
    }
    unsafe fn set(&mut self, index: usize, value: T) {
        self.data.add(index).write(value);
    }
}
impl<T> Drop for Array<T> {
    fn drop(&mut self) {
        unsafe { ((*self.allocator).release)(self.data.cast()) }
    }
}

/// # Safety
/// Callbacks and allocator remain valid, serialized and non-reentrant. count/width
/// describe one sequence; split success inserts exactly one token and updates widths.
/// Other callbacks must not mutate it. Every queried index is in range, except
/// ruby interval ends, which are exclusive. emit must copy the break before returning.
pub unsafe fn optimal(c: &Callbacks, allocator: *const Allocator, page: i32, indent: i32) -> bool {
    if (c.count)(c.context) == 0 {
        return true;
    }
    let mut i = 0;
    while i < (c.count)(c.context) {
        let width = if i == 0 { page - indent } else { page };
        while i32::from((c.width)(c.context, i)) > width {
            if (c.split)(c.context, i, width, 1) == 0 {
                break;
            }
        }
        i += 1;
    }
    let n = (c.count)(c.context);
    if n > i32::MAX as usize {
        return false;
    }
    let Some(mut dp) = Array::<i32>::new(n, allocator) else {
        return false;
    };
    let Some(mut ans) = Array::<usize>::new(n, allocator) else {
        return false;
    };
    dp.set(n - 1, 0);
    ans.set(n - 1, n - 1);
    for i in (0..n - 1).rev() {
        let mut length = 0;
        dp.set(i, i32::MAX);
        let effective = if i == 0 { page - indent } else { page };
        for j in i..n {
            let gap = if j > i { (c.gap)(c.context, j) } else { 0 };
            let start = if j == i {
                (c.ruby_start)(c.context, i, n)
            } else {
                0
            };
            length += i32::from((c.width)(c.context, j)) + gap + start;
            if length > effective {
                break;
            }
            if j + 1 < n && (c.allows_break)(c.context, j + 1) == 0 {
                continue;
            }
            let end = (c.ruby_end)(c.context, i, j + 1);
            if length + end > effective {
                continue;
            }
            let cost = if j == n - 1 {
                0
            } else {
                let remaining = effective - length;
                (i64::from(remaining) * i64::from(remaining) + i64::from(dp.get(j + 1)))
                    .min(i64::from(i32::MAX)) as i32
            };
            // Equal cost selects the longer line; MAX_COST still triggers fallback.
            if cost <= dp.get(i) {
                dp.set(i, cost);
                ans.set(i, j);
            }
        }
        if dp.get(i) == i32::MAX {
            ans.set(i, i);
            dp.set(i, dp.get(i + 1));
        }
    }
    let mut current = 0;
    while current < n {
        let next = (ans.get(current) + 1).max(current + 1);
        if (c.emit)(c.context, next) == 0 {
            return false;
        }
        current = next;
    }
    true
}

/// # Safety
/// Same callback contract as optimal. No Rust heap storage is required.
pub unsafe fn greedy(
    c: &Callbacks,
    page: i32,
    indent: i32,
    max_lines: usize,
    first_consumed: bool,
    hyphenation: bool,
) -> bool {
    let mut current = 0;
    let mut first = !first_consumed;
    let mut lines = 0;
    while current < (c.count)(c.context) && lines < max_lines {
        let start = current;
        let mut fitting = start;
        let mut width = (c.ruby_start)(c.context, start, (c.count)(c.context));
        let effective = if first { page - indent } else { page };
        while current < (c.count)(c.context) {
            let first_word = current == start;
            let spacing = if first_word {
                0
            } else {
                (c.gap)(c.context, current)
            };
            let candidate = spacing + i32::from((c.width)(c.context, current));
            let end = (c.ruby_end)(c.context, start, current + 1);
            if width + candidate <= effective {
                width += candidate;
                current += 1;
                if width + end <= effective
                    && (current == (c.count)(c.context)
                        || (c.allows_break)(c.context, current) != 0)
                {
                    fitting = current;
                }
                continue;
            }
            let available = effective - width - spacing;
            if width + candidate > effective
                && available > 0
                && (hyphenation || first_word)
                && (c.split)(c.context, current, available, u8::from(first_word)) != 0
            {
                current += 1;
                fitting = current;
                break;
            }
            if current == start {
                current += 1;
            }
            break;
        }
        if fitting > start {
            current = fitting;
        }
        while current > start + 1
            && current < (c.count)(c.context)
            && (c.allows_break)(c.context, current) == 0
        {
            current -= 1;
        }
        if (c.emit)(c.context, current) == 0 {
            return false;
        }
        lines += 1;
        first = false;
    }
    true
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::{cell::RefCell, vec, vec::Vec};

    std::thread_local! {
        static HEAP: RefCell<(usize, usize, usize)> = const { RefCell::new((0, 0, 0)) };
    }
    unsafe extern "C" fn allocate(bytes: usize) -> *mut u8 {
        let failed = HEAP.with(|h| {
            let mut h = h.borrow_mut();
            h.0 += 1;
            h.0 == h.1
        });
        if failed {
            return ptr::null_mut();
        }
        let layout = std::alloc::Layout::from_size_align(bytes + 8, 8).unwrap();
        let p = std::alloc::alloc(layout);
        if p.is_null() {
            return p;
        }
        p.cast::<usize>().write(bytes);
        HEAP.with(|h| h.borrow_mut().2 += 1);
        p.add(8)
    }
    unsafe extern "C" fn release(p: *mut u8) {
        let base = p.sub(8);
        let bytes = base.cast::<usize>().read();
        std::alloc::dealloc(
            base,
            std::alloc::Layout::from_size_align(bytes + 8, 8).unwrap(),
        );
        HEAP.with(|h| h.borrow_mut().2 -= 1);
    }
    static ALLOCATOR: Allocator = Allocator { allocate, release };
    struct Fixture {
        widths: Vec<u16>,
        breaks: Vec<usize>,
        attached: usize,
        gap: i32,
        ruby_end: i32,
        split: bool,
        split_calls: Vec<(usize, i32, u8)>,
        fail_emit: bool,
    }
    impl Fixture {
        fn new(widths: &[u16]) -> Self {
            Self {
                widths: widths.to_vec(),
                breaks: vec![],
                attached: usize::MAX,
                gap: 0,
                ruby_end: 0,
                split: false,
                split_calls: vec![],
                fail_emit: false,
            }
        }
        fn callbacks(&mut self) -> Callbacks {
            unsafe extern "C" fn count(p: *mut c_void) -> usize {
                (*p.cast::<Fixture>()).widths.len()
            }
            unsafe extern "C" fn width(p: *mut c_void, i: usize) -> u16 {
                (&(*p.cast::<Fixture>()).widths)[i]
            }
            unsafe extern "C" fn allows(p: *mut c_void, i: usize) -> u8 {
                u8::from((*p.cast::<Fixture>()).attached != i)
            }
            unsafe extern "C" fn gap(p: *mut c_void, _: usize) -> i32 {
                (*p.cast::<Fixture>()).gap
            }
            unsafe extern "C" fn start(_: *mut c_void, _: usize, _: usize) -> i32 {
                0
            }
            unsafe extern "C" fn end(p: *mut c_void, _: usize, _: usize) -> i32 {
                (*p.cast::<Fixture>()).ruby_end
            }
            unsafe extern "C" fn split(
                p: *mut c_void,
                i: usize,
                available: i32,
                fallback: u8,
            ) -> u8 {
                let f = &mut *p.cast::<Fixture>();
                f.split_calls.push((i, available, fallback));
                if !f.split || available <= 0 || i32::from(f.widths[i]) <= available {
                    return 0;
                }
                let rest = f.widths[i] - available as u16;
                f.widths[i] = available as u16;
                f.widths.insert(i + 1, rest);
                1
            }
            unsafe extern "C" fn emit(p: *mut c_void, i: usize) -> u8 {
                let f = &mut *p.cast::<Fixture>();
                if f.fail_emit {
                    return 0;
                }
                f.breaks.push(i);
                1
            }
            Callbacks {
                context: (self as *mut Self).cast(),
                count,
                width,
                allows_break: allows,
                gap,
                ruby_start: start,
                ruby_end: end,
                split,
                emit,
            }
        }
    }
    #[test]
    fn dp_ties_choose_longer_lines_and_cost_cap_forces_single_word() {
        let mut f = Fixture::new(&[0, 0, 0]);
        assert!(unsafe { optimal(&f.callbacks(), &ALLOCATOR, 0, 0) });
        assert_eq!(f.breaks, [3]);
        let mut f = Fixture::new(&[1, 65535]);
        assert!(unsafe { optimal(&f.callbacks(), &ALLOCATOR, 65534, 0) });
        assert_eq!(f.breaks, [1, 2]);
    }
    #[test]
    fn dp_keeps_ruby_margin_and_attached_boundaries() {
        let mut f = Fixture::new(&[5, 5, 5]);
        f.attached = 1;
        assert!(unsafe { optimal(&f.callbacks(), &ALLOCATOR, 10, 0) });
        assert_eq!(f.breaks, [2, 3]);
        let mut f = Fixture::new(&[5, 5, 5]);
        f.ruby_end = 1;
        assert!(unsafe { optimal(&f.callbacks(), &ALLOCATOR, 10, 0) });
        assert_eq!(f.breaks, [1, 2, 3]);
    }
    #[test]
    fn greedy_bounded_split_leaves_later_words_untouched() {
        let mut f = Fixture::new(&[15, 20]);
        f.split = true;
        assert!(unsafe { greedy(&f.callbacks(), 10, 2, 1, false, true) });
        assert_eq!(f.widths, [8, 7, 20]);
        assert_eq!(f.breaks, [1]);
        assert_eq!(f.split_calls, [(0, 8, 1)]);
    }
    #[test]
    fn greedy_mid_line_retry_has_no_fallback_and_continuations_backtrack() {
        let mut f = Fixture::new(&[4, 12]);
        f.split = true;
        assert!(unsafe { greedy(&f.callbacks(), 10, 0, 1, true, true) });
        assert_eq!(f.split_calls, [(1, 6, 0)]);
        assert_eq!(f.breaks, [2]);
        let mut f = Fixture::new(&[4, 4, 4]);
        f.attached = 2;
        assert!(unsafe { greedy(&f.callbacks(), 10, 0, usize::MAX, false, false) });
        assert_eq!(f.breaks, [1, 3]);
    }
    #[test]
    fn failed_dp_allocation_releases_every_prior_array_and_emits_nothing() {
        for fail in 1..=2 {
            HEAP.with(|h| *h.borrow_mut() = (0, fail, 0));
            let mut f = Fixture::new(&[2, 3]);
            assert!(!unsafe { optimal(&f.callbacks(), &ALLOCATOR, 10, 0) });
            assert!(f.breaks.is_empty());
            HEAP.with(|h| assert_eq!(h.borrow().2, 0));
        }
        HEAP.with(|h| *h.borrow_mut() = (0, 0, 0));
        let mut f = Fixture::new(&[2, 3]);
        f.fail_emit = true;
        assert!(!unsafe { optimal(&f.callbacks(), &ALLOCATOR, 10, 0) });
        assert!(!unsafe { greedy(&f.callbacks(), 10, 0, 1, false, false) });
        HEAP.with(|h| assert_eq!(h.borrow().2, 0));
    }
}
