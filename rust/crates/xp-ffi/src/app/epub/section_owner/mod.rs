//! Reader Section owns cache storage and incremental Chapter lifetime.
use super::Text;
use crate::{app::abi, section_cache};

mod book;
mod diagnostics;
mod driver;
mod file;
mod image;
mod input;
mod lifecycle;
mod owner;
mod queries;
mod text;
mod toc;
pub(super) use image::FramebufferLoan;
pub(super) use owner::ReaderSection;
pub(super) use services::Popup;
mod records;
mod services;
use records::Records;

#[cfg(feature = "app-test")]
mod driver_tests;
#[cfg(feature = "app-test")]
mod fixture;
#[cfg(feature = "app-test")]
mod tests;

struct PagePosition {
    offset: u32,
    visible: u32,
    paragraph: u16,
    list: u16,
}

struct Anchor {
    text: Text,
    page: u16,
}

struct BuildRecords {
    pages: Records<PagePosition, 64>,
    anchors: Records<Anchor, 32>,
}

impl BuildRecords {
    fn new(resources: &'static abi::Resources) -> Self {
        Self {
            pages: Records::new(resources),
            anchors: Records::new(resources),
        }
    }

    #[cfg(feature = "app-test")]
    fn push_page(&mut self, value: PagePosition) -> Result<(), ()> {
        self.pages.reserve_one()?;
        self.pages.push_reserved(value);
        Ok(())
    }

    fn push_anchor(&mut self, pending: &mut Text, page: u16) -> Result<(), ()> {
        self.anchors.reserve_one()?;
        self.anchors.push_reserved(Anchor {
            text: core::mem::take(pending),
            page,
        });
        Ok(())
    }
}

impl section_cache::Records for BuildRecords {
    fn entries(&self) -> usize {
        self.pages.len()
    }
    fn anchors(&self) -> usize {
        self.anchors.len()
    }
    fn offset(&self, index: usize) -> u32 {
        self.pages.get(index).unwrap().offset
    }
    fn paragraph(&self, index: usize) -> u32 {
        self.pages.get(index).unwrap().paragraph.into()
    }
    fn list(&self, index: usize) -> u32 {
        self.pages.get(index).unwrap().list.into()
    }
    fn visible(&self, index: usize) -> u32 {
        self.pages.get(index).unwrap().visible
    }
    fn anchor_page(&self, index: usize) -> u16 {
        self.anchors.get(index).unwrap().page
    }
    fn anchor(
        &self,
        index: usize,
        limit: Option<u16>,
    ) -> Result<Option<section_cache::Anchor<'_>>, ()> {
        let anchor = self.anchors.get(index).unwrap();
        Ok(if limit.is_some_and(|limit| anchor.page >= limit) {
            None
        } else {
            Some(section_cache::Anchor {
                text: anchor.text.bytes(),
                page: anchor.page,
            })
        })
    }
}

#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<PagePosition>() == 12);
    assert!(core::mem::align_of::<PagePosition>() == 4);
    assert!(core::mem::size_of::<Anchor>() == 16);
    assert!(core::mem::align_of::<Anchor>() == 4);
    assert!(core::mem::size_of::<Records<PagePosition, 64>>() == 20);
};

// Guarded stores ownership; ResourceAccess alone exposes its checked pointer.
#[cfg(not(feature = "app-test"))]
pub(super) struct SectionSlot(core::ptr::NonNull<ReaderSection>);
#[cfg(not(feature = "app-test"))]
impl SectionSlot {
    pub(super) unsafe fn new(raw: core::ptr::NonNull<ReaderSection>) -> Self {
        Self(raw)
    }
    pub(super) fn pointer(&self) -> core::ptr::NonNull<ReaderSection> {
        self.0
    }
}
#[cfg(not(feature = "app-test"))]
impl Drop for SectionSlot {
    fn drop(&mut self) {
        unsafe { ReaderSection::close(self.0) };
    }
}
