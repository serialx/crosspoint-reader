//! Named historical reader-policy fixtures; never part of the firmware type graph.
use super::super::{
    abi::*,
    app_abi,
    section_owner::{Popup, ReaderSection},
    session_book::Loaded,
    Text,
};
use core::{ffi::c_void, ptr::NonNull};
type XpAppText = app_abi::Text;
type XpAppTextSink = app_abi::TextSink;

#[repr(C)]
pub struct LegacySectionServices {
    pub page: super::legacy_page::LegacyPageServices,
    pub section_open:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, spine: i32) -> *mut core::ffi::c_void,
    pub section_close: unsafe extern "C" fn(section: *mut core::ffi::c_void),
    pub section_page_count: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u16,
    pub section_estimated_pages: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> i32,
    pub section_building: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u8,
    pub section_complete: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u8,
    pub section_partial: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u8,
    pub section_load_cache:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, spec: *const XpEpubRenderSpec) -> u8,
    pub section_create_cache: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        spec: *const XpEpubRenderSpec,
        popup: XpEpubBuildPopup,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub section_start_build: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        spec: *const XpEpubRenderSpec,
        popup: Option<XpEpubBuildPopup>,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub section_build_more: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        max_pages: i32,
        yield_on_input: u8,
    ) -> u8,
    pub section_abandon_build: unsafe extern "C" fn(section: *mut core::ffi::c_void),
    pub section_clear_cache: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u8,
    pub section_html_cached: unsafe extern "C" fn(section: *mut core::ffi::c_void) -> u8,
    pub section_find_anchor: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        anchor: XpAppText,
        page: *mut u16,
    ) -> u8,
    pub section_cached_anchor: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        anchor: XpAppText,
        page: *mut u16,
    ) -> u8,
    pub section_toc_for_page: unsafe extern "C" fn(
        section_or_null: *mut core::ffi::c_void,
        book: *mut core::ffi::c_void,
        spine: i32,
        page: i32,
    ) -> i32,
    pub section_page_for_offset: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        offset: u32,
        prefer_first: u8,
        page: *mut u16,
    ) -> u8,
    pub section_offset_for_page:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, page: u16, offset: *mut u32) -> u8,
    pub section_paragraph_for_page:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, page: u16, paragraph: *mut u16) -> u8,
    pub section_reached_offset:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, offset: u32) -> u8,
    pub section_load_page:
        unsafe extern "C" fn(section: *mut core::ffi::c_void, page: i32) -> *mut core::ffi::c_void,
    pub section_page_text: unsafe extern "C" fn(
        section: *mut core::ffi::c_void,
        page: i32,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
}

#[derive(Clone, Copy)]
pub(in crate::app::epub) enum Handle {
    Reader(NonNull<ReaderSection>),
    Legacy {
        pointer: NonNull<c_void>,
        services: &'static LegacySectionServices,
    },
}
impl Handle {
    pub(in crate::app::epub) fn fixture_pointer(self) -> *mut c_void {
        match self {
            Self::Reader(raw) => raw.as_ptr().cast(),
            Self::Legacy { pointer, .. } => pointer.as_ptr(),
        }
    }
}
pub(in crate::app::epub) struct SectionSlot(Handle);
impl SectionSlot {
    pub(in crate::app::epub) unsafe fn new(handle: Handle) -> Self {
        Self(handle)
    }
    pub(in crate::app::epub) fn pointer(&self) -> Handle {
        self.0
    }
    pub(in crate::app::epub) fn legacy(
        pointer: *mut c_void,
        services: &'static LegacySectionServices,
    ) -> Option<Self> {
        NonNull::new(pointer).map(|pointer| Self(Handle::Legacy { pointer, services }))
    }
    // Some historical seeds borrow their fake Section; their fixture retains destruction.
    pub(in crate::app::epub) fn return_fixture(self) {
        assert!(matches!(self.0, Handle::Legacy { .. }));
        core::mem::forget(self);
    }
}
impl Drop for SectionSlot {
    fn drop(&mut self) {
        unsafe {
            match self.0 {
                Handle::Reader(raw) => ReaderSection::close(raw),
                Handle::Legacy { pointer, services } => (services.section_close)(pointer.as_ptr()),
            }
        }
    }
}

