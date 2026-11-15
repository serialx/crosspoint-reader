extern crate alloc;
use super::{log, zip::Archive, File, Metadata, Text, XpEpubLeaves};
use crate::app::abi;
use alloc::vec::Vec;
use core::{ffi::c_void, ptr, slice};
use xp_core::{
    book_io::zip_index::{self, Target},
    epub::{
        cache::{self, Cache, Io, SizeOp, Span},
        diagnostic::Diagnostics,
        normalize_href,
    },
};

mod blocks;
pub(super) mod io;
use blocks::Blocks;
use io::{Buffered, FileSlot};

pub(super) struct Writer<'a> {
    pub(super) cache: Cache,
    pub(super) streams: Streams<'a>,
}

impl<'a> Writer<'a> {
    pub(super) fn new(
        path: &'a Text,
        cache_path: &'a Text,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> Self {
        log::message(leaves, 0, b"BMC", format_args!("Entering write mode"));
        Self {
            cache: Cache::default(),
            streams: Streams::new(path, cache_path, resources, leaves),
        }
    }

    pub(super) fn begin_opf(&mut self) -> Result<(), u32> {
        log::message(
            self.streams.leaves,
            0,
            b"BMC",
            format_args!("Beginning content opf pass"),
        );
        self.cache.begin_opf(&self.streams.io())
    }

    pub(super) fn end_opf(&mut self) -> Result<(), u32> {
        let result = self.cache.end_opf(&self.streams.io());
        if result.is_err() {
            log::message(
                self.streams.leaves,
                1,
                b"BMC",
                format_args!("Failed writing spine tmp file"),
            );
        }
        result
    }

    pub(super) fn begin_toc(&mut self, diagnostics: &Diagnostics) -> Result<(), u32> {
        log::message(
            self.streams.leaves,
            0,
            b"BMC",
            format_args!("Beginning toc pass"),
        );
        self.cache.begin_toc(&self.streams.io(), diagnostics)
    }

    pub(super) fn end_toc(&mut self) -> Result<(), u32> {
        let result = self.cache.end_toc(&self.streams.io());
        if result.is_err() {
            log::message(
                self.streams.leaves,
                1,
                b"BMC",
                format_args!("Failed writing toc tmp file"),
            );
        }
        result
    }

    pub(super) fn end(&mut self) -> bool {
        let was_building = self.cache.building;
        self.cache.building = false;
        if was_building {
            log::message(
                self.streams.leaves,
                0,
                b"BMC",
                format_args!(
                    "Wrote {} spine, {} TOC entries",
                    self.cache.spine_count, self.cache.toc_count
                ),
            );
        } else {
            log::message(
                self.streams.leaves,
                0,
                b"BMC",
                format_args!("endWrite called but not in build mode"),
            );
        }
        was_building
    }

    pub(super) fn build(
        &mut self,
        metadata: &Metadata,
        diagnostics: &Diagnostics,
    ) -> Result<(), u32> {
        let result = self.cache.build(
            &self.streams.io(),
            [
                &metadata.title,
                &metadata.author,
                &metadata.language,
                &metadata.cover,
                &metadata.text_reference,
            ],
            diagnostics,
        );
        if self.streams.flush_failed {
            log::message(
                self.streams.leaves,
                1,
                b"BMC",
                format_args!("Failed writing book.bin, removing truncated file"),
            );
        } else if result.is_err() && !self.streams.size_open_failed {
            log::message(
                self.streams.leaves,
                1,
                b"BMC",
                format_args!("Failed building book.bin"),
            );
        }
        if result.is_ok() {
            log::message(
                self.streams.leaves,
                0,
                b"BMC",
                format_args!("Successfully built book.bin"),
            );
        }
        self.streams.finish_build();
        result
    }
}

pub(super) struct Streams<'a> {
    path: &'a Text,
    cache_path: &'a Text,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    files: [FileSlot; 4],
    pass: Option<(usize, Buffered)>,
    build: Option<[Buffered; 3]>,
    zip: Archive<'a>,
    targets: Option<Blocks<Target>>,
    sizes: Option<Blocks<u32>>,
    entry: Vec<u8>,
    name: Vec<u8>,
    failed: bool,
    pub(super) flush_failed: bool,
    pub(super) size_open_failed: bool,
}

