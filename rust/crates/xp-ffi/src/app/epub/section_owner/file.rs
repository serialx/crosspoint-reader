extern crate alloc;

use super::super::{
    abi::XpEpubLeaves,
    book_owner::{log, FileSlot},
    Text,
};
use crate::{
    app::abi,
    section_cache::{self, Diagnostic},
};
use alloc::vec::Vec;
use core::ops::{Deref, DerefMut};

enum Cursor<'a> {
    Owned(FileSlot),
    Borrowed(&'a mut FileSlot),
}
impl Deref for Cursor<'_> {
    type Target = FileSlot;
    fn deref(&self) -> &FileSlot {
        match self {
            Self::Owned(file) => file,
            Self::Borrowed(file) => file,
        }
    }
}
impl DerefMut for Cursor<'_> {
    fn deref_mut(&mut self) -> &mut FileSlot {
        match self {
            Self::Owned(file) => file,
            Self::Borrowed(file) => file,
        }
    }
}
enum Temporary<'a> {
    Owned(Text),
    Borrowed(&'a Text),
}
impl Temporary<'_> {
    fn wire(&self) -> abi::Text {
        match self {
            Self::Owned(path) => path.wire(),
            Self::Borrowed(path) => path.wire(),
        }
    }
}

pub(super) struct CacheFile<'a> {
    file: Cursor<'a>,
    path: &'a Text,
    temporary: Option<Temporary<'a>>,
    text: Vec<u8>,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
}

impl<'a> CacheFile<'a> {
    pub(super) fn new(
        path: &'a Text,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> Self {
        Self {
            file: Cursor::Owned(FileSlot::new()),
            path,
            temporary: None,
            text: Vec::new(),
            resources,
            leaves,
        }
    }

    pub(super) fn writing(
        path: &'a Text,
        temporary: &'a Text,
        file: &'a mut FileSlot,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> Self {
        Self {
            file: Cursor::Borrowed(file),
            path,
            temporary: Some(Temporary::Borrowed(temporary)),
            text: Vec::new(),
            resources,
            leaves,
        }
    }

    // Prepare the only derived path before any mutating cache operation.
    pub(super) fn prepare_temporary(&mut self) -> Result<(), ()> {
        if self.temporary.is_none() {
            let mut path = Text::default();
            path.assign_parts(&[self.path.bytes(), b".part"])
                .map_err(|_| ())?;
            self.temporary = Some(Temporary::Owned(path));
        }
        Ok(())
    }

    pub(super) fn open_write(&mut self) -> bool {
        let path = match self.temporary.as_ref().expect("prepared cache writer path") {
            Temporary::Owned(path) => path,
            Temporary::Borrowed(path) => path,
        };
        self.file.open(self.leaves, path, true, c"SCT")
    }
}

impl section_cache::Reader for CacheFile<'_> {
    fn open_read(&mut self) -> bool {
        self.file.open(self.leaves, self.path, false, c"SCT")
    }
    fn read(&mut self, output: &mut [u8]) -> bool {
        let count = unsafe { self.file.read(output.as_mut_ptr(), output.len()) };
        usize::try_from(count).is_ok_and(|count| count == output.len())
    }
    fn seek(&mut self, position: u32) -> bool {
        self.file.seek(position)
    }
    fn position(&mut self) -> u32 {
        self.file.position()
    }
    fn size(&mut self) -> u32 {
        self.file.size()
    }
    fn text_matches(&mut self, length: u32, needle: &[u8]) -> Option<bool> {
        let length = length as usize;
        self.text
            .try_reserve_exact(length.saturating_sub(self.text.len()))
            .ok()?;
        self.text.resize(length, 0);
        if length != 0 {
            let count = unsafe { self.file.read(self.text.as_mut_ptr(), length) };
            if usize::try_from(count).ok()? != length {
                return None;
            }
        }
        Some(self.text == needle)
    }
    fn release_text(&mut self) {
        self.text = Vec::new();
    }
    fn log(&mut self, event: Diagnostic, a: u32, b: u32) {
        use Diagnostic::*;
        let (severity, arguments) = match event {
            ShortRecord => (1, format_args!("Short cache record {}", a)),
            InvalidRecord => (1, format_args!("Invalid cache record {}", a)),
            InvalidString => (1, format_args!("Invalid cache string length {}", a)),
            VisibleSeek => (1, format_args!("Failed to seek visible-text LUT")),
            VisibleRead => (1, format_args!("Failed to read visible-text LUT batch")),
            WriteRecord => (1, format_args!("Failed to encode/write cache record {}", a)),
            InvalidLut => (
                1,
                format_args!("Failed to write LUT due to invalid page positions"),
            ),
            Promote => (1, format_args!("Failed to move built section into place")),
            TruncatedHeader => (
                1,
                format_args!("Deserialization failed: truncated section header"),
            ),
            UnknownVersion => (
                1,
                format_args!("Deserialization failed: Unknown version {}", a),
            ),
            Parameters => (
                1,
                format_args!("Deserialization failed: Parameters do not match"),
            ),
            MalformedPartial => (
                1,
                format_args!("Deserialization failed: malformed partial section"),
            ),
            Loaded => (
                0,
                format_args!(
                    "Deserialization succeeded: {} pages{}",
                    a,
                    if b != 0 { " (partial)" } else { "" }
                ),
            ),
            CacheMissing => (0, format_args!("Cache does not exist, no action needed")),
            CacheClearFailed => (1, format_args!("Failed to clear cache")),
            CacheCleared => (0, format_args!("Cache cleared successfully")),
            PageFileClosed => (1, format_args!("File not open for writing page {}", a)),
            PageWriteFailed => (1, format_args!("Failed to serialize page {}", a)),
            PageProcessed => (0, format_args!("Page {} processed", a)),
            HeaderFileClosed => (0, format_args!("File not open for writing header")),
        };
        log::message(self.leaves, severity, b"SCT", arguments);
    }
}

impl section_cache::Storage for CacheFile<'_> {
    fn close(&mut self) {
        self.file.close();
    }
    fn exists_final(&mut self) -> bool {
        unsafe { (self.resources.path_exists)(self.path.wire()) != 0 }
    }
    fn remove_final(&mut self) -> bool {
        unsafe { (self.leaves.path_remove)(self.path.wire()) != 0 }
    }
    fn exists_temporary(&mut self) -> bool {
        unsafe {
            (self.resources.path_exists)(
                self.temporary.as_ref().expect("prepared cache path").wire(),
            ) != 0
        }
    }
    fn remove_temporary(&mut self) {
        unsafe {
            (self.leaves.path_remove)(self.temporary.as_ref().expect("prepared cache path").wire())
        };
    }
}
impl section_cache::Writer for CacheFile<'_> {
    fn is_open(&mut self) -> bool {
        self.file.is_open()
    }
    fn write(&mut self, input: &[u8]) -> bool {
        unsafe { self.file.write(input.as_ptr(), input.len()) == input.len() }
    }
    fn promote_temporary(&mut self) -> bool {
        unsafe {
            (self.leaves.path_rename)(
                self.temporary.as_ref().expect("prepared cache path").wire(),
                self.path.wire(),
            ) != 0
        }
    }
}
