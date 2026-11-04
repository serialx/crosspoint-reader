use super::*;

/// Exercise actual record storage with the physical allocator supplied by the host.
/// The caller supplies a process-lifetime resource table and exclusive writable output.
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_records(
    resources: &'static abi::Resources,
    anchors: u8,
    count: u32,
    text_length: u32,
    output: *mut [usize; 5],
) -> u8 {
    let output = unsafe { &mut *output };
    *output = [0; 5];
    let mut records = BuildRecords::new(resources);
    let mut pending = Text::default();
    for index in 0..count as usize {
        let result = if anchors != 0 {
            let Ok(text) = Text::read(text_length as usize, |bytes| bytes.fill(index as u8)) else {
                return 0;
            };
            pending = text;
            let previous = pending.wire().data;
            let result = records.push_anchor(&mut pending, index as u16);
            if result.is_ok() {
                assert!(pending.is_empty());
                assert_eq!(
                    records.anchors.get(index).unwrap().text.wire().data,
                    previous
                );
            } else {
                assert_eq!(pending.wire().data, previous);
                assert_eq!(pending.bytes().len(), text_length as usize);
            }
            result
        } else {
            records.push_page(PagePosition {
                offset: index as u32 + 44,
                visible: index as u32 * 2,
                paragraph: index as u16,
                list: (index + 1) as u16,
            })
        };
        if result.is_err() {
            return 0;
        }
        output[0] = index + 1;
        if anchors != 0 {
            let (chunks, capacity) = records.anchors.allocated_capacity();
            output[1] = chunks;
            output[2] = capacity;
        } else {
            let (chunks, capacity) = records.pages.allocated_capacity();
            output[1] = chunks;
            output[2] = capacity;
        }
    }
    for index in 0..count as usize {
        if anchors != 0 {
            let record = records.anchors.get(index).unwrap();
            assert_eq!(record.page, index as u16);
            assert!(record.text.bytes().iter().all(|byte| *byte == index as u8));
            assert_eq!(record.text.bytes().len(), text_length as usize);
        } else {
            let record = records.pages.get(index).unwrap();
            assert_eq!(record.offset, index as u32 + 44);
            assert_eq!(record.visible, index as u32 * 2);
            assert_eq!(record.paragraph, index as u16);
            assert_eq!(record.list, (index + 1) as u16);
        }
    }
    assert!(records.pages.get(records.pages.len()).is_none());
    assert!(records.anchors.get(records.anchors.len()).is_none());
    output[3] = core::mem::size_of::<PagePosition>();
    output[4] = core::mem::size_of::<Anchor>();
    // A reservation without a following push still owns and frees its chunk.
    if count == 0 {
        let _ = records.pages.reserve_one();
        let _ = records.pages.reserve_one();
        let _ = records.anchors.reserve_one();
        let _ = records.anchors.reserve_one();
    }
    drop(pending);
    1
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_storage(
    resources: &'static abi::Resources,
    leaves: &'static super::super::abi::XpEpubLeaves,
    source: abi::Text,
    cache: abi::Text,
    native: unsafe extern "C" fn(*mut core::ffi::c_void, u8, *mut [u32; 20], *mut [u32; 4]),
) -> u8 {
    use super::super::book_owner::{queries, ReaderBook};
    use super::{file::CacheFile, owner::ReaderSection};
    use crate::section_cache::{self, Query, Reader, Writer};
    use xp_core::section::{Kind, VERSION};
    unsafe fn roundtrip(raw: core::ptr::NonNull<ReaderSection>, count: u32) {
        let (path, resources, leaves) = unsafe { raw.as_ref().storage_for_test() };
        let mut io = CacheFile::new(path, resources, leaves);
        assert!(io.open_read());
        let header = io.record(Kind::Header).unwrap();
        let mut records = BuildRecords::new(resources);
        for index in 0..count {
            let mut range = [0; 3];
            section_cache::query(&mut io, Query::PageRange(index), &mut range).unwrap();
            assert!(io.seek(header.values[17] + 2 + index * 2));
            let paragraph = io.value(Kind::U16).unwrap() as u16;
            assert!(io.seek(header.values[18] + index * 2));
            let list = io.value(Kind::U16).unwrap() as u16;
            records
                .push_page(PagePosition {
                    offset: range[0],
                    visible: range[2],
                    paragraph,
                    list,
                })
                .unwrap();
        }
        assert!(io.seek(header.values[16]));
        let anchors = io.value(Kind::U16).unwrap();
        for _ in 0..anchors {
            let length = io.value(Kind::U32).unwrap();
            let mut text = Text::read(length as usize, |bytes| {
                assert!(io.read(bytes));
            })
            .unwrap();
            let page = io.value(Kind::U16).unwrap() as u16;
            records.push_anchor(&mut text, page).unwrap();
        }
        assert!(io.seek(0));
        let prefix = Text::read(header.values[15] as usize, |bytes| {
            assert!(io.read(bytes));
        })
        .unwrap();
        io.prepare_temporary().unwrap();
        assert!(io.open_write());
        assert!(io.write(prefix.bytes()));
        assert!(section_cache::commit(
            &mut io,
            &records,
            VERSION,
            count as u16,
            0,
            0
        ));
    }
    let (Some(source), Some(cache)) = (unsafe { source.bytes() }, unsafe { cache.bytes() }) else {
        return 0;
    };
    let (Ok(mut path), Ok(cache)) = (Text::copy(source), Text::copy(cache)) else {
        return 0;
    };
    unsafe extern "C" fn now() -> u32 {
        0
    }
    let Ok(book) = ReaderBook::load(&mut path, cache, resources, leaves, now, true, false) else {
        return 0;
    };
    let book = ReaderBook::into_raw(book);
    assert!(unsafe { ReaderBook::open_facade(book) });
    let facade = unsafe { queries::facade(book.as_ptr()) };
    let mut expected = [0; 20];
    let mut native_values = [0; 4];
    unsafe { native(facade, 0, &mut expected, &mut native_values) };
    let section = unsafe { ReaderSection::open(book, 0, resources, leaves) }.unwrap();
    assert!(unsafe { ReaderSection::load(section, &expected) });
    let count = unsafe { ReaderSection::query(section, Query::PageCount) }.unwrap()[0];
    assert_eq!(count, native_values[0]);
    assert_eq!(
        unsafe { ReaderSection::query(section, Query::ParagraphForPage(count - 1)) }.unwrap()[0],
        native_values[1]
    );
    assert_eq!(
        unsafe { ReaderSection::query(section, Query::VisibleForPage(count - 1)) }.unwrap()[0],
        native_values[2]
    );
    assert_eq!(
        unsafe { ReaderSection::anchor(section, b"anchor") }.unwrap(),
        native_values[3]
    );
    assert!(unsafe { ReaderSection::anchor(section, b"not-present") }.is_none());
    unsafe { roundtrip(section, count) };
    unsafe { native(facade, 1, &mut expected, &mut native_values) };
    assert!(unsafe { ReaderSection::clear(section) });
    assert!(unsafe { ReaderSection::query(section, Query::PageCount) }.is_none());
    unsafe {
        ReaderSection::close(section);
        ReaderBook::close(book);
    }
    1
}
