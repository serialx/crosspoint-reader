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
    pub title: Vec<u8>,
    pub href: Vec<u8>,
    pub anchor: Vec<u8>,
    pub level: u8,
    pub spine_index: i16,
}

// Only unpublished loads or callers holding CacheGuard may use this cursor.
struct Cursor<'a> {
    file: &'a File,
    position: u32,
}

impl<'a> Cursor<'a> {
    fn new(file: &'a File) -> Self {
        Self { file, position: 0 }
    }

    fn seek(&mut self, position: u32) -> bool {
        if !self.file.seek(position) {
            return false;
        }
        self.position = position;
        true
    }

    fn read(&mut self, bytes: &mut [u8]) -> bool {
        let count = self.file.read_count(bytes);
        self.position = self.position.wrapping_add(count as u32);
        count == bytes.len()
    }

    // Legacy short reads retain initialized destination bytes, including reuse.
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
        self.u32(&mut length);
        let length = length as usize;
        bytes
            .try_reserve_exact(length.saturating_sub(bytes.len()))
            .map_err(|_| Error::OutOfMemory)?;
        bytes.resize(length, 0);
        self.read(bytes);
        Ok(())
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
        Ok(row)
    }

    fn row(&mut self, lut: u32, index: u32) -> bool {
        let seek = self.seek(lut.wrapping_add(index.wrapping_mul(4)));
        let mut offset = 0;
        let read = self.u32(&mut offset);
        let record = self.seek(offset);
        seek && read && record
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
        cursor.seek(self.lut_offset.wrapping_add(count * 4));
        for index in 0..u32::from(self.spine_count) {
            if index % METADATA_WINDOW == 0 {
                self.starts.push(self.total_size);
            }
            self.total_size = cursor.spine()?.cumulative;
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
            title: Vec::new(),
            href: Vec::new(),
            anchor: Vec::new(),
            level: 0,
            spine_index: -1,
        };
        cursor.string(&mut row.title)?;
        cursor.string(&mut row.href)?;
        cursor.string(&mut row.anchor)?;
        let mut level = [row.level];
        cursor.read(&mut level);
        row.level = level[0];
        cursor.i16(&mut row.spine_index);
        Ok(row)
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

#[cfg(feature = "app-test")]
#[path = "../test_support/book_cache.rs"]
mod tests;
