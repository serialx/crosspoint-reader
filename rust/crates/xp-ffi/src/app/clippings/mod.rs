extern crate alloc;

mod codec;
pub mod io;
mod paths;
mod preview;
pub mod record;
mod sync;
mod transaction;

#[cfg(feature = "app-test")]
mod host;

use alloc::{alloc::alloc, boxed::Box, vec::Vec};
use core::{
    alloc::Layout,
    ptr::{self, NonNull},
};
use io::{File, Io};
use record::{Index, Record, PER_BLOCK, TEXT_MAX};
use transaction::Change;

pub type Result<T> = core::result::Result<T, ()>;

pub struct Store<I: Io, const BLOCKS: usize> {
    io: I,
    index: Index<BLOCKS>,
    count: usize,
    loaded: bool,
    dirty: bool,
    book: Vec<u8>,
    title: Vec<u8>,
    author: Vec<u8>,
    path: Vec<u8>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum AddResult {
    Added,
    LimitReached,
    SaveFailed,
}

fn copy(bytes: &[u8]) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    out.try_reserve_exact(bytes.len()).map_err(|_| ())?;
    out.extend_from_slice(bytes);
    Ok(out)
}

fn append(parts: &[&[u8]]) -> Result<Vec<u8>> {
    let length = parts
        .iter()
        .try_fold(0usize, |n, p| n.checked_add(p.len()))
        .ok_or(())?;
    let mut out = Vec::new();
    out.try_reserve_exact(length).map_err(|_| ())?;
    for part in parts {
        out.extend_from_slice(part);
    }
    Ok(out)
}

impl<I: Io, const BLOCKS: usize> Store<I, BLOCKS> {
    fn error(&self, arguments: core::fmt::Arguments<'_>, tail: &[&[u8]]) {
        self.io.log(1, arguments, tail);
    }

    fn reserve(&mut self, count: usize) -> bool {
        if self.index.reserve(count) {
            return true;
        }
        self.error(
            format_args!(
                "OOM: clipping block ({} bytes)",
                PER_BLOCK * core::mem::size_of::<Record>()
            ),
            &[],
        );
        false
    }

    pub fn try_new(io: I) -> Option<Box<Self>> {
        let pointer = NonNull::new(unsafe { alloc(Layout::new::<Self>()).cast::<Self>() })?;
        // No fallible step after allocation; initialize the header directly in its allocation.
        unsafe {
            let p = pointer.as_ptr();
            ptr::addr_of_mut!((*p).io).write(io);
            ptr::addr_of_mut!((*p).index).write(Index::new());
            ptr::addr_of_mut!((*p).count).write(0);
            ptr::addr_of_mut!((*p).loaded).write(false);
            ptr::addr_of_mut!((*p).dirty).write(false);
            ptr::addr_of_mut!((*p).book).write(Vec::new());
            ptr::addr_of_mut!((*p).title).write(Vec::new());
            ptr::addr_of_mut!((*p).author).write(Vec::new());
            ptr::addr_of_mut!((*p).path).write(Vec::new());
            Some(Box::from_raw(p))
        }
    }

    pub fn count(&self) -> usize {
        self.count
    }

    #[cfg(feature = "app-test")]
    pub(crate) fn seed_metadata(&mut self, rows: &[Record]) -> bool {
        if rows.len() > BLOCKS * PER_BLOCK || !self.index.reserve(rows.len()) {
            return false;
        }
        for (index, row) in rows.iter().enumerate() {
            *self.index.slot_mut(index) = *row;
        }
        self.count = rows.len();
        true
    }
    pub fn row(&self, index: usize) -> Option<&Record> {
        (index < self.count).then(|| self.index.slot(index))
    }

    pub fn load(&mut self, book: &[u8], title: &[u8], author: &[u8], kind: &[u8]) -> bool {
        self.dirty = false;
        self.loaded = false;
        self.index.clear();
        self.count = 0;
        self.path.clear();
        if kind != b"epub" || book.is_empty() {
            self.error(
                format_args!("Invalid clipping book type or path: "),
                &[kind],
            );
            return false;
        }
        let loaded = (|| {
            self.book = copy(book)?;
            self.title = copy(title)?;
            self.author = copy(author)?;
            self.path = self.store_path(book, b"epub")?;
            let backup = append(&[&self.path, b".bak"])?;
            if !self.io.exists(&self.path)
                && self.io.exists(&backup)
                && !self.io.rename(&backup, &self.path)
            {
                self.error(format_args!("Failed to recover clipping backup"), &[]);
                return Err(());
            }
            if !self.io.exists(&self.path) {
                return Ok(());
            }
            self.read_file()
        })()
        .is_ok();
        self.loaded = loaded;
        if !loaded {
            self.index.clear();
            self.count = 0;
        }
        loaded
    }