impl<'a> Streams<'a> {
    fn new(
        path: &'a Text,
        cache_path: &'a Text,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> Self {
        Self {
            path,
            cache_path,
            resources,
            leaves,
            files: core::array::from_fn(|_| FileSlot::new()),
            pass: None,
            build: None,
            zip: Archive::new(path, resources, leaves),
            targets: None,
            sizes: None,
            entry: Vec::new(),
            name: Vec::new(),
            failed: false,
            flush_failed: false,
            size_open_failed: false,
        }
    }

    fn filename(&self, id: usize) -> Result<Text, ()> {
        let suffix = match id {
            0 => b"/.items.bin".as_slice(),
            1 => b"/spine.bin.tmp",
            2 => b"/toc.bin.tmp",
            _ => b"/book.bin",
        };
        let mut path = Text::default();
        path.assign_parts(&[self.cache_path.bytes(), suffix])
            .map_err(|_| ())?;
        Ok(path)
    }

    fn finish_build(&mut self) {
        if let Some(buffers) = &mut self.build {
            buffers[2].flush(&mut self.files[3]);
        }
        self.build = None;
        self.sizes = None;
        self.targets = None;
        self.zip.close();
        self.entry = Vec::new();
        self.name = Vec::new();
    }

    fn control(&mut self, id: usize, operation: u32) -> u32 {
        if id >= self.files.len() {
            return 0;
        }
        match operation {
            cache::OPEN_READ | cache::OPEN_WRITE => {
                let Ok(path) = self.filename(id) else {
                    self.failed = true;
                    return 0;
                };
                let ok = self.files[id].open(
                    self.leaves,
                    &path,
                    operation == cache::OPEN_WRITE,
                    if id == cache::ITEMS as usize {
                        c"COF"
                    } else {
                        c"BMC"
                    },
                );
                if !ok && id == cache::ITEMS as usize {
                    let action = if operation == cache::OPEN_WRITE {
                        "writing"
                    } else {
                        "reading"
                    };
                    log::message(self.leaves, 1, b"COF", format_args!("Couldn't open temp items file for {}. This is probably going to be a fatal error.", action));
                }
                ok.into()
            }
            cache::CLOSE => {
                self.files[id].close();
                1
            }
            cache::REMOVE => {
                let Ok(path) = self.filename(id) else {
                    self.failed = true;
                    return 0;
                };
                if id == 0 && unsafe { (self.resources.path_exists)(path.wire()) } == 0 {
                    return 1;
                }
                u32::from(unsafe { (self.leaves.path_remove)(path.wire()) })
            }
            cache::IS_OPEN => self.files[id].is_open().into(),
            cache::SIZE => self.files[id].size(),
            cache::POSITION => {
                if let Some(buffers) = &self.build {
                    if id > 0 {
                        return buffers[id - 1].position();
                    }
                }
                self.files[id].position()
            }
            cache::BEGIN_PASS => {
                self.pass = Some((id, Buffered::new(self.resources, &self.files[id], true)));
                1
            }
            cache::END_PASS => {
                let ok = self
                    .pass
                    .as_mut()
                    .is_none_or(|(id, buffer)| buffer.flush(&mut self.files[*id]));
                self.pass = None;
                ok.into()
            }
            cache::BEGIN_BUILD => {
                // Native construction order is output, spine input, TOC input.
                let book = Buffered::new(self.resources, &self.files[3], true);
                let spine = Buffered::new(self.resources, &self.files[1], false);
                let toc = Buffered::new(self.resources, &self.files[2], false);
                self.build = Some([spine, toc, book]);
                1
            }
            cache::END_BUILD => {
                self.flush_failed = !self
                    .build
                    .as_mut()
                    .is_none_or(|buffers| buffers[2].flush(&mut self.files[3]));
                u32::from(!self.flush_failed && !self.failed)
            }
            cache::END_SIZES => {
                self.sizes = None;
                self.zip.close();
                1
            }
            _ => 0,
        }
    }

    unsafe fn read(&mut self, id: usize, bytes: *mut u8, length: usize) -> usize {
        if self.failed || id >= self.files.len() {
            return 0;
        }
        if let Some(buffers) = &mut self.build {
            if (1..=2).contains(&id) {
                return unsafe { buffers[id - 1].read(&mut self.files[id], bytes, length) };
            }
        }
        unsafe { self.files[id].read(bytes, length) }.max(0) as usize
    }

