extern crate alloc;

use super::{log, BookCache, CacheGuard, File, Metadata, SizeWindow, Text, XpEpubLeaves};
use crate::book_limits::METADATA_WINDOW;
use alloc::vec::Vec;
use core::cell::UnsafeCell;

#[derive(Debug, PartialEq, Eq)]
pub(super) enum Error {
    Invalid,
    Io,
    OutOfMemory,
}

pub(super) struct Spine {
    pub href: Vec<u8>,
    pub cumulative: u32,
    pub toc_index: i16,
}

pub(super) struct Toc {
    pub title: Text,
    pub href: Vec<u8>,
    pub anchor: Vec<u8>,
    pub level: u8,
    pub spine_index: i16,
}

// Only unpublished loads or callers holding CacheGuard may use this cursor.
struct Cursor<'a> {
    file: &'a File,
    position: u32,
    limit: u32,
    failed: bool,
}

impl<'a> Cursor<'a> {
    fn new(file: &'a File) -> Self {
        Self {
            file,
            position: 0,
            limit: file.size(),
            failed: false,
        }
    }

    fn seek(&mut self, position: u32) -> bool {
        if self.failed || position > self.limit || !self.file.seek(position) {
            self.failed = true;
            return false;
        }
        self.position = position;
        true
    }

    fn read(&mut self, bytes: &mut [u8]) -> bool {
        if self.failed || bytes.len() > self.limit.saturating_sub(self.position) as usize {
            self.failed = true;
            return false;
        }
        let count = self.file.read_count(bytes);
        self.position += count as u32;
        self.failed = count != bytes.len();
        !self.failed
    }

    // A failed cursor stays failed; partially read values must never be published.
    fn u32(&mut self, value: &mut u32) -> bool {
        let mut bytes = value.to_le_bytes();
        let ok = self.read(&mut bytes);
        *value = u32::from_le_bytes(bytes);
        ok
    }

    fn u16(&mut self, value: &mut u16) {
        let mut bytes = value.to_le_bytes();
        self.read(&mut bytes);
        *value = u16::from_le_bytes(bytes);
    }

    fn i16(&mut self, value: &mut i16) {
        let mut bytes = value.to_le_bytes();
        self.read(&mut bytes);
        *value = i16::from_le_bytes(bytes);
    }

    fn string(&mut self, bytes: &mut Vec<u8>) -> Result<(), Error> {
        let mut length = 0;
        if !self.u32(&mut length) || length > self.limit.saturating_sub(self.position) {
            return Err(Error::Invalid);
        }
        let length = length as usize;
        bytes
            .try_reserve_exact(length.saturating_sub(bytes.len()))
            .map_err(|_| Error::OutOfMemory)?;
        bytes.resize(length, 0);
        if self.read(bytes) {
            Ok(())
        } else {
            Err(Error::Io)
        }
    }

    fn text(&mut self) -> Result<Text, Error> {
        let mut length = 0;
        if !self.u32(&mut length) || length > self.limit.saturating_sub(self.position) {
            return Err(Error::Invalid);
        }
        let text = Text::read(length as usize, |bytes| {
            self.read(bytes);
        })
        .map_err(|_| Error::OutOfMemory)?;
        if self.failed {
            Err(Error::Io)
        } else {
            Ok(text)
        }
    }

    fn spine(&mut self) -> Result<Spine, Error> {
        let mut row = Spine {
            href: Vec::new(),
            cumulative: 0,
            toc_index: -1,
        };
        self.string(&mut row.href)?;
        self.u32(&mut row.cumulative);
        self.i16(&mut row.toc_index);
        if self.failed {
            Err(Error::Io)
        } else {
            Ok(row)
        }
    }

    fn row(&mut self, lut: u32, index: u32) -> bool {
        let Some(position) = index
            .checked_mul(4)
            .and_then(|index| lut.checked_add(index))
        else {
            self.failed = true;
            return false;
        };
        let seek = self.seek(position);
        let mut offset = 0;
        let read = self.u32(&mut offset);
        let record = self.seek(offset);
        seek && read && record
    }

