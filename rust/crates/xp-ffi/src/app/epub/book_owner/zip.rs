use super::{inflate::Inflater, log, File, Text, XpEpubLeaves};
use crate::app::{abi, resources::Buffer};
use core::{ffi::c_void, ptr, slice};
use xp_core::{
    book::io::Sink,
    book_io::zip_read,
    zip::{self, Details, Entry, Step, Walker},
};

mod diagnostics;

pub(super) struct Archive<'a> {
    path: &'a Text,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    file: Option<File>,
    details: Option<Details>,
    cursor: Option<u32>,
    last_name: Text,
    last: Option<Entry>,
}

impl<'a> Archive<'a> {
    pub(super) fn new(
        path: &'a Text,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> Self {
        Self {
            path,
            resources,
            leaves,
            file: None,
            details: None,
            cursor: None,
            last_name: Text::default(),
            last: None,
        }
    }

    pub(super) fn open(&mut self) -> bool {
        if self.file.is_none() {
            self.file = File::open(self.leaves, self.path, c"ZIP");
        }
        self.file.is_some()
    }

    pub(super) fn close(&mut self) {
        self.file = None;
        self.cursor = None;
        self.last = None;
    }

    fn details(&mut self) -> Option<Details> {
        if let Some(details) = self.details {
            return Some(details);
        }
        let file = self.file.as_ref()?;
        let size = file.size();
        let Some(request) = zip::eocd_request(size) else {
            self.error(if size < 22 {
                "File too small to be a valid zip"
            } else {
                "Invalid zip file size"
            });
            return None;
        };
        let Some(backing) =
            Buffer::allocate(self.resources, abi::MEMORY_DEFAULT, request.length as usize)
        else {
            self.error("Failed to allocate memory for EOCD scan buffer");
            return None;
        };
        // Native storage is initialized before constructing a readable slice.
        unsafe { ptr::write_bytes(backing.as_ptr(), 0, backing.len()) };
        let bytes = unsafe { slice::from_raw_parts_mut(backing.as_ptr(), backing.len()) };
        if !file.seek(request.offset) || !file.read(bytes) {
            self.error("EOCD signature not found in zip file");
            return None;
        }
        self.details = zip::eocd(bytes);
        if self.details.is_none() {
            self.error("EOCD signature not found in zip file");
        }
        self.details
    }

    fn error(&self, message: &str) {
        log::message(self.leaves, 1, b"ZIP", format_args!("{}", message));
    }

    fn next(&self, walker: &mut Walker) -> Option<Entry> {
        let file = self.file.as_ref()?;
        let mut header = [0; 46];
        while let Some(request) = walker.request() {
            if !file.seek(request.offset) {
                self.error("Failed to seek central directory");
                return None;
            }
            let count = file.read_count(&mut header[..request.length as usize]);
            match walker.advance(&header[..count]) {
                Some(Step::Entry(entry)) => return Some(entry),
                Some(Step::Read(_)) => continue,
                Some(Step::Done) => return None,
                None => {
                    self.error("Invalid central directory record");
                    return None;
                }
            }
        }
        None
    }

    fn lookup_open(&mut self, name: &[u8]) -> Option<Entry> {
        let name = name.split(|&byte| byte == 0).next().unwrap_or_default();
        if self.last.is_some() && self.last_name.bytes() == name {
            return self.last;
        }
        let details = self.details()?;
        let mut walker = Walker::new(
            details.central_offset,
            self.cursor.unwrap_or(details.central_offset),
        );
        let mut bytes = [0; 256];
        while let Some(entry) = self.next(&mut walker) {
            let length = entry.name.length as usize;
            if length != name.len() || length >= bytes.len() {
                continue;
            }
            let actual = self.file.as_ref()?.read_signed(&mut bytes[..length]);
            if actual != length as i32 {
                log::with_bytes(
                    self.leaves,
                    1,
                    b"ZIP",
                    &[b"Failed to read entry name for ", name],
                    format_args!(": expected {} bytes, got {}", length, actual),
                );
                break;
            }
            if &bytes[..length] == name {
                self.last_name.assign(name).ok()?;
                self.last = Some(entry);
                self.cursor = Some(entry.next_offset);
                return Some(entry);
            }
        }
        None
    }

    pub(super) fn lookup(&mut self, name: &[u8]) -> Option<Entry> {
        let opened = self.file.is_none();
        if !self.open() {
            return None;
        }
        let entry = self.lookup_open(name);
        if opened {
            self.close();
        }
        entry
    }

    pub(super) fn enumerate(&mut self, mut visit: impl FnMut(&[u8], &Entry)) -> bool {
        self.enumerate_until(|name, entry| {
            visit(name, entry);
            true
        })
    }

    pub(super) fn enumerate_until(&mut self, mut visit: impl FnMut(&[u8], &Entry) -> bool) -> bool {
        let opened = self.file.is_none();
        if !self.open() {
            return false;
        }
        let ok = if let Some(details) = self.details() {
            let mut walker = Walker::new(details.central_offset, details.central_offset);
            let mut bytes = [0; 256];
            while let Some(entry) = self.next(&mut walker) {
                let length = entry.name.length as usize;
                if length >= bytes.len() {
                    continue;
                }
                if !self.file.as_ref().unwrap().read(&mut bytes[..length]) {
                    self.error("Failed to read entry name");
                    break;
                }
                if !visit(&bytes[..length], &entry) {
                    break;
                }
            }
            true
        } else {
            false
        };
        if opened {
            self.close();
        }
        ok
    }