    unsafe fn write(&mut self, id: usize, bytes: *const u8, length: usize) -> usize {
        if self.failed || id >= self.files.len() {
            return 0;
        }
        if let Some(buffers) = &mut self.build {
            return unsafe { buffers[2].write(&mut self.files[3], bytes, length) };
        }
        if let Some((pass_id, buffer)) = &mut self.pass {
            if id == *pass_id {
                return unsafe { buffer.write(&mut self.files[*pass_id], bytes, length) };
            }
        }
        unsafe { self.files[id].write(bytes, length) }
    }

    fn seek(&mut self, id: usize, position: u32) -> bool {
        if id >= self.files.len() {
            return false;
        }
        if let Some(buffers) = &mut self.build {
            if (1..=2).contains(&id) {
                return buffers[id - 1].seek(&mut self.files[id], position);
            }
        }
        self.files[id].seek(position)
    }

    pub(super) fn io(&mut self) -> Io {
        unsafe extern "C" fn control(raw: *mut c_void, id: u32, op: u32) -> u32 {
            unsafe { (&mut *raw.cast::<Streams<'_>>()).control(id as usize, op) }
        }
        unsafe extern "C" fn read(raw: *mut c_void, id: u32, bytes: *mut u8, len: usize) -> usize {
            unsafe { (&mut *raw.cast::<Streams<'_>>()).read(id as usize, bytes, len) }
        }
        unsafe extern "C" fn write(
            raw: *mut c_void,
            id: u32,
            bytes: *const u8,
            len: usize,
        ) -> usize {
            unsafe { (&mut *raw.cast::<Streams<'_>>()).write(id as usize, bytes, len) }
        }
        unsafe extern "C" fn seek(raw: *mut c_void, id: u32, pos: u32) -> u8 {
            u8::from(unsafe { (&mut *raw.cast::<Streams<'_>>()).seek(id as usize, pos) })
        }
        unsafe extern "C" fn sizes(
            raw: *mut c_void,
            op: u32,
            index: u32,
            value: *mut c_void,
        ) -> u64 {
            unsafe { (&mut *raw.cast::<Streams<'_>>()).size_storage(op, index, value) }
        }
        Io {
            context: (self as *mut Self).cast(),
            control,
            read,
            write,
            seek,
            sizes: Some(sizes),
            observe: None,
        }
    }

    unsafe fn size_storage(&mut self, operation: u32, index: u32, value: *mut c_void) -> u64 {
        match operation {
            x if x == SizeOp::Path as u32 => unsafe {
                value.cast::<Span>().write(Span::new(self.path.bytes()))
            },
            x if x == SizeOp::RawSize as u32 => {
                return u64::from(
                    File::open(self.leaves, self.path, c"BMC").map_or(0, |file| file.size()),
                )
            }
            x if x == SizeOp::OpenZip as u32 => {
                self.size_open_failed = !self.zip.open();
                return u64::from(!self.size_open_failed);
            }
            x if x == SizeOp::AllocateTargets as u32 => {
                self.targets = Some(Blocks::new(self.resources))
            }
            x if x == SizeOp::AllocateSizes as u32 => {
                let mut sizes = Blocks::new(self.resources);
                for _ in 0..index {
                    if sizes.push(0).is_err() {
                        self.failed = true;
                        break;
                    }
                }
                self.sizes = Some(sizes);
            }
            x if x == SizeOp::BeginEntry as u32 => self.entry = Vec::new(),
            x if x == SizeOp::Href as u32 => {
                if self
                    .entry
                    .try_reserve_exact((index as usize).saturating_sub(self.entry.len()))
                    .is_err()
                {
                    self.failed = true;
                    unsafe { value.cast::<Span>().write(Span::new(&[])) };
                } else {
                    self.entry.resize(index as usize, 0);
                    // The batch reader fills this span before requesting normalization.
                    unsafe {
                        value.cast::<Span>().write(Span {
                            data: self.entry.as_mut_ptr(),
                            len: self.entry.len(),
                        })
                    };
                }
            }
            x if x == SizeOp::Normalize as u32 => {
                self.failed |= normalize_href(&mut self.name, &self.entry).is_err();
                unsafe { value.cast::<Span>().write(Span::new(&self.name)) };
            }
            x if x == SizeOp::Append as u32 || x == SizeOp::SetTarget as u32 => {
                let fields = unsafe { slice::from_raw_parts(value.cast::<u64>(), 3) };
                let target = Target {
                    hash: fields[0],
                    len: fields[1] as u16,
                    index: fields[2] as u16,
                };
                if let Some(targets) = &mut self.targets {
                    if operation == SizeOp::Append as u32 {
                        self.failed |= targets.push(target).is_err();
                    } else {
                        self.failed |= !targets.set(index as usize, target);
                    }
                }
            }
            x if x == SizeOp::EndEntry as u32 => {
                self.entry = Vec::new();
                self.name = Vec::new();
            }
            x if x == SizeOp::Target as u32 => {
                let target = self
                    .targets
                    .as_ref()
                    .and_then(|targets| targets.get(index as usize))
                    .unwrap_or(Target {
                        hash: 0,
                        len: 0,
                        index: 0,
                    });
                unsafe {
                    ptr::copy_nonoverlapping(
                        [target.hash, u64::from(target.len), u64::from(target.index)].as_ptr(),
                        value.cast::<u64>(),
                        3,
                    )
                };
            }
            x if x == SizeOp::Fill as u32 => return self.fill_sizes() as u64,
            x if x == SizeOp::Clear as u32 => {
                if let Some(targets) = &mut self.targets {
                    targets.clear();
                }
            }
            x if x == SizeOp::DropTargets as u32 => self.targets = None,
            x if x == SizeOp::Cached as u32 => {
                return u64::from(
                    self.sizes
                        .as_ref()
                        .and_then(|sizes| sizes.get(index as usize))
                        .unwrap_or(0),
                )
            }
            x if x == SizeOp::Lookup as u32 => {
                let span = unsafe { value.cast::<Span>().read() };
                let bytes = if span.len == 0 {
                    &[]
                } else {
                    unsafe { slice::from_raw_parts(span.data, span.len) }
                };
                if normalize_href(&mut self.name, bytes).is_err() {
                    self.failed = true;
                    return 1 << 32;
                }
                return self
                    .zip
                    .lookup(&self.name)
                    .map_or(1 << 32, |entry| u64::from(entry.uncompressed_size));
            }
            x if x == SizeOp::Error as u32 => {
                if index == 0 {
                    log::message(
                        self.leaves,
                        1,
                        b"BMC",
                        format_args!("Could not open EPUB zip for size calculations"),
                    );
                } else {
                    let name = self
                        .name
                        .split(|&byte| byte == 0)
                        .next()
                        .unwrap_or_default();
                    log::with_bytes(
                        self.leaves,
                        1,
                        b"BMC",
                        &[b"Warning: Could not get size for spine item: ", name],
                        format_args!(""),
                    );
                }
            }
            x if x == SizeOp::Batch as u32 => log::message(
                self.leaves,
                0,
                b"BMC",
                format_args!("Using batch size lookup for {} spine items", index as i32),
            ),
            x if x == SizeOp::Matched as u32 => log::message(
                self.leaves,
                0,
                b"BMC",
                format_args!(
                    "Batch lookup matched {}/{} spine items",
                    index as i32,
                    self.sizes.as_ref().map_or(0, Blocks::len)
                ),
            ),
            _ => {}
        }
        if self.failed {
            cache::SIZE_STORAGE_ERROR
        } else {
            0
        }
    }

    fn fill_sizes(&mut self) -> usize {
        struct Batch<'a> {
            targets: &'a Blocks<Target>,
            sizes: &'a mut Blocks<u32>,
        }
        unsafe extern "C" fn get(raw: *mut c_void, index: usize) -> Target {
            unsafe { (&*raw.cast::<Batch<'_>>()).targets.get(index).unwrap() }
        }
        unsafe extern "C" fn set(raw: *mut c_void, index: u16, size: u32) -> bool {
            unsafe {
                (&mut *raw.cast::<Batch<'_>>())
                    .sizes
                    .set(usize::from(index), size)
            }
        }
        let (Some(targets), Some(sizes)) = (&self.targets, &mut self.sizes) else {
            return 0;
        };
        if targets.len() == 0 || self.failed {
            return 0;
        }
        let mut batch = Batch { targets, sizes };
        let mut matched = 0;
        self.zip.enumerate_until(|name, entry| {
            matched += unsafe {
                zip_index::batch_match(
                    name,
                    entry.uncompressed_size,
                    targets.len(),
                    (&mut batch as *mut Batch<'_>).cast(),
                    get,
                    set,
                )
            };
            matched < targets.len()
        });
        matched
    }
}

impl Drop for Streams<'_> {
    fn drop(&mut self) {
        if let Some((id, buffer)) = &mut self.pass {
            buffer.flush(&mut self.files[*id]);
        }
        self.pass = None;
        self.finish_build();
    }
}

#[cfg(feature = "app-test")]
#[path = "../test_support/book_writer.rs"]
mod tests;