    // Validate strings without allocating storage for TOC rows during book open.
    fn skip_string(&mut self) -> bool {
        let mut length = 0;
        if !self.u32(&mut length) || length > self.limit.saturating_sub(self.position) {
            return false;
        }
        let mut buffer = [0; 128];
        while length != 0 {
            let count = length.min(buffer.len() as u32) as usize;
            if !self.read(&mut buffer[..count]) {
                return false;
            }
            length -= count as u32;
        }
        true
    }
}

impl BookCache {
    pub(super) fn new(leaves: &'static XpEpubLeaves) -> Self {
        Self {
            file: File {
                handle: None,
                leaves,
            },
            loaded: false,
            lut_offset: 0,
            spine_count: 0,
            toc_count: 0,
            total_size: 0,
            starts: Vec::new(),
            window: UnsafeCell::new(SizeWindow {
                first: None,
                sizes: [0; METADATA_WINDOW as usize],
            }),
        }
    }

    // The unpublished transaction exclusively owns these fields throughout load.
    pub(super) fn load(&mut self, metadata: &mut Metadata, path: &Text) -> Result<(), Error> {
        self.loaded = false;
        self.file.close();
        self.file = File::open(self.file.leaves, path, c"BMC").ok_or(Error::Io)?;
        let mut cursor = Cursor::new(&self.file);
        let mut version = [0];
        cursor.read(&mut version);
        if version != [10] {
            log::message(
                self.file.leaves,
                0,
                b"BMC",
                format_args!("Cache version mismatch: expected 10, got {}", version[0]),
            );
            self.file.close();
            return Err(Error::Invalid);
        }
        cursor.u32(&mut self.lut_offset);
        cursor.u16(&mut self.spine_count);
        cursor.u16(&mut self.toc_count);
        for bytes in [
            &mut metadata.title,
            &mut metadata.author,
            &mut metadata.language,
            &mut metadata.cover,
            &mut metadata.text_reference,
        ] {
            cursor.string(bytes)?;
        }
        self.starts.clear();
        self.total_size = 0;
        self.window.get_mut().first = None;
        self.starts
            .try_reserve_exact(u32::from(self.spine_count).div_ceil(METADATA_WINDOW) as usize)
            .map_err(|_| Error::OutOfMemory)?;
        let count = u32::from(self.spine_count) + u32::from(self.toc_count);
        let records = self
            .lut_offset
            .checked_add(count * 4)
            .ok_or(Error::Invalid)?;
        if cursor.failed || self.lut_offset != cursor.position || records > cursor.limit {
            return Err(Error::Invalid);
        }
        // Read the LUT and records forward once, without alternating SD sectors.
        let mut minimum = records;
        for index in 0..count {
            let mut offset = 0;
            let row_minimum = if index < u32::from(self.spine_count) {
                10
            } else {
                15
            };
            if !cursor.u32(&mut offset) || offset < minimum || (index == 0 && offset != records) {
                return Err(Error::Invalid);
            }
            minimum = offset.checked_add(row_minimum).ok_or(Error::Invalid)?;
            if minimum > cursor.limit {
                return Err(Error::Invalid);
            }
        }
        for index in 0..count {
            if index < u32::from(self.spine_count) {
                if index % METADATA_WINDOW == 0 {
                    self.starts.push(self.total_size);
                }
                self.total_size = cursor.spine()?.cumulative;
            } else if !cursor.skip_string()
                || !cursor.skip_string()
                || !cursor.skip_string()
                || !cursor.read(&mut [0; 3])
            {
                return Err(Error::Invalid);
            }
        }
        self.loaded = true;
        log::message(
            self.file.leaves,
            0,
            b"BMC",
            format_args!(
                "Loaded cache data: {} spine, {} TOC entries",
                self.spine_count, self.toc_count
            ),
        );
        Ok(())
    }

    pub(super) fn spine(&self, index: i32) -> Result<Spine, Error> {
        if !self.loaded {
            log::message(
                self.file.leaves,
                1,
                b"BMC",
                format_args!("getSpineEntry called but cache not loaded"),
            );
            return Err(Error::Invalid);
        }
        if index < 0 || index as u32 >= u32::from(self.spine_count) {
            log::message(
                self.file.leaves,
                1,
                b"BMC",
                format_args!("getSpineEntry index {} out of range", index),
            );
            return Err(Error::Invalid);
        }
        let _guard = CacheGuard::acquire(self.file.leaves);
        let mut cursor = Cursor::new(&self.file);
        cursor.row(self.lut_offset, index as u32);
        cursor.spine()
    }

