use super::{log, txt, zip::Archive, File, Protection, Text, Vec, XpEpubLeaves};
use crate::app::{abi, resources::Buffer};
use core::{ffi::c_void, ptr, slice};
use xp_core::book::io::Sink;

pub(super) struct Items<'a> {
    pub path: &'a Text,
    pub protection: &'a Protection,
    pub resources: &'static abi::Resources,
    pub leaves: &'static XpEpubLeaves,
    pub now: unsafe extern "C" fn() -> u32,
}

pub(super) struct Contents {
    buffer: Buffer,
    length: usize,
}

impl Contents {
    pub(super) fn bytes(&self) -> &[u8] {
        // ZIP extraction or exact-length decryption initialized this prefix.
        unsafe { slice::from_raw_parts(self.buffer.as_ptr(), self.length) }
    }
}

impl Items<'_> {
    fn normalize(&self, href: &[u8]) -> Option<Vec<u8>> {
        let mut path = Vec::new();
        xp_core::epub::normalize_href(&mut path, href).ok()?;
        Some(path)
    }

    fn empty(&self, href: &[u8]) -> bool {
        if !href.is_empty() {
            return false;
        }
        log::message(
            self.leaves,
            0,
            b"EBP",
            format_args!("Failed to read item, empty href"),
        );
        true
    }

    fn failed(&self, path: &[u8]) {
        log::with_bytes(
            self.leaves,
            1,
            b"EBP",
            &[b"content read failed for ", path],
            format_args!(""),
        );
    }

    pub(super) fn memory(&self, href: &[u8], trailing_nul: bool) -> Option<Contents> {
        if self.empty(href) {
            return None;
        }
        let path = self.normalize(href)?;
        if self.protection.is_encrypted(&path) {
            let length = self.protection.size(&path)? as usize;
            let total = length.checked_add(usize::from(trailing_nul))?;
            let Some(buffer) = Buffer::allocate(self.resources, abi::MEMORY_DEFAULT, total.max(1))
            else {
                log::with_bytes(
                    self.leaves,
                    1,
                    b"EBP",
                    &[b"insufficient memory for ", &path],
                    format_args!(" ({} bytes)", total),
                );
                return None;
            };
            struct Output {
                data: *mut u8,
                length: usize,
                written: usize,
            }
            unsafe extern "C" fn append(raw: *mut c_void, bytes: *const u8, length: usize) -> u8 {
                let output = unsafe { &mut *raw.cast::<Output>() };
                if length > output.length.saturating_sub(output.written) {
                    return 0;
                }
                if length != 0 {
                    unsafe {
                        ptr::copy_nonoverlapping(bytes, output.data.add(output.written), length)
                    };
                }
                output.written += length;
                1
            }
            let mut output = Output {
                data: buffer.as_ptr(),
                length,
                written: 0,
            };
            let ok = unsafe {
                self.protection
                    .stream(&path, append, (&mut output as *mut Output).cast())
            };
            if !ok || output.written != length {
                drop(buffer);
                self.failed(&path);
                return None;
            }
            if trailing_nul {
                unsafe { buffer.as_ptr().add(length).write(0) };
            }
            Some(Contents { buffer, length })
        } else {
            let mut archive = Archive::new(self.path, self.resources, self.leaves);
            let Some(buffer) = archive.memory(&path, trailing_nul) else {
                log::with_bytes(
                    self.leaves,
                    0,
                    b"EBP",
                    &[b"Failed to read item ", &path],
                    format_args!(""),
                );
                return None;
            };
            let length = buffer.len() - usize::from(trailing_nul);
            Some(Contents { buffer, length })
        }
    }

    pub(super) unsafe fn stream(
        &self,
        href: &[u8],
        chunk: usize,
        early_stop: bool,
        sink: Sink,
        context: *mut c_void,
    ) -> bool {
        if self.empty(href) {
            return false;
        }
        if txt::is_text(self.path.bytes()) {
            return unsafe {
                txt::stream(
                    self.path,
                    self.resources,
                    self.leaves,
                    self.now,
                    sink,
                    context,
                )
            };
        }
        let Some(path) = self.normalize(href) else {
            return false;
        };
        if self.protection.is_encrypted(&path) {
            let ok = unsafe { self.protection.stream(&path, sink, context) };
            if !ok {
                self.failed(&path);
            }
            ok
        } else {
            unsafe {
                Archive::new(self.path, self.resources, self.leaves)
                    .stream(&path, chunk, early_stop, sink, context)
            }
        }
    }

    pub(super) fn size(&self, href: &[u8]) -> Option<u32> {
        if txt::is_text(self.path.bytes()) {
            return Some(File::open(self.leaves, self.path, c"EBP")?.size());
        }
        let path = self.normalize(href)?;
        // This query reports the ZIP size even when the resource is encrypted.
        Archive::new(self.path, self.resources, self.leaves)
            .lookup(&path)
            .map(|entry| entry.uncompressed_size)
    }
}

#[cfg(feature = "app-test")]
#[path = "../test_support/book_items.rs"]
mod tests;