    fn prepare(&mut self, name: &[u8]) -> Option<Entry> {
        let entry = self.lookup_open(name)?;
        let file = self.file.as_ref()?;
        let mut header = [0; 30];
        if !file.seek(entry.local_offset) || !file.read(&mut header) {
            self.error("Something went wrong reading the local header");
            return None;
        }
        let Some(offset) = zip::data_offset(entry.local_offset, &header) else {
            self.error("Not a valid zip file header");
            return None;
        };
        // Preserve the target's signed-long offset check, then unchecked seek.
        if offset > i32::MAX as u64 {
            return None;
        }
        file.seek(offset as u32);
        Some(entry)
    }

    pub(super) unsafe fn stream(
        &mut self,
        name: &[u8],
        chunk: usize,
        early_stop: bool,
        sink: Sink,
        context: *mut c_void,
    ) -> bool {
        let opened = self.file.is_none();
        if !self.open() {
            return false;
        }
        let result = if let Some(entry) = self.prepare(name) {
            let mut extract = Extract {
                file: self.file.as_ref().unwrap(),
                resources: self.resources,
                leaves: self.leaves,
                inflater: None,
                name,
                sink,
                context,
            };
            unsafe {
                zip_read::stream(
                    &extract.services(),
                    entry.method,
                    entry.compressed_size,
                    entry.uncompressed_size,
                    chunk,
                    early_stop,
                )
            }
        } else {
            false
        };
        if opened {
            self.close();
        }
        result
    }

    pub(super) fn memory(&mut self, name: &[u8], trailing_nul: bool) -> Option<Buffer> {
        unsafe extern "C" fn unused_sink(_: *mut c_void, _: *const u8, _: usize) -> u8 {
            unreachable!()
        }
        let opened = self.file.is_none();
        if !self.open() {
            return None;
        }
        let result = self.prepare(name).and_then(|entry| {
            let mut extract = Extract {
                file: self.file.as_ref().unwrap(),
                resources: self.resources,
                leaves: self.leaves,
                inflater: None,
                name,
                sink: unused_sink,
                context: ptr::null_mut(),
            };
            let capacity = entry
                .uncompressed_size
                .checked_add(u32::from(trailing_nul))?;
            // The core initializes every returned byte, including the optional NUL.
            let data = unsafe {
                zip_read::memory(
                    &extract.services(),
                    entry.method,
                    entry.compressed_size,
                    entry.uncompressed_size,
                    trailing_nul,
                    ptr::null_mut(),
                )
            };
            unsafe {
                Buffer::from_raw(self.resources, abi::MEMORY_DEFAULT, data, capacity as usize)
            }
        });
        if opened {
            self.close();
        }
        result
    }
}

struct Extract<'a> {
    file: &'a File,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    inflater: Option<Inflater>,
    name: &'a [u8],
    sink: Sink,
    context: *mut c_void,
}

impl Extract<'_> {
    fn services(&mut self) -> zip_read::Services {
        unsafe extern "C" fn read(raw: *mut c_void, bytes: *mut u8, len: usize) -> i32 {
            let ctx = raw.cast::<Extract<'_>>();
            let (file, leaves) = unsafe { ((*ctx).file, (*ctx).leaves) };
            let Some(handle) = file.handle else {
                return -1;
            };
            let Ok(len) = u32::try_from(len) else {
                return -1;
            };
            unsafe { (leaves.file_read)(handle.as_ptr(), bytes, len) }
        }
        unsafe extern "C" fn allocate(raw: *mut c_void, len: usize) -> *mut u8 {
            let resources = unsafe { (*raw.cast::<Extract<'_>>()).resources };
            unsafe { (resources.buffer_allocate)(abi::MEMORY_DEFAULT, len) }
        }
        unsafe extern "C" fn release(raw: *mut c_void, bytes: *mut u8) {
            let resources = unsafe { (*raw.cast::<Extract<'_>>()).resources };
            unsafe { (resources.buffer_release)(abi::MEMORY_DEFAULT, bytes) };
        }
        unsafe extern "C" fn init(
            raw: *mut c_void,
            streaming: bool,
            fill: zip_read::FillFn,
            context: *mut c_void,
        ) -> bool {
            let ctx = raw.cast::<Extract<'_>>();
            let (resources, leaves) = unsafe { ((*ctx).resources, (*ctx).leaves) };
            let inflater = unsafe { &mut (*ctx).inflater };
            *inflater = Inflater::new(resources, leaves, streaming, fill, context);
            inflater.is_some()
        }
        unsafe extern "C" fn exact(raw: *mut c_void, bytes: *mut u8, len: usize) -> bool {
            let inflater = unsafe { &mut (*raw.cast::<Extract<'_>>()).inflater };
            inflater
                .as_mut()
                .is_some_and(|inflate| unsafe { inflate.read(bytes, len) })
        }
        unsafe extern "C" fn inflate(
            raw: *mut c_void,
            bytes: *mut u8,
            len: usize,
            produced: *mut usize,
        ) -> i32 {
            let inflater = unsafe { &mut (*raw.cast::<Extract<'_>>()).inflater };
            inflater.as_mut().map_or(2, |inflate| unsafe {
                inflate.read_at_most(bytes, len, &mut *produced)
            })
        }
        unsafe extern "C" fn write(raw: *mut c_void, bytes: *const u8, len: usize) -> usize {
            let ctx = raw.cast::<Extract<'_>>();
            let (sink, context) = unsafe { ((*ctx).sink, (*ctx).context) };
            if unsafe { sink(context, bytes, len) } != 0 {
                len
            } else {
                0
            }
        }
        zip_read::Services {
            context: (self as *mut Self).cast(),
            read,
            allocate,
            release,
            init,
            exact,
            inflate,
            write,
            diagnostic: Some(diagnostics::extract),
        }
    }
}

#[cfg(feature = "app-test")]
#[path = "../test_support/book_zip.rs"]
mod tests;