    pub(super) fn toc(&self, index: i32) -> Result<Toc, Error> {
        if !self.loaded {
            log::message(
                self.file.leaves,
                1,
                b"BMC",
                format_args!("getTocEntry called but cache not loaded"),
            );
            return Err(Error::Invalid);
        }
        if index < 0 || index as u32 >= u32::from(self.toc_count) {
            log::message(
                self.file.leaves,
                1,
                b"BMC",
                format_args!("getTocEntry index {} out of range", index),
            );
            return Err(Error::Invalid);
        }
        let _guard = CacheGuard::acquire(self.file.leaves);
        let mut cursor = Cursor::new(&self.file);
        cursor.row(self.lut_offset, u32::from(self.spine_count) + index as u32);
        let mut row = Toc {
            title: Text::default(),
            href: Vec::new(),
            anchor: Vec::new(),
            level: 0,
            spine_index: -1,
        };
        row.title = cursor.text()?;
        cursor.string(&mut row.href)?;
        cursor.string(&mut row.anchor)?;
        let mut level = [row.level];
        cursor.read(&mut level);
        row.level = level[0];
        cursor.i16(&mut row.spine_index);
        if cursor.failed {
            Err(Error::Io)
        } else {
            Ok(row)
        }
    }

    // The caller holds CacheGuard. No mutable window borrow crosses file callbacks.
    fn fill_window(&self, first: u32) -> bool {
        unsafe { (*self.window.get()).first = None };
        let mut cursor = Cursor::new(&self.file);
        let mut position = 0;
        if !cursor.seek(self.lut_offset.wrapping_add(first.wrapping_mul(4)))
            || !cursor.u32(&mut position)
            || !cursor.seek(position)
        {
            return false;
        }
        for index in 0..METADATA_WINDOW.min(u32::from(self.spine_count) - first) {
            let mut length = 0;
            if !cursor.u32(&mut length) || !cursor.seek(cursor.position.wrapping_add(length)) {
                return false;
            }
            let mut size = unsafe { (*self.window.get()).sizes[index as usize] };
            let ok = cursor.u32(&mut size);
            unsafe { (*self.window.get()).sizes[index as usize] = size };
            if !ok || !cursor.seek(cursor.position.wrapping_add(2)) {
                return false;
            }
        }
        unsafe { (*self.window.get()).first = Some(first) };
        true
    }

    pub(super) fn cumulative(&self, index: i32) -> u32 {
        if !self.loaded || index < 0 || index as u32 >= u32::from(self.spine_count) {
            return 0;
        }
        let index = index as u32;
        if index == u32::from(self.spine_count) - 1 {
            return self.total_size;
        }
        let window = index / METADATA_WINDOW;
        let first = window * METADATA_WINDOW;
        if index - first == METADATA_WINDOW - 1 {
            return self.starts[window as usize + 1];
        }
        let _guard = CacheGuard::acquire(self.file.leaves);
        if unsafe { (*self.window.get()).first } != Some(first) && !self.fill_window(first) {
            return self.starts[window as usize];
        }
        unsafe { (*self.window.get()).sizes[(index - first) as usize] }
    }

    pub(super) fn spine_for_size(&self, size: u32) -> i32 {
        if !self.loaded || self.spine_count == 0 || size > self.total_size {
            return -1;
        }
        let window = self.starts[1..].partition_point(|&start| start < size);
        let first = window as u32 * METADATA_WINDOW;
        let last = u32::from(self.spine_count).min(first + METADATA_WINDOW) - 1;
        let _guard = CacheGuard::acquire(self.file.leaves);
        if unsafe { (*self.window.get()).first } != Some(first) && !self.fill_window(first) {
            return last as i32;
        }
        for index in first..last {
            if unsafe { (*self.window.get()).sizes[(index - first) as usize] } >= size {
                return index as i32;
            }
        }
        last as i32
    }
}

#[cfg(any(feature = "app-test", feature = "chapter-test"))]
#[path = "../test_support/book_cache.rs"]
mod tests;
