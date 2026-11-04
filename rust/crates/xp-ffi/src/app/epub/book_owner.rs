extern crate alloc;

use super::{abi::XpEpubLeaves, app_abi, Text};
use alloc::{boxed::Box, vec::Vec};
use core::{cell::UnsafeCell, ffi::c_void, marker::PhantomData, ptr::NonNull};
use xp_core::book::io::Sink;

pub(super) mod queries;
pub(super) use load::LoadError;
mod cache;
mod css;
mod facade;
mod inflate;
mod items;
mod load;
pub(super) mod log;
mod package;
mod txt;
mod writer;
#[cfg(feature = "app-test")]
pub(super) use writer::io::FileSlot;
mod zip;

#[cfg(feature = "app-test")]
#[path = "test_support/book_open.rs"]
mod open_tests;

#[cfg(feature = "app-test")]
#[path = "test_support/book_load.rs"]
mod load_tests;

pub(super) struct ReaderBook {
    facade: Option<NonNull<c_void>>,
    resources: &'static app_abi::Resources,
    now: unsafe extern "C" fn() -> u32,
    path: Text,
    cache_path: Text,
    metadata: Metadata,
    cache: BookCache,
    css: Option<Box<crate::css::Css>>,
    protection: Protection,
}

#[derive(Default)]
struct Metadata {
    title: Vec<u8>,
    author: Vec<u8>,
    language: Vec<u8>,
    cover: Vec<u8>,
    text_reference: Vec<u8>,
}

impl Metadata {
    fn assign(&mut self, field: u32, bytes: &[u8]) -> Result<(), ()> {
        use xp_core::epub::opf;
        let output = match field {
            opf::TITLE => &mut self.title,
            opf::AUTHOR => &mut self.author,
            opf::LANGUAGE => &mut self.language,
            opf::COVER => &mut self.cover,
            opf::TEXT => &mut self.text_reference,
            _ => return Err(()),
        };
        output.clear();
        if matches!(field, opf::TITLE | opf::AUTHOR) && xp_core::utf8::may_compose(bytes) {
            // Match utf8ComposeNfc, including replacement bytes after XML truncation.
            let mut length = Some(0usize);
            xp_core::utf8::compose(
                bytes,
                &mut |part| length = length.and_then(|n| n.checked_add(part.len())),
                &mut |_| true,
            );
            output
                .try_reserve_exact(length.ok_or(())?)
                .map_err(|_| ())?;
            xp_core::utf8::compose(
                bytes,
                &mut |part| output.extend_from_slice(part),
                &mut |_| true,
            );
        } else {
            output.try_reserve_exact(bytes.len()).map_err(|_| ())?;
            output.extend_from_slice(bytes);
        }
        Ok(())
    }
}

struct BookCache {
    file: File,
    loaded: bool,
    lut_offset: u32,
    spine_count: u16,
    toc_count: u16,
    total_size: u32,
    starts: Vec<u32>,
    window: UnsafeCell<SizeWindow>,
}

struct SizeWindow {
    first: Option<u32>,
    sizes: [u32; crate::book_limits::METADATA_WINDOW as usize],
}

// ResourceAccess (when already held) precedes cache, then Storage. No sinks,
// logging, parser calls or recursive cache acquisition while this guard exists.
pub(super) struct CacheGuard {
    leaves: &'static XpEpubLeaves,
    task: PhantomData<*mut ()>,
}

impl CacheGuard {
    pub(super) fn acquire(leaves: &'static XpEpubLeaves) -> Self {
        unsafe { (leaves.cache_lock)() };
        Self {
            leaves,
            task: PhantomData,
        }
    }
}

impl Drop for CacheGuard {
    fn drop(&mut self) {
        unsafe { (self.leaves.cache_unlock)() };
    }
}

pub(super) struct File {
    handle: Option<NonNull<c_void>>,
    leaves: &'static XpEpubLeaves,
}

impl File {
    pub(super) fn open(
        leaves: &'static XpEpubLeaves,
        path: &Text,
        origin: &'static core::ffi::CStr,
    ) -> Option<Self> {
        let handle = NonNull::new(unsafe {
            (leaves.file_open_read)(
                path.wire(),
                app_abi::Text::from_terminated(origin.to_bytes_with_nul()),
            )
        })?;
        Some(Self {
            handle: Some(handle),
            leaves,
        })
    }

