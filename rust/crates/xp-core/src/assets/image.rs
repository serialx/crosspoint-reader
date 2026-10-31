//! Page-lifetime image cache owners. One read window is reused until release.
use crate::image::frame::{Target, Writer};
extern crate alloc;
#[cfg(target_arch = "riscv32")]
use alloc::alloc::{alloc_zeroed, dealloc, realloc};
use alloc::vec::Vec;
#[cfg(target_arch = "riscv32")]
use core::alloc::Layout;
use core::{ptr, slice};
#[cfg(not(target_arch = "riscv32"))]
type Buffer = super::memory::Buffer<u8>;

/// # Safety
/// Target callbacks lend exclusive writable bytes through the next target call.
/// Storage callbacks preserve that loan and never reenter the image state.
pub unsafe trait Services {
    fn open_cache(&mut self, path: &[u8]) -> bool;
    fn read(&mut self, bytes: &mut [u8]) -> usize;
    fn seek(&mut self, offset: usize);
    #[cfg(feature = "reader-strips")]
    fn seek_rows(&mut self, offset: usize) -> bool {
        self.seek(offset);
        true
    }
    fn size(&mut self) -> usize;
    fn close(&mut self);
    fn heap(&mut self) -> (usize, usize);
    fn target(&mut self) -> Target;
    fn extract(&mut self);
    fn source_size(&mut self) -> Option<usize>;
    fn decode(&mut self, cache: &[u8]) -> Option<bool>;
    fn diagnostic(&mut self, event: u32, a: i32, b: i32);
    #[cfg(feature = "strip-spike")]
    fn packed_override(
        &mut self,
        _target: &Target,
        _x: i32,
        _y: i32,
        _width: usize,
        _row: &[u8],
    ) -> bool {
        false
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Config {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub screen_width: i32,
    pub screen_height: i32,
    pub scanning: bool,
    pub intersects: bool,
}
const CHUNK: usize = 16384;
fn hash(path: &[u8]) -> u64 {
    path.iter().fold(14695981039346656037u64, |hash, b| {
        (hash ^ u64::from(*b)).wrapping_mul(1099511628211)
    })
}
pub fn cache_path(path: &[u8]) -> Option<Vec<u8>> {
    let end = path.iter().rposition(|b| *b == b'.').unwrap_or(path.len());
    let mut output = Vec::new();
    output.try_reserve_exact(end.checked_add(5)?).ok()?;
    output.extend_from_slice(&path[..end]);
    output.extend_from_slice(b".px2\0");
    Some(output)
}
#[cfg(target_arch = "riscv32")]
struct Buffer {
    data: *mut u8,
    len: usize,
}
#[cfg(target_arch = "riscv32")]
impl Buffer {
    const fn new() -> Self {
        Self {
            data: ptr::null_mut(),
            len: 0,
        }
    }
    fn grow(&mut self, len: usize) -> bool {
        if len <= self.len {
            return true;
        }
        let Ok(layout) = Layout::array::<u8>(len) else {
            return false;
        };
        // SAFETY: byte layouts retain alignment one and this owner releases the allocation.
        let data = unsafe {
            if self.data.is_null() {
                alloc_zeroed(layout)
            } else {
                realloc(
                    self.data,
                    Layout::from_size_align_unchecked(self.len, 1),
                    len,
                )
            }
        };
        if data.is_null() {
            return false;
        }
        if self.len != 0 {
            unsafe { ptr::write_bytes(data.add(self.len), 0, len - self.len) };
        }
        self.data = data;
        self.len = len;
        true
    }
    fn bytes(&self) -> &[u8] {
        if self.len == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(self.data, self.len) }
        }
    }
    fn bytes_mut(&mut self) -> &mut [u8] {
        if self.len == 0 {
            &mut []
        } else {
            unsafe { slice::from_raw_parts_mut(self.data, self.len) }
        }
    }
}
#[cfg(target_arch = "riscv32")]
impl Drop for Buffer {
    fn drop(&mut self) {
        if !self.data.is_null() {
            unsafe { dealloc(self.data, Layout::from_size_align_unchecked(self.len, 1)) };
        }
    }
}
pub struct State {
    #[cfg(feature = "reader-strips")]
    read_window: (*mut u8, usize),
    failures: [u64; 16],
    slot_hash: u64,
    chunks: [*mut u8; 6],
    work: Buffer,
    slot_width: u16,
    slot_height: u16,
    failure_count: u8,
    slot_attempted: bool,
}
// Bound the C3 cache/failure state; the row window is page-lifetime heap.
#[cfg(target_pointer_width = "32")]
const _: () = assert!(
    core::mem::size_of::<State>()
        == if cfg!(feature = "reader-strips") {
            184
        } else {
            176
        }
);
impl Default for State {
    fn default() -> Self {
        Self::new()
    }
}
impl State {
    pub const fn new() -> Self {
        Self {
            #[cfg(feature = "reader-strips")]
            read_window: (ptr::null_mut(), 0),
            failures: [0; 16],
            slot_hash: 0,
            chunks: [ptr::null_mut(); 6],
            work: Buffer::new(),
            slot_width: 0,
            slot_height: 0,
            failure_count: 0,
            slot_attempted: false,
        }
    }
    /// # Safety
    /// Non-null storage is exclusively borrowed until replaced by a null loan.
    /// It must remain valid across render/release calls and cannot alias the target.
    #[cfg(feature = "reader-strips")]
    pub unsafe fn set_read_window(&mut self, data: *mut u8, len: usize) {
        self.read_window = if data.is_null() || len > isize::MAX as usize {
            (ptr::null_mut(), 0)
        } else {
            (data, len)
        };
    }
    fn release_slot(&mut self) {
        #[cfg(target_arch = "riscv32")]
        let payload = (self.slot_width as usize).div_ceil(4) * self.slot_height as usize;
        // The C3 deallocator still needs the exact chunk layout.
        #[allow(clippy::unused_enumerate_index)]
        for (_i, chunk) in self.chunks.iter_mut().enumerate() {
            if !chunk.is_null() {
                #[cfg(target_arch = "riscv32")]
                let len = (payload - _i * CHUNK).min(CHUNK);
                // SAFETY: each non-null chunk owns exactly this validated payload range.
                #[cfg(target_arch = "riscv32")]
                unsafe {
                    dealloc(*chunk, Layout::from_size_align_unchecked(len, 1))
                };
                #[cfg(not(target_arch = "riscv32"))]
                unsafe {
                    super::memory::free(*chunk)
                };
                *chunk = ptr::null_mut();
            }
        }
        self.slot_hash = 0;
        self.slot_width = 0;
        self.slot_height = 0;
    }
    pub fn release(&mut self) {
        self.release_slot();
        self.work = Buffer::new();
        self.slot_attempted = false;
    }
    pub fn clear_failures(&mut self) {
        self.failure_count = 0;
    }
    pub fn failed(&self, path: &[u8]) -> bool {
        self.failures[..self.failure_count as usize].contains(&hash(path))
    }
    fn remember(&mut self, path: &[u8]) {
        if self.failure_count < 16 && !self.failed(path) {
            self.failures[self.failure_count as usize] = hash(path);
            self.failure_count += 1;
        }
    }
    fn header(s: &mut dyn Services, width: i32, height: i32) -> Option<(usize, usize)> {
        let mut header = [0; 4];
        if s.read(&mut header[..2]) != 2 || s.read(&mut header[2..]) != 2 {
            return None;
        }
        let w = u16::from_le_bytes([header[0], header[1]]) as usize;
        let h = u16::from_le_bytes([header[2], header[3]]) as usize;
        if w == 0
            || h == 0
            || (w as i64 - i64::from(width)).unsigned_abs() > 1
            || (h as i64 - i64::from(height)).unsigned_abs() > 1
            || s.size() < 4 + w.div_ceil(4) * h
        {
            return None;
        }
        Some((w, h))
    }
    pub fn valid(path: &[u8], width: i32, height: i32, s: &mut dyn Services) -> bool {
        let Some(cache) = cache_path(path) else {
            return false;
        };
        if !s.open_cache(&cache) {
            return false;
        }
        let valid = Self::header(s, width, height).is_some();
        s.close();
        valid
    }
    fn load(&mut self, hash: u64, w: usize, h: usize, s: &mut dyn Services) -> bool {
        self.release_slot();
        let stride = w.div_ceil(4);
        let mut remaining = stride * h;
        if stride > 208 || remaining == 0 || remaining.div_ceil(CHUNK) > 6 {
            return false;
        }
        self.slot_width = w as u16;
        self.slot_height = h as u16;
        for i in 0..remaining.div_ceil(CHUNK) {
            let want = remaining.min(CHUNK);
            let (free, largest) = s.heap();
            if free < remaining + 24576 || largest < want + 8192 {
                self.release_slot();
                return false;
            }
            let mut bytes = Buffer::new();
            if !bytes.grow(want) {
                self.release_slot();
                return false;
            }
            if s.read(bytes.bytes_mut()) != want {
                self.release_slot();
                return false;
            }
            self.chunks[i] = bytes.data;
            core::mem::forget(bytes);
            remaining -= want;
        }
        self.slot_hash = hash;
        self.slot_width = w as u16;
        self.slot_height = h as u16;
        true
    }
    fn packed(
        writer: &mut Writer,
        _target: &Target,
        geometry: (i32, i32, usize),
        bytes: &[u8],
        _services: &mut dyn Services,
    ) {
        let (x, y, w) = geometry;
        #[cfg(feature = "strip-spike")]
        if _services.packed_override(_target, x, y, w, bytes) {
            return;
        }
        writer.packed_row(x, y, w as i32, bytes);
    }
    fn slot(&self, x: i32, y: i32, s: &mut dyn Services) {
        let stride = (self.slot_width as usize).div_ceil(4);
        let mut row = [0; 208];
        // SAFETY: Services lends the exclusive target until the next target callback.
        let target = s.target();
        let mut writer = unsafe { Writer::new(target) };
        #[cfg(feature = "reader-strips")]
        let range = visible_rows(&target, y, self.slot_height as usize);
        #[cfg(not(feature = "reader-strips"))]
        let range = 0..self.slot_height as usize;
        for r in range {
            let start = r * stride;
            let chunk = start / CHUNK;
            let offset = start % CHUNK;
            let bytes = if offset + stride <= CHUNK {
                unsafe { slice::from_raw_parts(self.chunks[chunk].add(offset), stride) }
            } else {
                let first = CHUNK - offset;
                row[..first].copy_from_slice(unsafe {
                    slice::from_raw_parts(self.chunks[chunk].add(offset), first)
                });
                row[first..stride].copy_from_slice(unsafe {
                    slice::from_raw_parts(self.chunks[chunk + 1], stride - first)
                });
                &row[..stride]
            };
            Self::packed(
                &mut writer,
                &target,
                (x, y + r as i32, self.slot_width as usize),
                bytes,
                s,
            );
        }
    }
    fn cache(&mut self, path_len: usize, c: Config, s: &mut dyn Services) -> bool {
        let path_hash = hash(&self.work.bytes()[..path_len - 1]);
        if self.slot_hash == path_hash && self.slot_width != 0 {
            self.slot(c.x, c.y, s);
            return true;
        }
        if !s.open_cache(&self.work.bytes()[..path_len]) {
            return false;
        }
        let Some((w, h)) = Self::header(s, c.width, c.height) else {
            s.diagnostic(0, 0, 0);
            s.close();
            return false;
        };
        s.diagnostic(1, w as i32, h as i32);
        let stride = w.div_ceil(4);
        if !self.slot_attempted && {
            self.slot_attempted = true;
            self.load(path_hash, w, h, s)
        } {
            self.slot(c.x, c.y, s);
            s.diagnostic(2, 0, 0);
            s.close();
            return true;
        }
        s.seek(4);
        let desired = (4096 / stride).max(1).min(h) * stride;
        #[cfg(feature = "reader-strips")]
        let borrowed = !self.read_window.0.is_null() && self.read_window.1 >= stride;
        #[cfg(not(feature = "reader-strips"))]
        let borrowed = false;
        if !borrowed
            && self.work.len - path_len < stride
            && !self.work.grow(path_len + desired)
            && !self.work.grow(path_len + stride)
        {
            s.diagnostic(3, 0, 0);
            s.close();
            return false;
        }
        #[cfg(feature = "reader-strips")]
        let (data, capacity) = if borrowed {
            (self.read_window.0, self.read_window.1.min(desired))
        } else {
            (
                unsafe { self.work.data.add(path_len) },
                self.work.len - path_len,
            )
        };
        #[cfg(feature = "reader-strips")]
        let rows = (capacity / stride).min(h);
        #[cfg(not(feature = "reader-strips"))]
        let rows = ((self.work.len - path_len) / stride).min(h);
        // SAFETY: Services preserves the target loan during storage calls.
        let target = s.target();
        let mut writer = unsafe { Writer::new(target) };
        #[cfg(feature = "reader-strips")]
        let range = visible_rows(&target, c.y, h);
        #[cfg(not(feature = "reader-strips"))]
        let range = 0..h;
        #[cfg(feature = "reader-strips")]
        if !s.seek_rows(4 + range.start * stride) {
            s.close();
            return false;
        }
        let mut row = range.start;
        while row < range.end {
            let count = (range.end - row).min(rows);
            let bytes = count * stride;
            // SAFETY: owned work or the exclusive caller loan covers this batch.
            #[cfg(feature = "reader-strips")]
            let batch = unsafe { slice::from_raw_parts_mut(data, bytes) };
            #[cfg(not(feature = "reader-strips"))]
            let batch = &mut self.work.bytes_mut()[path_len..path_len + bytes];
            if s.read(batch) != bytes {
                s.diagnostic(4, row as i32, 0);
                s.close();
                return false;
            }
            for index in 0..count {
                Self::packed(
                    &mut writer,
                    &target,
                    (c.x, c.y + (row + index) as i32, w),
                    &batch[index * stride..(index + 1) * stride],
                    s,
                );
            }
            row += count;
        }
        s.diagnostic(5, 0, 0);
        s.close();
        true
    }
    /// 0: no pixels, 1: preserve decoded/cache pixels, 2: draw the existing placeholder.
    pub fn render(&mut self, path: &[u8], c: Config, s: &mut dyn Services) -> u32 {
        if c.scanning {
            return 0;
        }
        s.diagnostic(6, 0, 0);
        if c.x < 0
            || c.y < 0
            || c.x as i64 + c.width as i64 > c.screen_width as i64
            || c.y as i64 + c.height as i64 > c.screen_height as i64
        {
            s.diagnostic(7, 0, 0);
            return 0;
        }
        if !c.intersects {
            return 0;
        }
        if self.failed(path) {
            return 2;
        }
        let end = path.iter().rposition(|b| *b == b'.').unwrap_or(path.len());
        let Some(path_len) = end.checked_add(5) else {
            return 2;
        };
        if !self.work.grow(path_len) {
            s.diagnostic(3, 0, 0);
            self.remember(path);
            return 2;
        }
        self.work.bytes_mut()[..end].copy_from_slice(&path[..end]);
        self.work.bytes_mut()[end..path_len].copy_from_slice(b".px2\0");
        if self.cache(path_len, c, s) {
            return 1;
        }
        s.extract();
        let Some(size) = s.source_size() else {
            s.diagnostic(8, 0, 0);
            self.remember(path);
            return 2;
        };
        if size == 0 {
            s.diagnostic(9, 0, 0);
            self.remember(path);
            return 2;
        }
        s.diagnostic(10, 0, 0);
        match s.decode(&self.work.bytes()[..path_len]) {
            None => {
                s.diagnostic(11, 0, 0);
                self.remember(path);
                2
            }
            Some(false) => {
                s.diagnostic(12, 0, 0);
                self.remember(path);
                2
            }
            Some(true) => {
                s.diagnostic(13, 0, 0);
                1
            }
        }
    }
}

