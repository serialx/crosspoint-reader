use super::{file::CacheFile, owner::ReaderSection};
use crate::section_cache::Query;
use core::{
    ffi::c_void,
    ptr::{self, NonNull},
};

impl ReaderSection {
    pub(in crate::app::epub) unsafe fn page_count(raw: NonNull<Self>) -> u16 {
        let owner = raw.as_ptr();
        unsafe { (*owner).build.as_ref() }.map_or(unsafe { (*owner).readable.pages }, |build| {
            build.services().pages.get()
        })
    }
    pub(in crate::app::epub) unsafe fn building(raw: NonNull<Self>) -> bool {
        unsafe { (*raw.as_ptr()).build.is_some() }
    }
    pub(in crate::app::epub) unsafe fn complete(raw: NonNull<Self>) -> bool {
        unsafe { (*raw.as_ptr()).complete }
    }
    pub(in crate::app::epub) unsafe fn partial(raw: NonNull<Self>) -> bool {
        unsafe { (*raw.as_ptr()).readable.partial != 0 }
    }
    pub(in crate::app::epub) unsafe fn estimated_pages(raw: NonNull<Self>) -> i32 {
        let owner = raw.as_ptr();
        let pages = unsafe { Self::page_count(raw) };
        let readable = unsafe { (*owner).readable };
        let clamp = |value: u64| {
            if value <= u64::from(pages) {
                pages
            } else {
                value.min(60000) as u16
            }
        };
        let partial = || {
            if readable.partial == 0
                || readable.consumed == 0
                || readable.total <= readable.consumed
            {
                pages
            } else {
                clamp(
                    u64::from(readable.partial_pages) * u64::from(readable.total)
                        / u64::from(readable.consumed),
                )
            }
        };
        let Some(build) = (unsafe { (*owner).build.as_ref() }) else {
            return partial().into();
        };
        let built = build.services().built.get();
        let progress = unsafe { &mut (*owner).progress };
        if built == 0 || progress.consumed == 0 || progress.total <= progress.consumed {
            return partial().into();
        }
        let raw = u64::from(built) * u64::from(progress.total) / u64::from(progress.consumed);
        if progress.smoothed <= 0.0 {
            progress.smoothed = raw as f32;
        } else if progress.consumed != progress.smoothed_at {
            progress.smoothed += 0.25 * (raw as f32 - progress.smoothed);
        }
        progress.smoothed_at = progress.consumed;
        clamp((progress.smoothed + 0.5) as u64).into()
    }
    pub(in crate::app::epub) unsafe fn reached_offset(raw: NonNull<Self>, offset: u32) -> bool {
        let Some(build) = (unsafe { (*raw.as_ptr()).build.as_ref() }) else {
            return false;
        };
        let records = build.services().records();
        records
            .pages
            .len()
            .checked_sub(1)
            .and_then(|i| records.pages.get(i))
            .is_some_and(|entry| offset <= entry.visible)
    }
    pub(in crate::app::epub) unsafe fn page_for_offset(
        raw: NonNull<Self>,
        offset: u32,
        first: bool,
    ) -> Option<u16> {
        if unsafe { Self::reached_offset(raw, offset) } {
            let records = unsafe { (*raw.as_ptr()).build.as_ref().unwrap().services().records() };
            let mut result = 0;
            for i in 0..records.pages.len() {
                let start = records.pages.get(i).unwrap().visible;
                if first && start == offset {
                    return Some(i as u16);
                }
                if start > offset {
                    break;
                }
                result = i as u16;
            }
            return Some(result);
        }
        unsafe { Self::query(raw, Query::PageForVisible { offset, first }) }
            .map(|row| row[0] as u16)
    }
    pub(in crate::app::epub) unsafe fn offset_for_page(
        raw: NonNull<Self>,
        page: u16,
    ) -> Option<u32> {
        if let Some(entry) = unsafe { (*raw.as_ptr()).build.as_ref() }
            .and_then(|build| build.services().records().pages.get(usize::from(page)))
        {
            return Some(entry.visible);
        }
        unsafe { Self::query(raw, Query::VisibleForPage(page.into())) }.map(|row| row[0])
    }
    pub(in crate::app::epub) unsafe fn paragraph_for_page(
        raw: NonNull<Self>,
        page: u16,
    ) -> Option<u16> {
        unsafe { Self::query(raw, Query::ParagraphForPage(page.into())) }.map(|row| row[0] as u16)
    }
    pub(super) unsafe fn build_anchor(raw: NonNull<Self>, needle: &[u8]) -> Option<u16> {
        let build = unsafe { (*raw.as_ptr()).build.as_ref() }?;
        let records = build.services().records();
        (0..records.anchors.len()).find_map(|index| {
            let anchor = records.anchors.get(index).unwrap();
            (anchor.text.bytes() == needle).then_some(anchor.page)
        })
    }
    pub(in crate::app::epub) unsafe fn find_anchor(
        raw: NonNull<Self>,
        needle: &[u8],
        cached: bool,
    ) -> Option<u16> {
        if !cached {
            if let Some(page) = unsafe { Self::build_anchor(raw, needle) } {
                return Some(page);
            }
        }
        unsafe { Self::anchor(raw, needle) }.map(|page| page as u16)
    }
    pub(in crate::app::epub) unsafe fn load_page(raw: NonNull<Self>, page: i32) -> *mut c_void {
        let Ok(index) = usize::try_from(page) else {
            return ptr::null_mut();
        };
        let owner = raw.as_ptr();
        if let Some(build) = unsafe { (*owner).build.as_ref() } {
            let services = build.services();
            let records = services.records();
            if let Some(entry) = records.pages.get(index) {
                return services.output(|file| {
                    if !file.is_open() || entry.offset == 0 {
                        return ptr::null_mut();
                    }
                    let write = file.physical_position();
                    let end = records
                        .pages
                        .get(index + 1)
                        .map_or(write, |entry| entry.offset);
                    if end <= entry.offset || end > write || !file.seek(entry.offset) {
                        return ptr::null_mut();
                    }
                    let page = file.load_page(end - entry.offset, entry.visible);
                    // The original adapter returns the Page even if this restore fails.
                    file.seek(write);
                    page
                });
            }
        }
        let readable = unsafe { (*owner).readable };
        let disk = if readable.partial != 0 {
            readable.partial_pages
        } else if unsafe { (*owner).build.is_some() } {
            0
        } else {
            readable.pages
        };
        if index >= usize::from(disk) {
            return ptr::null_mut();
        }
        unsafe { CacheFile::new(&(*owner).path, (*owner).resources, (*owner).leaves) }
            .load_page(page as u32)
    }
}
