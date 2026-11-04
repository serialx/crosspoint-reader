//! Historical synthetic Page oracle services; excluded from firmware.
use super::super::{abi::*, app_abi::Text as XpAppText};

pub type XpEpubHighlight = unsafe extern "C" fn(
    context: *mut core::ffi::c_void,
    page: *mut core::ffi::c_void,
    font: i32,
    left: i32,
    top: i32,
);

#[repr(C)]
pub struct LegacyPageServices {
    pub page_close: unsafe extern "C" fn(page: *mut core::ffi::c_void),
    pub page_info: unsafe extern "C" fn(page: *mut core::ffi::c_void, out: *mut XpEpubPageInfo),
    pub page_element:
        unsafe extern "C" fn(page: *mut core::ffi::c_void, index: u16, out: *mut XpEpubElement),
    pub page_word: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        element: u16,
        word: u16,
        out: *mut XpEpubWord,
    ),
    pub metadata_assign:
        unsafe extern "C" fn(resources: *mut core::ffi::c_void, page: *mut core::ffi::c_void) -> u8,
    pub metadata_clear: unsafe extern "C" fn(resources: *mut core::ffi::c_void),
    pub metadata_footnotes: unsafe extern "C" fn(resources: *mut core::ffi::c_void) -> u16,
    pub metadata_links: unsafe extern "C" fn(resources: *mut core::ffi::c_void) -> u16,
    pub metadata_footnote: unsafe extern "C" fn(
        resources: *mut core::ffi::c_void,
        index: u16,
        sink: XpEpubFootnoteSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub metadata_link: unsafe extern "C" fn(
        resources: *mut core::ffi::c_void,
        index: u16,
        sink: XpEpubLinkSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub page_word_font: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        element: u16,
        word: u16,
        reader_font: i32,
    ) -> i32,
    pub page_line_font:
        unsafe extern "C" fn(page: *mut core::ffi::c_void, element: u16, reader_font: i32) -> i32,
    pub page_word_y_offset: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        element: u16,
        word: u16,
        reader_font: i32,
    ) -> i32,
    pub page_line_ruby_shift:
        unsafe extern "C" fn(page: *mut core::ffi::c_void, element: u16, line_font: i32) -> i32,
    pub page_render:
        unsafe extern "C" fn(page: *mut core::ffi::c_void, font: i32, left: i32, top: i32),
    pub page_load: unsafe extern "C" fn(*mut core::ffi::c_void, u32, u32) -> *mut core::ffi::c_void,
    pub render_bind_page: unsafe extern "C" fn(
        resources: *mut core::ffi::c_void,
        page: *mut core::ffi::c_void,
        font: i32,
        left: i32,
        top: i32,
        bottom: i32,
        highlight: XpEpubHighlight,
        context: *mut core::ffi::c_void,
    ),
    pub child_footnote: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        left: i32,
        top: i32,
    ) -> *mut core::ffi::c_void,
    pub child_dictionary: unsafe extern "C" fn(
        page: *mut core::ffi::c_void,
        left: i32,
        top: i32,
        initial_text: XpAppText,
    ) -> *mut core::ffi::c_void,
    pub child_selection: unsafe extern "C" fn(
        pages: *mut *mut core::ffi::c_void,
        count: u8,
        args: *const XpEpubSelectionArgs,
    ) -> *mut core::ffi::c_void,
}

use super::super::{page::Factory, page_owner, page_view};
use core::{ffi::c_void, ptr::NonNull};

pub(in crate::app::epub) struct LegacyPage {
    handle: NonNull<c_void>,
    services: &'static LegacyPageServices,
}
impl Drop for LegacyPage {
    fn drop(&mut self) {
        unsafe { (self.services.page_close)(self.handle.as_ptr()) };
    }
}
pub(in crate::app::epub) enum Page {
    Reader(page_owner::Page),
    Legacy(LegacyPage),
}
impl Page {
    pub(in crate::app::epub) unsafe fn from_raw(
        handle: *mut c_void,
        services: &'static LegacyPageServices,
    ) -> Option<Self> {
        NonNull::new(handle).map(|handle| Self::Legacy(LegacyPage { handle, services }))
    }
    pub(in crate::app::epub) fn borrow(&self) -> page_view::View<'_> {
        match self {
            Self::Reader(page) => page.borrow(),
            Self::Legacy(page) => page_view::View::Legacy(page.handle, page.services),
        }
    }
    pub(in crate::app::epub) fn reader(&self) -> &page_owner::Page {
        match self {
            Self::Reader(page) => page,
            Self::Legacy(_) => panic!("real-owner composition required"),
        }
    }
}

