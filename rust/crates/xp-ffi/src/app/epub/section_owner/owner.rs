extern crate alloc;

use super::{
    super::{abi::XpEpubLeaves, book_owner::queries, Text},
    file::CacheFile,
};
use crate::{
    app::abi,
    section_cache::{self, LoadState, Query},
};
use alloc::{alloc::alloc, boxed::Box};
use core::{alloc::Layout, ffi::c_void, ptr::NonNull};

pub(super) struct ReaderSection {
    path: Text,
    spine: i32,
    readable: LoadState,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
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
    pub(super) unsafe fn open(
        book: NonNull<c_void>,
        spine: i32,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> Option<NonNull<Self>> {
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
        let raw = NonNull::new(unsafe { alloc(Layout::new::<Self>()) }.cast::<Self>())?;
        unsafe {
            raw.as_ptr().write(Self {
                path,
                spine,
                readable: LoadState::default(),
                resources,
                leaves,
            })
        };
        Some(raw)
    }

    pub(super) unsafe fn close(raw: NonNull<Self>) {
        unsafe { drop(Box::from_raw(raw.as_ptr())) };
    }

    pub(super) unsafe fn load(raw: NonNull<Self>, expected: &[u32; 20]) -> bool {
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

    pub(super) unsafe fn query(raw: NonNull<Self>, query: Query) -> Option<[u32; 3]> {
        let owner = raw.as_ptr();
        let mut io = unsafe { CacheFile::new(&(*owner).path, (*owner).resources, (*owner).leaves) };
        let mut output = [0; 3];
        section_cache::query(&mut io, query, &mut output)?;
        Some(output)
    }

    pub(super) unsafe fn anchor(raw: NonNull<Self>, needle: &[u8]) -> Option<u32> {
        let owner = raw.as_ptr();
        let mut io = unsafe { CacheFile::new(&(*owner).path, (*owner).resources, (*owner).leaves) };
        section_cache::anchor(&mut io, needle)
    }

    pub(super) unsafe fn clear(raw: NonNull<Self>) -> bool {
        let owner = raw.as_ptr();
        let mut io = unsafe { CacheFile::new(&(*owner).path, (*owner).resources, (*owner).leaves) };
        io.prepare_temporary().is_ok() && section_cache::clear(&mut io)
    }
}
