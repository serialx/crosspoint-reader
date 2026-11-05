#[cfg(not(feature = "app-test"))]
pub(super) use super::page_owner::{Page, Pending};
#[cfg(feature = "app-test")]
pub(super) use super::test_support::legacy_page::{Page, Pending};
use super::{abi, app_abi};
use core::{ffi::c_void, ptr::NonNull};

pub(super) enum Factory<'a> {
    Footnote {
        left: i32,
        top: i32,
    },
    Dictionary {
        left: i32,
        top: i32,
        initial: app_abi::Text,
    },
    Selection(&'a abi::XpEpubSelectionArgs),
}

impl super::page_owner::Pending {
    pub(super) fn launch(
        &mut self,
        leaves: &abi::XpEpubLeaves,
        factory: Factory<'_>,
    ) -> Option<NonNull<c_void>> {
        let (records, count) = self.records();
        let child = unsafe {
            match factory {
                Factory::Footnote { left, top } => (leaves.child_footnote)(records, left, top),
                Factory::Dictionary { left, top, initial } => {
                    (leaves.child_dictionary)(records, left, top, initial)
                }
                Factory::Selection(args) => (leaves.child_selection)(records, count, args),
            }
        };
        self.completed(!child.is_null());
        NonNull::new(child)
    }
}

impl super::EpubSession {
    pub(super) unsafe fn load_page(
        &self,
        section: super::session_section::Handle,
        index: i32,
    ) -> Option<Page> {
        #[cfg(feature = "app-test")]
        if let (super::session_section::Handle::Reader(raw), Some(services)) =
            (section, self.legacy_pages.get())
        {
            return unsafe {
                super::section_owner::ReaderSection::with_page_file(
                    raw,
                    index,
                    |file, bytes, visible| file.load_legacy_page(services, bytes, visible),
                )
            };
        }
        unsafe { super::session_section::Section::load_page(section, index) }
    }

    pub(super) unsafe fn page_text(
        &self,
        section: super::session_section::Handle,
        index: i32,
    ) -> Option<super::Text> {
        #[cfg(feature = "app-test")]
        if let (super::session_section::Handle::Reader(_), Some(_)) =
            (section, self.legacy_pages.get())
        {
            let Some(page) = (unsafe { self.load_page(section, index) }) else {
                return Some(super::Text::default());
            };
            return super::test_support::legacy_page::page_text(&page, self.leaves);
        }
        unsafe { super::session_section::Section::page_text(section, index) }
    }
}
