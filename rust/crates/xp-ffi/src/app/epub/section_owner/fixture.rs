//! App-test bridge for the real Section owner; it contains no build policy.
extern crate alloc;
use super::super::{
    abi::{XpEpubLeaves, XpEpubRenderSpec},
    book_owner::ReaderBook,
    Text,
};
use super::{Popup, ReaderSection};
use crate::{app::abi, section_cache::Query};
use alloc::{alloc::alloc, boxed::Box};
use core::{alloc::Layout, ffi::c_void, ptr::NonNull};

struct Fixture {
    book: NonNull<c_void>,
    section: NonNull<ReaderSection>,
    runtime: &'static abi::Runtime,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_open(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    runtime: &'static abi::Runtime,
    source: abi::Text,
    cache: abi::Text,
    spine: i32,
) -> *mut c_void {
    let Some(source) = (unsafe { source.bytes() }) else {
        return core::ptr::null_mut();
    };
    let Some(cache) = (unsafe { cache.bytes() }) else {
        return core::ptr::null_mut();
    };
    let (Ok(mut path), Ok(cache)) = (Text::copy(source), Text::copy(cache)) else {
        return core::ptr::null_mut();
    };
    let Ok(book) = ReaderBook::load(
        &mut path,
        cache,
        resources,
        leaves,
        runtime.now_ms,
        true,
        false,
    ) else {
        return core::ptr::null_mut();
    };
    let book = ReaderBook::into_raw(book);
    let Some(section) = (unsafe { ReaderSection::open(book, spine, resources, leaves) }) else {
        unsafe { ReaderBook::close(book) };
        return core::ptr::null_mut();
    };
    let raw = unsafe { alloc(Layout::new::<Fixture>()) }.cast::<Fixture>();
    if raw.is_null() {
        unsafe {
            ReaderSection::close(section);
            ReaderBook::close(book);
        }
        return core::ptr::null_mut();
    }
    unsafe {
        raw.write(Fixture {
            book,
            section,
            runtime,
        });
    }
    raw.cast()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_close(raw: *mut c_void) {
    let context = unsafe { Box::from_raw(raw.cast::<Fixture>()) };
    unsafe {
        ReaderSection::close(context.section);
        ReaderBook::close(context.book)
    };
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_load(
    raw: *mut c_void,
    spec: &XpEpubRenderSpec,
) -> u8 {
    unsafe { ReaderSection::load_cache((*raw.cast::<Fixture>()).section, spec) }.into()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_start(
    raw: *mut c_void,
    spec: &XpEpubRenderSpec,
    call: Option<unsafe extern "C" fn(*mut c_void)>,
    context: *mut c_void,
) -> u8 {
    let f = unsafe { &*raw.cast::<Fixture>() };
    unsafe {
        ReaderSection::start(
            f.section,
            spec,
            call.map(|call| Popup { call, context }),
            f.runtime,
        )
    }
    .into()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_create(
    raw: *mut c_void,
    spec: &XpEpubRenderSpec,
    call: Option<unsafe extern "C" fn(*mut c_void)>,
    context: *mut c_void,
) -> u8 {
    let f = unsafe { &*raw.cast::<Fixture>() };
    unsafe {
        ReaderSection::create(
            f.section,
            spec,
            call.map(|call| Popup { call, context }),
            f.runtime,
        )
    }
    .into()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_advance(
    raw: *mut c_void,
    pages: i32,
    yielding: u8,
) -> u8 {
    unsafe { ReaderSection::advance((*raw.cast::<Fixture>()).section, pages, yielding != 0) }.into()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_suspend(raw: *mut c_void) {
    unsafe { ReaderSection::suspend((*raw.cast::<Fixture>()).section) };
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_abandon(raw: *mut c_void) {
    unsafe { ReaderSection::abandon((*raw.cast::<Fixture>()).section) };
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_clear(raw: *mut c_void) -> u8 {
    unsafe { ReaderSection::clear((*raw.cast::<Fixture>()).section) }.into()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_facts(raw: *mut c_void, out: *mut [u32; 4]) {
    let s = unsafe { (*raw.cast::<Fixture>()).section };
    unsafe {
        out.write([
            ReaderSection::page_count(s).into(),
            ReaderSection::building(s).into(),
            ReaderSection::complete(s).into(),
            ReaderSection::partial(s).into(),
        ])
    };
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_page(
    raw: *mut c_void,
    page: i32,
) -> *mut c_void {
    unsafe { ReaderSection::load_page((*raw.cast::<Fixture>()).section, page) }
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_query(
    raw: *mut c_void,
    kind: u8,
    arg: u32,
    first: u8,
    out: *mut u32,
) -> u8 {
    let s = unsafe { (*raw.cast::<Fixture>()).section };
    let value = unsafe {
        match kind {
            0 => ReaderSection::query(s, Query::PageCount).map(|r| r[0]),
            1 => ReaderSection::query(s, Query::PageForParagraph(arg)).map(|r| r[0]),
            2 => ReaderSection::query(s, Query::PageForList(arg)).map(|r| r[0]),
            3 => ReaderSection::paragraph_for_page(s, arg as u16).map(u32::from),
            4 => ReaderSection::offset_for_page(s, arg as u16),
            5 => ReaderSection::page_for_offset(s, arg, first != 0).map(u32::from),
            6 => Some(ReaderSection::reached_offset(s, arg).into()),
            _ => panic!("unknown fixture query"),
        }
    };
    if let Some(value) = value {
        unsafe { out.write(value) };
        1
    } else {
        0
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_anchor(
    raw: *mut c_void,
    text: abi::Text,
    kind: u8,
    out: *mut u16,
) -> u8 {
    let s = unsafe { (*raw.cast::<Fixture>()).section };
    let text = unsafe { text.bytes() }.unwrap();
    let value = unsafe {
        match kind {
            0 => ReaderSection::find_anchor(s, text, false),
            1 => ReaderSection::find_anchor(s, text, true),
            2 => ReaderSection::build_anchor(s, text),
            _ => panic!("unknown fixture anchor"),
        }
    };
    if let Some(value) = value {
        unsafe { out.write(value) };
        1
    } else {
        0
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_toc(raw: *mut c_void, page: i32) -> i32 {
    unsafe { ReaderSection::toc_for_page((*raw.cast::<Fixture>()).section, page) }
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_text(
    raw: *mut c_void,
    page: i32,
    sink: abi::TextSink,
    context: *mut c_void,
) -> u8 {
    let Some(text) = (unsafe { ReaderSection::page_text((*raw.cast::<Fixture>()).section, page) })
    else {
        return 0;
    };
    unsafe { sink(context, text.wire()) }
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_estimate(raw: *mut c_void) -> i32 {
    unsafe { ReaderSection::estimated_pages((*raw.cast::<Fixture>()).section) }
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_html(raw: *mut c_void) -> u8 {
    unsafe { ReaderSection::html_cached((*raw.cast::<Fixture>()).section) }.into()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_fixture_facade(raw: *mut c_void) -> *mut c_void {
    let book = unsafe { (*raw.cast::<Fixture>()).book };
    if !unsafe { ReaderBook::open_facade(book) } {
        return core::ptr::null_mut();
    }
    unsafe { super::super::book_owner::queries::facade(book.as_ptr()) }
}
