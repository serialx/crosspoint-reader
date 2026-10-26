//! Book v10 build records. The sink owns files and the existing buffered streams.
extern crate alloc;
use super::Result;
use alloc::alloc::{alloc, dealloc};
use alloc::vec::Vec;
use core::ffi::c_void;
use core::{alloc::Layout, ptr::NonNull};

pub const ITEMS: u32 = 0;
pub const SPINE: u32 = 1;
pub const TOC: u32 = 2;
pub const BOOK: u32 = 3;
pub const OPEN_READ: u32 = 0;
pub const OPEN_WRITE: u32 = 1;
pub const CLOSE: u32 = 2;
pub const REMOVE: u32 = 3;
pub const IS_OPEN: u32 = 4;
pub const POSITION: u32 = 5;
pub const SIZE: u32 = 6;
pub const BEGIN_PASS: u32 = 7;
pub const END_PASS: u32 = 8;
pub const BEGIN_BUILD: u32 = 9;
pub const END_BUILD: u32 = 10;
pub const BEGIN_SIZES: u32 = 11;
pub const END_SIZES: u32 = 12;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Span {
    pub data: *const u8,
    pub len: usize,
}
impl Span {
    pub fn new(bytes: &[u8]) -> Self {
        Self {
            data: bytes.as_ptr(),
            len: bytes.len(),
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Io {
    pub context: *mut c_void,
    pub control: unsafe extern "C" fn(*mut c_void, u32, u32) -> u32,
    pub read: unsafe extern "C" fn(*mut c_void, u32, *mut u8, usize) -> usize,
    pub write: unsafe extern "C" fn(*mut c_void, u32, *const u8, usize) -> usize,
    pub seek: unsafe extern "C" fn(*mut c_void, u32, u32) -> u8,
    pub item_size: Option<unsafe extern "C" fn(*mut c_void, u16, Span) -> u32>,
    // Optional synchronous observations for host oracles; never reenter the writer.
    pub observe: Option<unsafe extern "C" fn(*mut c_void, u32, Span, Span, Span, u8)>,
}
impl Io {
    pub fn control(&self, file: u32, operation: u32) -> u32 {
        unsafe { (self.control)(self.context, file, operation) }
    }
    pub fn seek(&self, file: u32, pos: u32) -> Result<()> {
        if unsafe { (self.seek)(self.context, file, pos) } != 0 {
            Ok(())
        } else {
            Err(2)
        }
    }
    pub fn read(&self, file: u32, bytes: &mut [u8]) -> Result<()> {
        if unsafe { (self.read)(self.context, file, bytes.as_mut_ptr(), bytes.len()) }
            == bytes.len()
        {
            Ok(())
        } else {
            Err(2)
        }
    }
    pub fn write(&self, file: u32, bytes: &[u8]) -> Result<()> {
        if unsafe { (self.write)(self.context, file, bytes.as_ptr(), bytes.len()) } == bytes.len() {
            Ok(())
        } else {
            Err(2)
        }
    }
    pub fn string(&self, file: u32, bytes: &[u8]) -> Result<()> {
        self.write(file, &(bytes.len() as u32).to_le_bytes())?;
        self.write(file, bytes)
    }
    pub fn u32(&self, file: u32) -> Result<u32> {
        let mut value = [0; 4];
        self.read(file, &mut value)?;
        Ok(u32::from_le_bytes(value))
    }
    pub fn read_string(&self, file: u32, value: &mut Scratch) -> Result<()> {
        let size = self.u32(file)? as usize;
        value.resize(size)?;
        self.read(file, value.as_mut_slice())
    }
    #[cfg(feature = "package-trace")]
    fn observe(&self, kind: u32, title: &[u8], href: &[u8], anchor: &[u8], level: u8) {
        if let Some(f) = self.observe {
            unsafe {
                f(
                    self.context,
                    kind,
                    Span::new(title),
                    Span::new(href),
                    Span::new(anchor),
                    level,
                )
            }
        }
    }
    #[cfg(not(feature = "package-trace"))]
    fn observe(&self, _: u32, _: &[u8], _: &[u8], _: &[u8], _: u8) {}
}

// Match the C++ file reader's 15-byte small-string storage.
#[derive(Default)]
pub struct Scratch {
    inline: [u8; 16],
    heap: Vec<u8>,
}
impl Scratch {
    fn resize(&mut self, len: usize) -> Result<()> {
        if len < 16 && self.heap.capacity() == 0 {
            *self.inline.last_mut().ok_or(4u32)? = len as u8;
        } else {
            self.heap.clear();
            self.heap.try_reserve_exact(len).map_err(|_| 1u32)?;
            self.heap.resize(len, 0);
            *self.inline.last_mut().ok_or(4u32)? = 0;
        }
        Ok(())
    }
    pub fn set(&mut self, value: &[u8]) -> Result<()> {
        self.resize(value.len())?;
        self.as_mut_slice().copy_from_slice(value);
        Ok(())
    }
    pub fn as_slice(&self) -> &[u8] {
        if self.heap.capacity() != 0 {
            &self.heap
        } else {
            self.inline
                .get(..usize::from(self.inline.last().copied().unwrap_or(0)))
                .unwrap_or(&[])
        }
    }
    fn as_mut_slice(&mut self) -> &mut [u8] {
        if self.heap.capacity() != 0 {
            &mut self.heap
        } else {
            let len = usize::from(self.inline.last().copied().unwrap_or(0));
            self.inline.get_mut(..len).unwrap_or(&mut [])
        }
    }
}
impl core::ops::Deref for Scratch {
    type Target = [u8];
    fn deref(&self) -> &[u8] {
        self.as_slice()
    }
}

// Only package index/mapping records belong here. Copy permits raw reads and drop
// without element destructors; each nonzero record must fit the 512-byte budget.
mod node_element {
    pub trait Sealed: Copy {}
    impl Sealed for super::super::facade::Item {}
    impl Sealed for super::HrefIndex {}
    impl Sealed for i16 {}
}

// Match the old deque payloads: 42 Items = 504 B, 32 HrefIndexes = 512 B,
// 256 i16 mappings = 512 B. Nodes avoid a book-sized contiguous allocation.
pub(super) struct Nodes<T: node_element::Sealed> {
    nodes: Vec<NonNull<T>>,
    len: usize,
}
impl<T: node_element::Sealed> Default for Nodes<T> {
    fn default() -> Self {
        Self {
            nodes: Vec::new(),
            len: 0,
        }
    }
}
impl<T: node_element::Sealed> Nodes<T> {
    const CAP: usize = {
        let size = core::mem::size_of::<T>();
        assert!(size > 0 && size <= 512);
        512 / size
    };
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    pub fn push(&mut self, item: T) -> Result<()> {
        if self.len.is_multiple_of(Self::CAP) {
            self.nodes.try_reserve(1).map_err(|_| 1u32)?;
            let layout = Layout::array::<T>(Self::CAP).map_err(|_| 1u32)?;
            // Nodes owns each checked allocation until drop; initialized entries are Copy.
            let node = NonNull::new(unsafe { alloc(layout) }.cast::<T>()).ok_or(1u32)?;
            self.nodes.push(node);
        }
        let node = self.nodes.last().ok_or(4u32)?;
        unsafe {
            node.as_ptr().add(self.len % Self::CAP).write(item);
        }
        self.len += 1;
        Ok(())
    }
    pub fn get(&self, i: usize) -> Result<T> {
        if i >= self.len {
            return Err(4);
        }
        let node = self.nodes.get(i / Self::CAP).ok_or(4u32)?;
        Ok(unsafe { node.as_ptr().add(i % Self::CAP).read() })
    }
    pub fn set(&mut self, i: usize, value: T) -> Result<()> {
        if i >= self.len {
            return Err(4);
        }
        let node = self.nodes.get(i / Self::CAP).ok_or(4u32)?;
        unsafe {
            node.as_ptr().add(i % Self::CAP).write(value);
        }
        Ok(())
    }
    fn swap(&mut self, a: usize, b: usize) -> Result<()> {
        let value = self.get(a)?;
        self.set(a, self.get(b)?)?;
        self.set(b, value)
    }
    pub fn lower_bound(&self, value: T, less: fn(T, T) -> bool) -> Result<usize> {
        let (mut first, mut count) = (0, self.len);
        while count != 0 {
            let step = count / 2;
            let middle = first + step;
            if less(self.get(middle)?, value) {
                first = middle + 1;
                count -= step + 1;
            } else {
                count = step;
            }
        }
        Ok(first)
    }
    pub fn sort(&mut self, less: fn(T, T) -> bool) -> Result<()> {
        // Frozen parity with the 8f71e528 C++ writer's GCC 14.2 libstdc++ sort:
        // equal keys select duplicate manifest ids and repeated indexed spine hrefs.
        // Do not track later library sorts. An explicit tie-break rule needs a
        // separately approved behavior change and updated package goldens/tie test.
        #[inline(never)]
        fn partition<T: node_element::Sealed>(
            a: &mut Nodes<T>,
            first: usize,
            last: usize,
            less: fn(T, T) -> bool,
        ) -> Result<usize> {
            let (left, middle, right) = (first + 1, first + (last - first) / 2, last - 1);
            let (x, y, z) = (a.get(left)?, a.get(middle)?, a.get(right)?);
            let median = if less(x, y) {
                if less(y, z) {
                    middle
                } else if less(x, z) {
                    right
                } else {
                    left
                }
            } else if less(x, z) {
                left
            } else if less(y, z) {
                right
            } else {
                middle
            };
            a.swap(first, median)?;
            let pivot = a.get(first)?;
            let (mut i, mut j) = (first + 1, last);
            loop {
                while less(a.get(i)?, pivot) {
                    i += 1;
                }
                j -= 1;
                while less(pivot, a.get(j)?) {
                    j -= 1;
                }
                if i >= j {
                    return Ok(i);
                }
                a.swap(i, j)?;
                i += 1;
            }
        }
        fn adjust<T: node_element::Sealed>(
            a: &mut Nodes<T>,
            base: usize,
            mut hole: usize,
            len: usize,
            value: T,
            less: fn(T, T) -> bool,
        ) -> Result<()> {
            let top = hole;
            let mut child = hole;
            while child < (len - 1) / 2 {
                child = 2 * (child + 1);
                if less(a.get(base + child)?, a.get(base + child - 1)?) {
                    child -= 1;
                }
                a.set(base + hole, a.get(base + child)?)?;
                hole = child;
            }
            if len.is_multiple_of(2) && child == (len - 2) / 2 {
                child = 2 * (child + 1);
                a.set(base + hole, a.get(base + child - 1)?)?;
                hole = child - 1;
            }
            while hole > top {
                let parent = (hole - 1) / 2;
                if !less(a.get(base + parent)?, value) {
                    break;
                }
                a.set(base + hole, a.get(base + parent)?)?;
                hole = parent;
            }
            a.set(base + hole, value)
        }
        fn intro<T: node_element::Sealed>(
            a: &mut Nodes<T>,
            mut first: usize,
            mut last: usize,
            mut depth: usize,
            less: fn(T, T) -> bool,
        ) -> Result<()> {
            while last - first > 16 {
                if depth == 0 {
                    let len = last - first;
                    for root in (0..len / 2).rev() {
                        adjust(a, first, root, len, a.get(first + root)?, less)?;
                    }
                    for end in (1..len).rev() {
                        let value = a.get(first + end)?;
                        a.set(first + end, a.get(first)?)?;
                        if end > 1 {
                            adjust(a, first, 0, end, value, less)?;
                        } else {
                            a.set(first, value)?;
                        }
                    }
                    return Ok(());
                }
                depth -= 1;
                let cut = partition(a, first, last, less)?;
                // Recurse into the smaller partition to bound stack depth by log2(len).
                if cut - first < last - cut {
                    intro(a, first, cut, depth, less)?;
                    first = cut;
                } else {
                    intro(a, cut, last, depth, less)?;
                    last = cut;
                }
            }
            Ok(())
        }
        if self.len == 0 {
            return Ok(());
        }
        intro(
            self,
            0,
            self.len,
            2 * (usize::BITS - 1 - self.len.leading_zeros()) as usize,
            less,
        )?;
        for i in 1..self.len {
            let value = self.get(i)?;
            let mut j = i;
            while j > 0 && less(value, self.get(j - 1)?) {
                self.set(j, self.get(j - 1)?)?;
                j -= 1;
            }
            self.set(j, value)?;
        }
        Ok(())
    }
}
impl<T: node_element::Sealed> Drop for Nodes<T> {
    fn drop(&mut self) {
        if let Ok(layout) = Layout::array::<T>(Self::CAP) {
            for node in &self.nodes {
                unsafe {
                    dealloc(node.as_ptr().cast(), layout);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{href_less, HrefIndex, Nodes};

    #[test]
    fn duplicate_key_order_matches_legacy_partition() {
        let mut nodes = Nodes::default();
        // Seventeen equal keys cross the insertion-only threshold. Payloads must
        // retain the GCC 14.2 libstdc++ permutation, not insertion or spine order.
        for spine in 0..17 {
            nodes
                .push(HrefIndex {
                    hash: 7,
                    len: 3,
                    spine,
                })
                .unwrap();
        }
        nodes.sort(href_less).unwrap();
        let expected = [8, 16, 15, 14, 13, 12, 11, 10, 9, 0, 7, 6, 5, 4, 3, 2, 1];
        for (index, spine) in expected.into_iter().enumerate() {
            assert_eq!(nodes.get(index).unwrap().spine, spine);
        }
        let first = nodes
            .lower_bound(
                HrefIndex {
                    hash: 7,
                    len: 3,
                    spine: 0,
                },
                href_less,
            )
            .unwrap();
        assert_eq!(nodes.get(first).unwrap().spine, 8);
    }
}

const _: () = {
    assert!(core::mem::size_of::<super::facade::Item>() == 12);
    assert!(core::mem::size_of::<HrefIndex>() == 16);
    assert!(core::mem::size_of::<i16>() == 2);
};

#[derive(Clone, Copy)]
struct HrefIndex {
    hash: u64,
    len: u16,
    spine: i16,
}
#[inline(never)]
fn href_less(a: HrefIndex, b: HrefIndex) -> bool {
    (a.hash, a.len) < (b.hash, b.len)
}
pub struct Cache {
    pub spine_count: u16,
    pub toc_count: u16,
    pub building: bool,
    index: Nodes<HrefIndex>,
}
impl Default for Cache {
    fn default() -> Self {
        Self {
            spine_count: 0,
            toc_count: 0,
            building: true,
            index: Nodes::default(),
        }
    }
}
fn read_spine(io: &Io, href: &mut Scratch) -> Result<(u32, i16)> {
    io.read_string(SPINE, href)?;
    let size = io.u32(SPINE)?;
    let mut index = [0; 2];
    io.read(SPINE, &mut index)?;
    Ok((size, i16::from_le_bytes(index)))
}
fn read_toc(
    io: &Io,
    title: &mut Scratch,
    href: &mut Scratch,
    anchor: &mut Scratch,
) -> Result<(u8, i16)> {
    io.read_string(TOC, title)?;
    io.read_string(TOC, href)?;
    io.read_string(TOC, anchor)?;
    let mut level = [0];
    io.read(TOC, &mut level)?;
    let mut index = [0; 2];
    io.read(TOC, &mut index)?;
    Ok((
        level.first().copied().unwrap_or(0),
        i16::from_le_bytes(index),
    ))
}
fn spine(io: &Io, file: u32, href: &[u8], size: u32, index: i16) -> Result<()> {
    io.string(file, href)?;
    io.write(file, &size.to_le_bytes())?;
    io.write(file, &index.to_le_bytes())
}
fn toc(
    io: &Io,
    file: u32,
    title: &[u8],
    href: &[u8],
    anchor: &[u8],
    level: u8,
    index: i16,
) -> Result<()> {
    io.string(file, title)?;
    io.string(file, href)?;
    io.string(file, anchor)?;
    io.write(file, &[level])?;
    io.write(file, &index.to_le_bytes())
}
impl Cache {
    pub fn begin_opf(&mut self, io: &Io) -> Result<()> {
        if io.control(SPINE, OPEN_WRITE) == 0 {
            return Err(2);
        }
        io.control(SPINE, BEGIN_PASS);
        Ok(())
    }
    pub fn end_opf(&mut self, io: &Io) -> Result<()> {
        let ok = io.control(SPINE, END_PASS) != 0;
        io.control(SPINE, CLOSE);
        if ok {
            Ok(())
        } else {
            Err(2)
        }
    }
    pub fn create_spine(&mut self, io: &Io, href: &[u8]) -> Result<()> {
        io.observe(0, &[], href, &[], 0);
        if !self.building || io.control(SPINE, IS_OPEN) == 0 {
            return Ok(());
        }
        spine(io, SPINE, href, 0, -1)?;
        self.spine_count = self.spine_count.wrapping_add(1);
        Ok(())
    }
    pub fn begin_toc(&mut self, io: &Io) -> Result<()> {
        if io.control(SPINE, OPEN_READ) == 0 {
            return Err(2);
        }
        if io.control(TOC, OPEN_WRITE) == 0 {
            io.control(SPINE, CLOSE);
            return Err(2);
        }
        self.index = Nodes::default();
        if self.spine_count >= 400 {
            io.seek(SPINE, 0)?;
            for i in 0..self.spine_count {
                let mut href = Scratch::default();
                read_spine(io, &mut href)?;
                if self
                    .index
                    .push(HrefIndex {
                        hash: crate::fnv1a64(&href),
                        len: href.len() as u16,
                        spine: i as i16,
                    })
                    .is_err()
                {
                    self.index = Nodes::default();
                    break;
                }
            }
            self.index.sort(href_less)?;
            io.seek(SPINE, 0)?;
        }
        io.control(TOC, BEGIN_PASS);
        Ok(())
    }
    pub fn create_toc(
        &mut self,
        io: &Io,
        title: &[u8],
        href: &[u8],
        anchor: &[u8],
        level: u8,
    ) -> Result<()> {
        io.observe(1, title, href, anchor, level);
        if !self.building || io.control(TOC, IS_OPEN) == 0 || io.control(SPINE, IS_OPEN) == 0 {
            return Err(2);
        }
        let mut index = -1;
        if !self.index.is_empty() {
            let target = HrefIndex {
                hash: crate::fnv1a64(href),
                len: href.len() as u16,
                spine: 0,
            };
            let at = self.index.lower_bound(target, href_less)?;
            if let Ok(found) = self.index.get(at) {
                if found.hash == target.hash && found.len == target.len {
                    index = found.spine;
                }
            }
        } else {
            io.seek(SPINE, 0)?;
            for i in 0..self.spine_count {
                let mut stored = Scratch::default();
                read_spine(io, &mut stored)?;
                if stored.as_slice() == href {
                    index = i as i16;
                    break;
                }
            }
        }
        let mut normalized = Vec::new();
        let title = if crate::utf8::may_compose(title) {
            let mut count = Some(0usize);
            crate::utf8::compose(
                title,
                &mut |b| count = count.and_then(|n| n.checked_add(b.len())),
                &mut |_| true,
            );
            normalized
                .try_reserve_exact(count.ok_or(1u32)?)
                .map_err(|_| 1u32)?;
            crate::utf8::compose(title, &mut |b| normalized.extend_from_slice(b), &mut |_| {
                true
            });
            normalized.as_slice()
        } else {
            title
        };
        toc(io, TOC, title, href, anchor, level, index)?;
        self.toc_count = self.toc_count.wrapping_add(1);
        Ok(())
    }
    pub fn end_toc(&mut self, io: &Io) -> Result<()> {
        let ok = io.control(TOC, END_PASS) != 0;
        io.control(TOC, CLOSE);
        io.control(SPINE, CLOSE);
        self.index = Nodes::default();
        if ok {
            Ok(())
        } else {
            Err(2)
        }
    }
    pub fn build(&mut self, io: &Io, metadata: [&[u8]; 5]) -> Result<()> {
        if io.control(BOOK, OPEN_WRITE) == 0 {
            return Err(2);
        }
        if io.control(SPINE, OPEN_READ) == 0 {
            io.control(BOOK, CLOSE);
            return Err(2);
        }
        if io.control(TOC, OPEN_READ) == 0 {
            io.control(BOOK, CLOSE);
            io.control(SPINE, CLOSE);
            return Err(2);
        }
        io.control(BOOK, BEGIN_BUILD);
        let result = self.build_records(io, metadata);
        let flushed = io.control(BOOK, END_BUILD) != 0;
        io.control(BOOK, CLOSE);
        io.control(SPINE, CLOSE);
        io.control(TOC, CLOSE);
        if result.is_err() || !flushed {
            io.control(BOOK, REMOVE);
        }
        result?;
        if flushed {
            Ok(())
        } else {
            Err(2)
        }
    }
    fn build_records(&self, io: &Io, metadata: [&[u8]; 5]) -> Result<()> {
        let lut_offset = metadata
            .iter()
            .fold(29u32, |sum, v| sum.wrapping_add(v.len() as u32));
        let lut_size = (u32::from(self.spine_count) + u32::from(self.toc_count)) * 4;
        io.write(BOOK, &[10])?;
        io.write(BOOK, &lut_offset.to_le_bytes())?;
        io.write(BOOK, &self.spine_count.to_le_bytes())?;
        io.write(BOOK, &self.toc_count.to_le_bytes())?;
        for field in metadata {
            io.string(BOOK, field)?;
        }
        io.seek(SPINE, 0)?;
        for _ in 0..self.spine_count {
            let pos = io.control(SPINE, POSITION);
            read_spine(io, &mut Scratch::default())?;
            io.write(
                BOOK,
                &pos.wrapping_add(lut_offset)
                    .wrapping_add(lut_size)
                    .to_le_bytes(),
            )?;
        }
        let spine_bytes = io.control(SPINE, POSITION);
        io.seek(TOC, 0)?;
        for _ in 0..self.toc_count {
            let pos = io.control(TOC, POSITION);
            read_toc(
                io,
                &mut Scratch::default(),
                &mut Scratch::default(),
                &mut Scratch::default(),
            )?;
            io.write(
                BOOK,
                &pos.wrapping_add(lut_offset)
                    .wrapping_add(lut_size)
                    .wrapping_add(spine_bytes)
                    .to_le_bytes(),
            )?;
        }
        let mut mapping = Nodes::default();
        for _ in 0..self.spine_count {
            mapping.push(-1i16)?;
        }
        io.seek(TOC, 0)?;
        for j in 0..self.toc_count {
            let (_, index) = read_toc(
                io,
                &mut Scratch::default(),
                &mut Scratch::default(),
                &mut Scratch::default(),
            )?;
            if index >= 0 && (index as u16) < self.spine_count && mapping.get(index as usize)? == -1
            {
                mapping.set(index as usize, j as i16)?;
            }
        }
        if io.control(SPINE, BEGIN_SIZES) == 0 {
            return Err(2);
        }
        let result: Result<()> = (|| {
            io.seek(SPINE, 0)?;
            let mut cumulative = 0u32;
            let mut previous = -1;
            for i in 0..self.spine_count {
                let mut href = Scratch::default();
                read_spine(io, &mut href)?;
                let mut index = mapping.get(i as usize)?;
                if index == -1 {
                    index = previous;
                }
                previous = index;
                cumulative = cumulative.wrapping_add(unsafe {
                    io.item_size
                        .map_or(0, |f| f(io.context, i, Span::new(&href)))
                });
                spine(io, BOOK, &href, cumulative, index)?;
            }
            Ok(())
        })();
        io.control(SPINE, END_SIZES);
        result?;
        io.seek(TOC, 0)?;
        for _ in 0..self.toc_count {
            let (mut title, mut href, mut anchor) =
                (Scratch::default(), Scratch::default(), Scratch::default());
            let (level, index) = read_toc(io, &mut title, &mut href, &mut anchor)?;
            toc(io, BOOK, &title, &href, &anchor, level, index)?;
        }
        Ok(())
    }
}