impl Drop for State {
    fn drop(&mut self) {
        self.release_slot();
    }
}

// Portrait bands intersect columns; only landscape bands can skip logical rows.
#[cfg(feature = "reader-strips")]
fn visible_rows(target: &Target, y: i32, height: usize) -> core::ops::Range<usize> {
    if target.rows >= target.height || !matches!(target.orientation, 1 | 3) {
        return 0..height;
    }
    let origin = i64::from(target.origin);
    let end = origin + i64::from(target.rows);
    let y = i64::from(y);
    let (first, last) = if target.orientation == 1 {
        (
            i64::from(target.height) - end - y,
            i64::from(target.height) - origin - y,
        )
    } else {
        (origin - y, end - y)
    };
    first.clamp(0, height as i64) as usize..last.clamp(0, height as i64) as usize
}

#[cfg(all(test, feature = "reader-strips"))]
mod strip_tests {
    use super::*;

    #[test]
    fn row_bounds_match_cpp_corner_intersections() {
        for height in [480, 528] {
            for orientation in 0..4 {
                for origin in (0..height).step_by(80) {
                    let target = Target {
                        data: ptr::null_mut(),
                        len: 0,
                        stride: 100,
                        origin,
                        rows: 80.min(height - origin),
                        width: 800,
                        height,
                        orientation,
                        mode: 0,
                        absolute: 0,
                    };
                    for y in [0, 20, 100, 400] {
                        for h in [1, 79, 80, 81, 380] {
                            let mut first = 0;
                            let mut end = h;
                            let intersects = |r: usize| {
                                let native = match orientation {
                                    1 => height - 1 - (y + r as i32),
                                    3 => y + r as i32,
                                    _ => return true,
                                };
                                native >= origin && native < origin + target.rows
                            };
                            while first < end && !intersects(first) {
                                first += 1;
                            }
                            while end > first && !intersects(end - 1) {
                                end -= 1;
                            }
                            // Empty ranges may have different endpoints but emit no rows.
                            assert_eq!(
                                visible_rows(&target, y, h).collect::<Vec<_>>(),
                                (first..end).collect::<Vec<_>>()
                            );
                        }
                    }
                }
            }
        }
    }
}
