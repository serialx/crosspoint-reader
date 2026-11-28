use super::super::app_abi;
use super::*;
unsafe extern "C" {
    fn cssTestFailAllocation(index: usize);
    fn cssTestAllocationAttempts() -> usize;
}
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_document_id(
    leaves: &XpEpubLeaves,
    path: app_abi::Text,
    output: *mut u8,
) -> bool {
    let path = Text::copy(unsafe { path.bytes().unwrap() }).unwrap();
    unsafe { cssTestFailAllocation(0) };
    let digest = calculate(leaves, &path);
    let attempts = unsafe { cssTestAllocationAttempts() };
    unsafe { cssTestFailAllocation(usize::MAX) };
    assert_eq!(attempts, 0, "document sampling allocated Rust heap storage");
    let Some(digest) = digest else {
        return false;
    };
    assert_eq!(
        digest.0.get(32),
        Some(&0),
        "native event variables require a trailing NUL"
    );
    unsafe { ptr::copy_nonoverlapping(digest.wire().data, output, digest.wire().len) };
    true
}

#[no_mangle]
pub unsafe extern "C" fn xp_app_test_document_session(
    leaves: &'static XpEpubLeaves,
    runtime: &'static app_abi::Runtime,
    resources: &'static app_abi::Resources,
    path: app_abi::Text,
    worthy: bool,
    load_book: bool,
) {
    let mut session = super::super::super::EpubSession::try_new(
        unsafe { path.bytes().unwrap() },
        false,
        15,
        leaves,
        runtime,
        resources,
    )
    .unwrap();
    if load_book {
        use super::super::ReaderBook;
        let mut source = Text::copy(unsafe { path.bytes().unwrap() }).unwrap();
        let cache = Text::copy(b"/session.cache").unwrap();
        unsafe extern "C" fn now() -> u32 {
            0
        }
        let book = ReaderBook::load(&mut source, cache, resources, leaves, now, false, true)
            .unwrap_or_else(|_| panic!("document-ID failure must not prevent book loading"));
        assert_eq!(book.cache.spine(0).unwrap().href, b"chapter.xhtml");
        {
            let access = session.entry_resources();
            session.set_book(&access, Some(ReaderBook::into_raw(book)));
        }
        session.state.get_mut().facts.book = true;
    }
    session.state.get_mut().base.session = xp_ui::reader::SessionAccounting {
        start_epoch: 100,
        duration: if worthy { 10 } else { 9 },
        start_progress: 123,
        end_progress: 456,
        ..Default::default()
    };
    session.flush_session();
    let state = session.state.get_mut().base.session;
    assert_eq!(state.start_epoch, 0);
    assert_eq!(state.duration, 0);
    assert_eq!(state.start_progress, 0);
    assert_eq!(state.end_progress, 0);
    if load_book {
        use super::super::ReaderBook;
        let access = session.entry_resources();
        let raw = session.take_book(&access).expect("book remains open");
        let book = unsafe { raw.cast::<ReaderBook>().as_ref() };
        assert_eq!(book.cache.spine(0).unwrap().href, b"chapter.xhtml");
        assert_eq!(book.cache.spine(0).unwrap().cumulative, 17);
        struct Contents {
            bytes: [u8; 17],
            length: usize,
        }
        unsafe extern "C" fn append(raw: *mut c_void, bytes: *const u8, length: usize) -> u8 {
            let out = unsafe { &mut *raw.cast::<Contents>() };
            assert!(length <= out.bytes.len() - out.length);
            out.bytes[out.length..out.length + length]
                .copy_from_slice(unsafe { core::slice::from_raw_parts(bytes, length) });
            out.length += length;
            1
        }
        let mut contents = Contents {
            bytes: [0; 17],
            length: 0,
        };
        assert_ne!(
            unsafe {
                super::super::queries::stream(
                    raw.as_ptr(),
                    app_abi::Text::from_bytes(b"chapter.xhtml"),
                    8,
                    0,
                    append,
                    (&mut contents as *mut Contents).cast(),
                )
            },
            0
        );
        assert_eq!(contents.length, 17);
        assert_eq!(&contents.bytes, b"<p>Still here</p>");
        unsafe { ReaderBook::close(raw) };
    }
}
