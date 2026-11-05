// Synthetic book inputs for frozen session oracles; never linked into firmware.
use super::super::{abi::*, app_abi};
extern crate alloc;
use alloc::boxed::Box;
type XpAppText = app_abi::Text;
type XpAppTextSink = app_abi::TextSink;
#[repr(C)]
pub struct LegacyBookServices {
    pub section: super::legacy_section::LegacySectionServices,
    pub book_open: unsafe extern "C" fn(path: XpAppText) -> *mut core::ffi::c_void,
    pub book_load:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, build_missing: u8, skip_css: u8) -> u8,
    pub book_close: unsafe extern "C" fn(book: *mut core::ffi::c_void),
    pub book_size: unsafe extern "C" fn(book: *mut core::ffi::c_void) -> u32,
    pub book_spine_count: unsafe extern "C" fn(book: *mut core::ffi::c_void) -> i32,
    pub book_toc_count: unsafe extern "C" fn(book: *mut core::ffi::c_void) -> i32,
    pub book_path: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_cache_path: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_title: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_author: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_language: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_spine: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        index: i32,
        sink: XpEpubSpineSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_toc: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        index: i32,
        sink: XpEpubTocSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub book_toc_for_spine: unsafe extern "C" fn(book: *mut core::ffi::c_void, index: i32) -> i32,
    pub book_cumulative_size: unsafe extern "C" fn(book: *mut core::ffi::c_void, spine: i32) -> u32,
    pub book_spine_for_size: unsafe extern "C" fn(book: *mut core::ffi::c_void, bytes: u32) -> i32,
    pub book_text_reference: unsafe extern "C" fn(book: *mut core::ffi::c_void) -> i32,
    pub book_progress:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, spine: i32, fraction: f32) -> f32,
    pub book_resolve_href:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, href: XpAppText) -> i32,
    pub book_setup_cache: unsafe extern "C" fn(book: *mut core::ffi::c_void),
    pub book_clear_cache: unsafe extern "C" fn(book: *mut core::ffi::c_void),
    pub book_install_extractor: unsafe extern "C" fn(book: *mut core::ffi::c_void),
    pub book_clear_extractor: unsafe extern "C" fn(),
    pub book_protection_error: unsafe extern "C" fn(
        book: *mut core::ffi::c_void,
        sink: XpAppTextSink,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub bookmarks_load:
        unsafe extern "C" fn(bookmarks: *mut core::ffi::c_void, book: *mut core::ffi::c_void) -> u8,
    pub bookmarks_save:
        unsafe extern "C" fn(bookmarks: *mut core::ffi::c_void, book: *mut core::ffi::c_void) -> u8,
    pub clippings_load: unsafe extern "C" fn(book: *mut core::ffi::c_void) -> u8,
    pub recents_add: unsafe extern "C" fn(book: *mut core::ffi::c_void),
}

// Only this named constructor opts an application fixture into synthetic books.
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_install_epub_legacy_book(
    path: app_abi::Text,
    fast_initial: u8,
    leaves: *const XpEpubLeaves,
    services: &'static LegacyBookServices,
) -> u8 {
    let (Some(app), Some(path), Some(leaves)) =
        (crate::app::app(), unsafe { path.bytes() }, unsafe {
            leaves.as_ref()
        })
    else {
        return 0;
    };
    let Some(mut owner) = super::super::EpubSession::try_owned_legacy(
        path,
        fast_initial != 0,
        leaves,
        app.runtime,
        app.resources,
        services,
    ) else {
        return 0;
    };
    owner.book_factory = super::super::session_book::Factory::Legacy(services);
    owner.legacy_pages.set(Some(&services.section.page));
    app.replace(crate::app::Owner::Epub(owner));
    1
}

impl super::super::EpubSession {
    pub(in crate::app) fn try_owned_legacy(
        path: &[u8],
        fast: bool,
        leaves: &'static XpEpubLeaves,
        runtime: &'static app_abi::Runtime,
        resources: &'static app_abi::Resources,
        fixture: &'static LegacyBookServices,
    ) -> Option<Box<Self>> {
        let mut owner = Self::try_owned_with_fixture(
            path,
            fast,
            leaves,
            runtime,
            resources,
            Some(&fixture.section.page),
        )?;
        owner.book_factory = super::super::session_book::Factory::Legacy(fixture);
        Some(owner)
    }
    pub(in crate::app) fn try_owned_legacy_render(
        path: &[u8],
        fast: bool,
        leaves: &'static XpEpubLeaves,
        runtime: &'static app_abi::Runtime,
        resources: &'static app_abi::Resources,
        fixture: &'static super::legacy_page::LegacyPageServices,
    ) -> Option<Box<Self>> {
        Self::try_owned_with_fixture(path, fast, leaves, runtime, resources, Some(fixture))
    }
    pub(in crate::app) fn legacy_page_fixture(
        &self,
    ) -> Option<&'static super::legacy_page::LegacyPageServices> {
        self.legacy_pages.get()
    }
    pub(in crate::app) fn legacy_book_fixture(&self) -> Option<&'static LegacyBookServices> {
        match self.book_factory {
            super::super::session_book::Factory::Legacy(f) => Some(f),
            _ => None,
        }
    }
}

// Only named routing oracles may install this fixture for one real App event.
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_event_legacy_book(
    event: *const app_abi::Event,
    services: &'static LegacyBookServices,
) -> app_abi::EventResult {
    let Some(app) = crate::app::app() else {
        return app_abi::EVENT_INVALID;
    };
    struct Entry<'a>(&'a core::cell::Cell<Option<&'static LegacyBookServices>>);
    impl Drop for Entry<'_> {
        fn drop(&mut self) {
            self.0.set(None);
        }
    }
    assert!(app.legacy_entry_fixture.get().is_none());
    app.legacy_entry_fixture.set(Some(services));
    let entry = Entry(&app.legacy_entry_fixture);
    let result = unsafe { crate::app::xp_app_event(event) };
    drop(entry);
    assert!(app.legacy_entry_fixture.get().is_none());
    result
}
