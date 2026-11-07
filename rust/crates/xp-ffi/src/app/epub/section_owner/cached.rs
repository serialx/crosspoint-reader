//! Read-only section-cache queries used while mapping a saved reader position.
use super::{file::CacheFile, owner::ReaderSection};
use crate::{
    app::{
        abi,
        epub::{abi::XpEpubLeaves, Text},
    },
    section_cache::{self, Query},
};
use core::{ffi::c_void, ptr::NonNull};

pub(in crate::app::epub) struct CachedSection {
    path: Text,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
}
impl CachedSection {
    pub(in crate::app::epub) unsafe fn open(
        book: NonNull<c_void>,
        spine: i32,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> Option<Self> {
        Some(Self {
            path: unsafe { ReaderSection::cache_path(book, spine) }?,
            resources,
            leaves,
        })
    }
    pub(in crate::app::epub) fn query(&self, operation: Query) -> Option<u16> {
        let mut file = CacheFile::new(&self.path, self.resources, self.leaves);
        let mut out = [0; 3];
        section_cache::query(&mut file, operation, &mut out)?;
        Some(out[0] as u16)
    }
    pub(in crate::app::epub) fn anchor(&self, anchor: &[u8]) -> Option<u16> {
        let mut file = CacheFile::new(&self.path, self.resources, self.leaves);
        section_cache::anchor(&mut file, anchor).map(|page| page as u16)
    }
}
