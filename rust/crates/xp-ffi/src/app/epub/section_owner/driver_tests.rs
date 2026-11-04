use super::super::{
    abi::XpEpubLeaves,
    book_owner::{queries, FileSlot, ReaderBook},
    Text,
};
use super::{
    book::{self, CssStatus},
    driver::{BuildOwner, Step},
    image,
    owner::ReaderSection,
    services::{Fonts, Init, Paths},
};
use crate::{app::abi, chapter::Options, section_cache};
use core::ffi::c_void;
use xp_core::{
    chapter::assembly,
    section::{PARTIAL_VERSION, VERSION},
};

#[repr(C)]
pub(super) struct Bindings {
    fonts: Fonts,
    image: image::Physical,
    now: unsafe extern "C" fn() -> u32,
    language: unsafe extern "C" fn(abi::Text) -> i32,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_driver(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    source: abi::Text,
    cache: abi::Text,
    bindings: *const Bindings,
    partial: u8,
    native: unsafe extern "C" fn(*mut c_void, u8, *mut [u32; 20], *mut [u32; 4]),
) -> u8 {
    let bindings = unsafe { &*bindings };
    let mut path = Text::copy(unsafe { source.bytes() }.unwrap()).unwrap();
    let cache = Text::copy(unsafe { cache.bytes() }.unwrap()).unwrap();
    let cache_copy = Text::copy(cache.bytes()).unwrap();
    let book = ReaderBook::load(
        &mut path,
        cache,
        resources,
        leaves,
        bindings.now,
        true,
        false,
    )
    .ok()
    .unwrap();
    let book = ReaderBook::into_raw(book);
    assert!(unsafe { ReaderBook::open_facade(book) });
    let facade = unsafe { queries::facade(book.as_ptr()) };
    let mut expected = [0; 20];
    let mut values = [0; 4];
    unsafe { native(facade, 0, &mut expected, &mut values) };
    let section = unsafe { ReaderSection::open(book, 0, resources, leaves) }.unwrap();
    assert!(unsafe { ReaderSection::load(section, &expected) });
    let (mut paths, href) = unsafe { Paths::prepare(book, 0) }.unwrap();
    assert_eq!(href.bytes(), b"a.xhtml");
    paths.select_html(true).unwrap();
    let toc = unsafe { book::toc(book, 0) }.unwrap();
    assert_eq!(toc.len(), 1);
    assert_eq!(toc[0].bytes(), b"anchor");
    let language =
        Text::receive(|sink, raw| unsafe { queries::language(book.as_ptr(), sink, raw) }).unwrap();
    let mut output = FileSlot::new();
    assert!(output.open(leaves, &paths.temporary_cache, true, c"SCT"));
    let mut owner = unsafe {
        BuildOwner::new(Init {
            book,
            resources,
            leaves,
            fonts: bindings.fonts,
            image: bindings.image,
            now: bindings.now,
            popup: None,
            paths,
            css: None,
            toc,
            language: (bindings.language)(language.wire()),
            pages: 0,
            output,
        })
    }
    .unwrap();
    let mut header = expected;
    header[14] = 0;
    owner
        .services()
        .writer(|io| section_cache::header(io, &header));
    assert!(owner.load_css(&cache_copy) == CssStatus::Complete);
    let options = Options {
        alignment: header[5] as u8,
        embedded: header[9] as u8,
        images: header[10] as u8,
        css_enabled: u8::from(header[9] != 0),
        assembly: assembly::Options {
            font: header[1] as i32,
            compression: f32::from_bits(header[2]),
            width: header[6] as u16,
            height: header[7] as u16,
            extra_spacing: header[3] != 0,
            alignment: header[5] as u8,
            indent_spaces: header[4] as u8,
            character_spacing: header[12] as i8,
            word_spacing: header[13] as u8,
            hyphenation: header[8] != 0,
            focus: header[11] != 0,
        },
    };
    assert!(owner.begin(&options));
    let total = owner.services().input.total();
    let mut ticks = 0;
    let consumed = loop {
        ticks += 1;
        assert!(ticks < 5000);
        match owner.step() {
            Step::Error => panic!("reader Chapter failed"),
            Step::Done => {
                assert_eq!(partial, 0);
                assert!(owner.finish());
                break 0;
            }
            Step::More => {
                if partial != 0 && owner.services().built.get() >= values[0] as u16 {
                    break owner.services().input.consumed();
                }
            }
        }
    };
    assert_eq!(u32::from(owner.services().built.get()), values[0]);
    assert!(owner.services().commit(
        if partial != 0 {
            PARTIAL_VERSION
        } else {
            VERSION
        },
        consumed,
        if partial != 0 { total } else { 0 }
    ));
    owner.abort();
    owner.clear_css();
    drop(owner);
    assert!(unsafe { ReaderSection::load(section, &expected) });
    unsafe { native(facade, 1, &mut expected, &mut values) };
    unsafe {
        ReaderSection::close(section);
        ReaderBook::close(book);
    }
    1
}
