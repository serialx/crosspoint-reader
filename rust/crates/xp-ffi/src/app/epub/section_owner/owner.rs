extern crate alloc;

use super::{
    super::{abi::XpEpubLeaves, book_owner::queries, Text},
    driver::BuildOwner,
    file::CacheFile,
};
use crate::{
    app::abi,
    section_cache::{self, LoadState, Query},
};
use alloc::{alloc::alloc, boxed::Box};
use core::{alloc::Layout, ffi::c_void, ptr::NonNull};

pub(in crate::app::epub) struct ReaderSection {
    pub(super) path: Text,
    pub(super) spine: i32,
    pub(super) book: NonNull<c_void>,
    pub(super) readable: LoadState,
    pub(super) resources: &'static abi::Resources,
    pub(super) leaves: &'static XpEpubLeaves,
    pub(super) build: Option<BuildOwner>,
    pub(super) complete: bool,
    pub(super) progress: Progress,
}

#[derive(Default)]
pub(super) struct Progress {
    pub consumed: u32,
    pub total: u32,
    pub smoothed: f32,
    pub smoothed_at: u32,
}

impl ReaderSection {
    #[cfg(feature = "app-test")]
    pub(super) fn storage_for_test(
        &self,
    ) -> (&Text, &'static abi::Resources, &'static XpEpubLeaves) {
        assert_eq!(self.spine, 0);
        (&self.path, self.resources, self.leaves)
    }

    // The live ReaderBook supplies its native-hash cache root. No book borrow survives allocation.
    pub(in crate::app::epub) unsafe fn open(
        book: NonNull<c_void>,
        spine: i32,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> Option<NonNull<Self>> {
        let path = unsafe { Self::cache_path(book, spine) }?;
        let raw = NonNull::new(unsafe { alloc(Layout::new::<Self>()) }.cast::<Self>())?;
        unsafe {
            raw.as_ptr().write(Self {
                path,
                spine,
                book,
                readable: LoadState::default(),
                resources,
                leaves,
                build: None,
                complete: false,
                progress: Progress::default(),
            })
        };
        Some(raw)
    }

    pub(super) unsafe fn cache_path(book: NonNull<c_void>, spine: i32) -> Option<Text> {
        let mut path = Text::default();
        let mut digits = [0; 20];
        path.assign_parts(&[
            unsafe { queries::cache_bytes(book.as_ptr()) },
            b"/sections/",
            if spine < 0 { b"-" } else { b"" },
            queries::decimal(u64::from(spine.unsigned_abs()), &mut digits),
            b".bin",
        ])
        .ok()?;
        Some(path)
    }

    pub(in crate::app::epub) unsafe fn close(raw: NonNull<Self>) {
        unsafe { Self::suspend(raw) };
        unsafe { drop(Box::from_raw(raw.as_ptr())) };
    }

    pub(in crate::app::epub) unsafe fn load(raw: NonNull<Self>, expected: &[u32; 20]) -> bool {
        let owner = raw.as_ptr();
        let mut state = unsafe { (*owner).readable };
        let result = {
            let mut io =
                unsafe { CacheFile::new(&(*owner).path, (*owner).resources, (*owner).leaves) };
            if io.prepare_temporary().is_err() {
                return false;
            }
            section_cache::load(&mut io, expected, &mut state)
        };
        unsafe { (*owner).readable = state };
        result
    }

    pub(in crate::app::epub) unsafe fn query(raw: NonNull<Self>, query: Query) -> Option<[u32; 3]> {
        let owner = raw.as_ptr();
        let mut io = unsafe { CacheFile::new(&(*owner).path, (*owner).resources, (*owner).leaves) };
        let mut output = [0; 3];
        section_cache::query(&mut io, query, &mut output)?;
        Some(output)
    }

    pub(in crate::app::epub) unsafe fn anchor(raw: NonNull<Self>, needle: &[u8]) -> Option<u32> {
        let owner = raw.as_ptr();
        let mut io = unsafe { CacheFile::new(&(*owner).path, (*owner).resources, (*owner).leaves) };
        section_cache::anchor(&mut io, needle)
    }

    pub(in crate::app::epub) unsafe fn clear(raw: NonNull<Self>) -> bool {
        let owner = raw.as_ptr();
        let mut io = unsafe { CacheFile::new(&(*owner).path, (*owner).resources, (*owner).leaves) };
        io.prepare_temporary().is_ok() && section_cache::clear(&mut io)
    }
}