    pub(super) fn size(&self) -> u32 {
        self.handle.map_or(0, |handle| unsafe {
            (self.leaves.file_size)(handle.as_ptr())
        })
    }

    fn seek(&self, offset: u32) -> bool {
        self.handle
            .is_some_and(|handle| unsafe { (self.leaves.file_seek)(handle.as_ptr(), offset) != 0 })
    }

    fn read(&self, bytes: &mut [u8]) -> bool {
        self.read_result(bytes)
            .is_some_and(|count| count == bytes.len())
    }

    fn read_count(&self, bytes: &mut [u8]) -> usize {
        self.read_result(bytes).unwrap_or(0)
    }

    fn read_result(&self, bytes: &mut [u8]) -> Option<usize> {
        usize::try_from(self.read_signed(bytes)).ok()
    }

    fn read_signed(&self, bytes: &mut [u8]) -> i32 {
        let Ok(length) = u32::try_from(bytes.len()) else {
            return -1;
        };
        let Some(handle) = self.handle else {
            return -1;
        };
        unsafe { (self.leaves.file_read)(handle.as_ptr(), bytes.as_mut_ptr(), length) }
    }

    fn close(&mut self) -> bool {
        self.handle
            .take()
            .is_some_and(|handle| unsafe { (self.leaves.file_close)(handle.as_ptr()) != 0 })
    }

    #[cfg(feature = "app-test")]
    pub(super) fn read_at(&self, offset: u32, bytes: &mut [u8]) -> bool {
        let _guard = CacheGuard::acquire(self.leaves);
        self.seek(offset) && self.read(bytes)
    }
}

impl Drop for File {
    fn drop(&mut self) {
        self.close();
    }
}

pub(super) enum Protection {
    Plain,
    Encrypted(ProtectionLoan),
}

pub(super) struct ProtectionLoan {
    handle: NonNull<c_void>,
    leaves: &'static XpEpubLeaves,
}

pub(super) enum OpenError {
    OutOfMemory,
    Message(Text),
}

impl Protection {
    pub(super) fn open(leaves: &'static XpEpubLeaves, path: &Text) -> Result<Self, OpenError> {
        #[derive(Default)]
        struct Capture {
            message: Text,
            failed: bool,
        }
        unsafe extern "C" fn receive(context: *mut c_void, message: app_abi::Text) -> u8 {
            let capture = unsafe { &mut *context.cast::<Capture>() };
            let Some(bytes) = (unsafe { message.bytes() }) else {
                capture.failed = true;
                return 0;
            };
            if capture.message.assign(bytes).is_err() {
                capture.failed = true;
                return 0;
            }
            1
        }
        let mut capture = Capture::default();
        let handle = unsafe {
            (leaves.protection_open)(path.wire(), receive, (&mut capture as *mut Capture).cast())
        };
        let loan = NonNull::new(handle).map(|handle| ProtectionLoan { handle, leaves });
        if capture.failed {
            return Err(OpenError::OutOfMemory);
        }
        if !capture.message.is_empty() {
            return Err(OpenError::Message(capture.message));
        }
        Ok(loan.map_or(Self::Plain, Self::Encrypted))
    }

    pub(super) fn is_encrypted(&self, path: &[u8]) -> bool {
        match self {
            Self::Plain => false,
            Self::Encrypted(loan) => unsafe {
                (loan.leaves.protection_encrypted)(
                    loan.handle.as_ptr(),
                    app_abi::Text::from_bytes(path),
                ) != 0
            },
        }
    }

    pub(super) fn size(&self, path: &[u8]) -> Option<u32> {
        match self {
            Self::Plain => None,
            Self::Encrypted(loan) => Some(unsafe {
                (loan.leaves.protection_size)(loan.handle.as_ptr(), app_abi::Text::from_bytes(path))
            }),
        }
    }

    // The sink and context stay valid for the synchronous native stream.
    pub(super) unsafe fn stream(&self, path: &[u8], sink: Sink, context: *mut c_void) -> bool {
        match self {
            Self::Plain => false,
            Self::Encrypted(loan) => unsafe {
                (loan.leaves.protection_stream)(
                    loan.handle.as_ptr(),
                    app_abi::Text::from_bytes(path),
                    sink,
                    context,
                ) != 0
            },
        }
    }
}

impl Drop for ProtectionLoan {
    fn drop(&mut self) {
        unsafe { (self.leaves.protection_close)(self.handle.as_ptr()) };
    }
}
