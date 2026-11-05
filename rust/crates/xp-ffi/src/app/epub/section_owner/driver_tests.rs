//! Hook-free construction and transitions of the production reader Section.
use super::super::{
    abi::{XpEpubLeaves, XpEpubRenderSpec},
    book_owner::{queries, ReaderBook},
    Text,
};
use super::{owner::ReaderSection, services::Popup};
use crate::{app::abi, section_cache::Query};
use core::ffi::c_void;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_driver(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    runtime: &'static abi::Runtime,
    source: abi::Text,
    cache: abi::Text,
    partial: u8,
    with_popup: u8,
    native: unsafe extern "C" fn(
        *mut c_void,
        *const super::super::abi::XpEpubPageView,
        u16,
        u8,
        u32,
    ),
) -> u8 {
    let mut path = Text::copy(unsafe { source.bytes() }.unwrap()).unwrap();
    let cache = Text::copy(unsafe { cache.bytes() }.unwrap()).unwrap();
    let book = ReaderBook::load(
        &mut path,
        cache,
        resources,
        leaves,
        runtime.now_ms,
        true,
        false,
    )
    .ok()
    .unwrap();
    let book = ReaderBook::into_raw(book);
    assert!(unsafe { ReaderBook::open_facade(book) });
    let facade = unsafe { queries::facade(book.as_ptr()) };
    let section = unsafe { ReaderSection::open(book, 0, resources, leaves) }.unwrap();
    let spec = XpEpubRenderSpec {
        font: 16,
        compression: 1.0,
        width: 240,
        height: 320,
        paragraph_spacing: 0,
        paragraph_indent: 2,
        character_spacing: 0,
        word_spacing_percent: 100,
        alignment: 0,
        hyphenation: 0,
        embedded_style: 1,
        images: 1,
        focus_reading: 0,
    };
    let mut popups = 0u32;
    unsafe extern "C" fn popup(raw: *mut c_void) {
        unsafe { *raw.cast::<u32>() += 1 };
    }
    let callback = (with_popup != 0).then_some(Popup {
        call: popup,
        context: (&mut popups as *mut u32).cast(),
    });
    assert!(unsafe { ReaderSection::start(section, &spec, callback, runtime) });
    let total = unsafe { (*section.as_ptr()).progress.total };
    assert_eq!(popups, u32::from(with_popup != 0 && total >= 10 * 1024));
    assert!(!unsafe { ReaderSection::start(section, &spec, callback, runtime) });
    assert!(unsafe { ReaderSection::advance(section, 1, false) });
    let page = unsafe { ReaderSection::load_page(section, 0) };
    assert!(page.is_some());
    drop(page);
    if partial != 0 {
        assert!(unsafe { ReaderSection::building(section) });
        unsafe { ReaderSection::suspend(section) };
        assert!(unsafe { ReaderSection::partial(section) });
    } else {
        assert!(unsafe { ReaderSection::advance(section, 0, false) });
        assert!(unsafe { ReaderSection::complete(section) });
    }
    assert!(!unsafe { ReaderSection::building(section) });
    let pages = unsafe { ReaderSection::page_count(section) };
    assert!(pages > 0);
    assert!(unsafe { ReaderSection::load_cache(section, &spec) });
    assert_eq!(unsafe { ReaderSection::page_count(section) }, pages);
    assert_eq!(unsafe { ReaderSection::partial(section) }, partial != 0);
    assert_eq!(
        unsafe { ReaderSection::find_anchor(section, b"anchor", false) },
        Some(0)
    );
    let cached = unsafe { ReaderSection::query(section, Query::PageCount) }.map(|row| row[0]);
    assert_eq!(cached, (partial == 0).then_some(u32::from(pages)));
    let page = unsafe { ReaderSection::load_page(section, 0) };
    let page = page.expect("loaded Page");
    unsafe {
        native(facade, &page.borrow().wire(), pages, partial, popups);
        drop(page);
        ReaderSection::close(section);
        ReaderBook::close(book);
    }
    1
}