pub(in crate::app::epub) struct Section;
impl Section {
    pub(in crate::app::epub) unsafe fn open(
        book: NonNull<c_void>,
        spine: i32,
        resources: &'static app_abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> Option<Handle> {
        match unsafe { &*book.cast::<Loaded>().as_ptr() } {
            Loaded::Reader(book) => {
                unsafe { ReaderSection::open(*book, spine, resources, leaves) }.map(Handle::Reader)
            }
            Loaded::Legacy {
                handle, services, ..
            } => NonNull::new(unsafe { (services.section.section_open)(handle.as_ptr(), spine) })
                .map(|pointer| Handle::Legacy {
                    pointer,
                    services: &services.section,
                }),
        }
    }
    pub(in crate::app::epub) unsafe fn page_count(handle: Handle) -> u16 {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::page_count(raw),
                Handle::Legacy { pointer, services } => {
                    (services.section_page_count)(pointer.as_ptr())
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn estimated_pages(handle: Handle) -> i32 {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::estimated_pages(raw),
                Handle::Legacy { pointer, services } => {
                    (services.section_estimated_pages)(pointer.as_ptr())
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn building(handle: Handle) -> bool {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::building(raw),
                Handle::Legacy { pointer, services } => {
                    (services.section_building)(pointer.as_ptr()) != 0
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn complete(handle: Handle) -> bool {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::complete(raw),
                Handle::Legacy { pointer, services } => {
                    (services.section_complete)(pointer.as_ptr()) != 0
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn partial(handle: Handle) -> bool {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::partial(raw),
                Handle::Legacy { pointer, services } => {
                    (services.section_partial)(pointer.as_ptr()) != 0
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn html_cached(handle: Handle) -> bool {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::html_cached(raw),
                Handle::Legacy { pointer, services } => {
                    (services.section_html_cached)(pointer.as_ptr()) != 0
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn load_cache(handle: Handle, spec: &XpEpubRenderSpec) -> bool {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::load_cache(raw, spec),
                Handle::Legacy { pointer, services } => {
                    (services.section_load_cache)(pointer.as_ptr(), spec) != 0
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn reached_offset(handle: Handle, offset: u32) -> bool {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::reached_offset(raw, offset),
                Handle::Legacy { pointer, services } => {
                    (services.section_reached_offset)(pointer.as_ptr(), offset) != 0
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn load_page(
        handle: Handle,
        page: i32,
    ) -> Option<super::legacy_page::Page> {
        unsafe {
            match handle {
                Handle::Reader(raw) => {
                    ReaderSection::load_page(raw, page).map(super::legacy_page::Page::Reader)
                }
                Handle::Legacy { pointer, services } => super::legacy_page::Page::from_raw(
                    (services.section_load_page)(pointer.as_ptr(), page),
                    &services.page,
                ),
            }
        }
    }
    pub(in crate::app::epub) unsafe fn abandon(handle: Handle) {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::abandon(raw),
                Handle::Legacy { pointer, services } => {
                    (services.section_abandon_build)(pointer.as_ptr())
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn clear(handle: Handle) -> bool {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::clear(raw),
                Handle::Legacy { pointer, services } => {
                    (services.section_clear_cache)(pointer.as_ptr()) != 0
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn advance(
        handle: Handle,
        max: i32,
        yield_input: bool,
    ) -> bool {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::advance(raw, max, yield_input),
                Handle::Legacy { pointer, services } => {
                    (services.section_build_more)(pointer.as_ptr(), max, u8::from(yield_input)) != 0
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn start(
        handle: Handle,
        spec: &XpEpubRenderSpec,
        popup: Option<Popup>,
        runtime: &'static app_abi::Runtime,
    ) -> bool {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::start(raw, spec, popup, runtime),
                Handle::Legacy { pointer, services } => {
                    (services.section_start_build)(
                        pointer.as_ptr(),
                        spec,
                        popup.map(|p| p.call),
                        popup.map_or(core::ptr::null_mut(), |p| p.context),
                    ) != 0
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn create(
        handle: Handle,
        spec: &XpEpubRenderSpec,
        popup: Option<Popup>,
        runtime: &'static app_abi::Runtime,
    ) -> bool {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::create(raw, spec, popup, runtime),
                Handle::Legacy { pointer, services } => {
                    (services.section_create_cache)(
                        pointer.as_ptr(),
                        spec,
                        popup.expect("create popup").call,
                        popup.map_or(core::ptr::null_mut(), |p| p.context),
                    ) != 0
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn page_for_offset(
        handle: Handle,
        offset: u32,
        first: bool,
    ) -> Option<u16> {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::page_for_offset(raw, offset, first),
                Handle::Legacy { pointer, services } => {
                    let mut result = 0;
                    ((services.section_page_for_offset)(
                        pointer.as_ptr(),
                        offset,
                        u8::from(first),
                        &mut result,
                    ) != 0)
                        .then_some(result)
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn offset_for_page(handle: Handle, page: u16) -> Option<u32> {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::offset_for_page(raw, page),
                Handle::Legacy { pointer, services } => {
                    let mut result = 0;
                    ((services.section_offset_for_page)(pointer.as_ptr(), page, &mut result) != 0)
                        .then_some(result)
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn paragraph_for_page(
        handle: Handle,
        page: u16,
    ) -> Option<u16> {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::paragraph_for_page(raw, page),
                Handle::Legacy { pointer, services } => {
                    let mut result = 0;
                    ((services.section_paragraph_for_page)(pointer.as_ptr(), page, &mut result)
                        != 0)
                        .then_some(result)
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn find_anchor(
        handle: Handle,
        needle: &[u8],
        cached: bool,
    ) -> Option<u16> {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::find_anchor(raw, needle, cached),
                Handle::Legacy { pointer, services } => {
                    let anchor = Text::copy(needle).ok()?;
                    let call = if cached {
                        services.section_cached_anchor
                    } else {
                        services.section_find_anchor
                    };
                    let mut result = 0;
                    (call(pointer.as_ptr(), anchor.wire(), &mut result) != 0).then_some(result)
                }
            }
        }
    }
    pub(in crate::app::epub) unsafe fn page_text(handle: Handle, page: i32) -> Option<Text> {
        unsafe {
            match handle {
                Handle::Reader(raw) => ReaderSection::page_text(raw, page),
                Handle::Legacy { pointer, services } => Text::receive(|sink, context| {
                    (services.section_page_text)(pointer.as_ptr(), page, sink, context)
                }),
            }
        }
    }
    pub(in crate::app::epub) unsafe fn select_toc(
        section: Option<Handle>,
        book: NonNull<c_void>,
        spine: i32,
        page: i32,
        resources: &'static app_abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> i32 {
        match unsafe { &*book.cast::<Loaded>().as_ptr() } {
            Loaded::Reader(book) => {
                let section = section.map(|section| match section {
                    Handle::Reader(raw) => raw,
                    Handle::Legacy { .. } => panic!("real book with legacy Section"),
                });
                unsafe { ReaderSection::select_toc(section, *book, spine, page, resources, leaves) }
            }
            Loaded::Legacy {
                handle, services, ..
            } => {
                let pointer = section.map_or(core::ptr::null_mut(), |section| match section {
                    Handle::Legacy { pointer, .. } => pointer.as_ptr(),
                    Handle::Reader(_) => panic!("legacy book with real Section"),
                });
                unsafe {
                    (services.section.section_toc_for_page)(pointer, handle.as_ptr(), spine, page)
                }
            }
        }
    }
}