    #[cfg(feature = "app-test")]
    pub fn retained_string_capacities(&self) -> [usize; 4] {
        [
            self.book.capacity(),
            self.title.capacity(),
            self.author.capacity(),
            self.path.capacity(),
        ]
    }

    pub fn unload(&mut self) {
        if self.dirty {
            self.save();
        }
        self.index.clear();
        self.count = 0;
        self.loaded = false;
        self.book.clear();
        self.title.clear();
        self.author.clear();
        self.path.clear();
        self.dirty = false;
    }

    pub fn add(&mut self, record: Record, chapter: &[u8], text: &[u8]) -> AddResult {
        let pending = match self.prepare_add(record, chapter, text) {
            Ok(pending) => pending,
            Err(result) => return result,
        };
        let Ok(written) = self.persist(pending, Some(text)) else {
            return AddResult::SaveFailed;
        };
        self.publish_clean(written);
        AddResult::Added
    }

    pub(crate) fn prepare_add(
        &mut self,
        mut record: Record,
        chapter: &[u8],
        text: &[u8],
    ) -> core::result::Result<transaction::Pending, AddResult> {
        if self.count >= BLOCKS * PER_BLOCK {
            self.error(
                format_args!("Clipping limit ({}) reached", BLOCKS * PER_BLOCK),
                &[],
            );
            return Err(AddResult::LimitReached);
        }
        if !self.loaded || text.len() > TEXT_MAX || !self.reserve(self.count + 1) {
            self.error(
                format_args!(
                    "Cannot add clipping: invalid store, text length, or allocation failure"
                ),
                &[],
            );
            return Err(AddResult::SaveFailed);
        }
        record.page_count = record.page_count.max(1);
        let now = self.io.epoch_seconds();
        record.timestamp = if now > 1577836800 { now as u32 } else { 0 };
        let end = chapter
            .iter()
            .position(|&b| b == 0)
            .unwrap_or(chapter.len())
            .min(47);
        let end = xp_core::utf8::safe_truncate(&chapter[..end]);
        record.chapter = [0; 48];
        record.chapter[..end].copy_from_slice(&chapter[..end]);
        record.text_length = text.len() as u16;
        self.dirty = true;
        self.prepare_write(Change::Insert(record), Some(text))
            .map_err(|()| AddResult::SaveFailed)
    }

    pub fn save(&mut self) -> bool {
        if !self.loaded {
            return false;
        }
        if !self.dirty {
            return true;
        }
        if self.rewrite(Change::None, None).is_err() {
            return false;
        }
        self.dirty = false;
        true
    }

    pub fn read_text(&self, index: usize, out: &mut Vec<u8>) -> bool {
        let Some(row) = self.row(index) else {
            return false;
        };
        out.clear();
        if row.text_length == 0 {
            return true;
        }
        if self.path.is_empty() {
            return false;
        }
        let Some(mut file) = self.io.open_read(&self.path) else {
            return false;
        };
        if !file.seek(row.text_offset) {
            self.error(
                format_args!("Failed to seek clipping text at {}: ", row.text_offset),
                &[&self.path],
            );
            return false;
        }
        if out.try_reserve_exact(row.text_length as usize).is_err() {
            return false;
        }
        out.resize(row.text_length as usize, 0);
        if file.read(out) != row.text_length as i32 {
            self.error(
                format_args!("Failed to read clipping text at {}: ", row.text_offset),
                &[&self.path],
            );
            out.clear();
            return false;
        }
        true
    }

    pub fn read_into(&self, index: usize, out: &mut [u8]) -> Option<usize> {
        let row = self.row(index)?;
        let length = row.text_length as usize;
        let out = out.get_mut(..length)?;
        if length == 0 {
            return Some(0);
        }
        if self.path.is_empty() {
            return None;
        }
        let mut file = self.io.open_read(&self.path)?;
        if !file.seek(row.text_offset) {
            self.error(
                format_args!("Failed to seek clipping text at {}: ", row.text_offset),
                &[&self.path],
            );
            return None;
        }
        if file.read(out) != length as i32 {
            self.error(
                format_args!("Failed to read clipping text at {}: ", row.text_offset),
                &[&self.path],
            );
            return None;
        }
        Some(length)
    }

    fn store_path(&self, book: &[u8], kind: &[u8]) -> Result<Vec<u8>> {
        let mut hash = self.io.path_hash(book);
        let mut digits = [0; 20];
        let mut start = digits.len();
        loop {
            start -= 1;
            digits[start] = b'0' + (hash % 10) as u8;
            hash /= 10;
            if hash == 0 {
                break;
            }
        }
        append(&[
            b"/.crosspoint/clippings/",
            kind,
            b"_",
            &digits[start..],
            b".bin",
        ])
    }
}