pub(in crate::app::epub) enum Pending {
    Reader(page_owner::Pending),
    Legacy([Option<Page>; 3]),
}
impl Pending {
    pub(in crate::app::epub) fn new(mut pages: [Option<Page>; 3]) -> Self {
        if matches!(pages[0], Some(Page::Legacy(_))) {
            assert!(pages
                .iter()
                .all(|page| !matches!(page, Some(Page::Reader(_)))));
            return Self::Legacy(pages);
        }
        Self::Reader(page_owner::Pending::new(core::array::from_fn(|i| {
            pages[i].take().map(|page| match page {
                Page::Reader(page) => page,
                Page::Legacy(_) => panic!("mixed Page owners"),
            })
        })))
    }
    pub(in crate::app::epub) fn launch(
        &mut self,
        leaves: &XpEpubLeaves,
        factory: Factory<'_>,
    ) -> Option<NonNull<c_void>> {
        let Self::Legacy(pages) = self else {
            let Self::Reader(pending) = self else {
                unreachable!()
            };
            return pending.launch(leaves, factory);
        };
        let Some(Page::Legacy(first)) = &pages[0] else {
            unreachable!()
        };
        let services = first.services;
        let mut handles = pages.each_ref().map(|page| {
            page.as_ref().map_or(core::ptr::null_mut(), |page| {
                page.borrow().fixture_pointer()
            })
        });
        let count = pages.iter().take_while(|page| page.is_some()).count();
        let child = unsafe {
            match factory {
                Factory::Footnote { left, top } => (services.child_footnote)(handles[0], left, top),
                Factory::Dictionary { left, top, initial } => {
                    (services.child_dictionary)(handles[0], left, top, initial)
                }
                Factory::Selection(args) => {
                    (services.child_selection)(handles.as_mut_ptr(), count as u8, args)
                }
            }
        };
        if !child.is_null() {
            for page in pages.iter_mut() {
                if let Some(page) = page.take() {
                    core::mem::forget(page);
                }
            }
        }
        NonNull::new(child)
    }
}
impl Drop for Pending {
    fn drop(&mut self) {
        if let Self::Legacy(pages) = self {
            for page in pages.iter_mut().rev() {
                drop(page.take());
            }
        }
    }
}

// Kept only for the explicitly installed physical Page fake in App lifecycle cases.
pub(in crate::app::epub) fn page_text(
    page: &Page,
    leaves: &XpEpubLeaves,
) -> Option<super::super::Text> {
    let words = |visit: &mut dyn FnMut(&[u8]) -> Option<()>| -> Option<()> {
        let view = page.borrow();
        for element in 0..view.info(leaves).elements {
            let row = view.element(element);
            if row.tag != 1 || row.valid == 0 {
                continue;
            }
            for index in 0..row.words {
                visit(unsafe { view.word(element, index).text.bytes() }?)?;
            }
        }
        Some(())
    };
    let mut length = 0usize;
    words(&mut |word| {
        length = length
            .checked_add(usize::from(length != 0))?
            .checked_add(word.len())?;
        Some(())
    })?;
    let mut valid = true;
    let text = super::super::Text::read(length, |out| {
        let mut previous = 0;
        valid = words(&mut |word| {
            previous +=
                xp_core::section::encode_text_word(previous, word, out.get_mut(previous..)?)
                    .ok()?;
            Some(())
        })
        .is_some()
            && previous == length;
    })
    .ok()?;
    valid.then_some(text)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_install_page_fake(
    owner: *mut c_void,
    services: &'static LegacyPageServices,
) {
    let owner = unsafe { &*owner.cast::<super::super::EpubSession>() };
    assert!(owner.legacy_pages.get().is_none());
    owner.legacy_pages.set(Some(services));
}
