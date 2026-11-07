use super::*;
use crate::app::epub::book_owner::ReaderBook;

type Observe = unsafe extern "C" fn(app_abi::Text, f32, i32, i32);

// Actual ReaderBook and session guard; no synthetic book or progress services.
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_book_progress(
    resources: &'static app_abi::Resources,
    leaves: &'static abi::XpEpubLeaves,
    runtime: &'static app_abi::Runtime,
    path: app_abi::Text,
    cache: app_abi::Text,
    mode: u8,
    observe: Observe,
) -> u8 {
    let (Some(path), Some(cache)) = (unsafe { path.bytes() }, unsafe { cache.bytes() }) else {
        return 0;
    };
    let (Ok(mut book_path), Ok(cache)) = (Text::copy(path), Text::copy(cache)) else {
        return 0;
    };
    let Some(session) = EpubSession::try_new(path, false, 15, leaves, runtime, resources) else {
        return 0;
    };
    let Ok(book) = ReaderBook::load(
        &mut book_path,
        cache,
        resources,
        leaves,
        runtime.now_ms,
        true,
        false,
    ) else {
        return 0;
    };
    let raw = NonNull::from(book.as_ref()).cast();
    let (saved, mapped) = {
        let access = session.lock_resources();
        let position = abi::XpEpubPosition {
            spine: 0,
            page: 1,
            count: 3,
            visible_offset: 6,
            paragraph: 1,
            list_item: 0,
            has_visible_offset: u8::from(mode == 0),
            has_paragraph: u8::from(mode == 1),
            has_list_item: 0,
            resolved_spine: 0,
            mapped_page: 0,
            anchor: [0; 64],
        };
        let Some(saved) = (unsafe { session.saved_progress(&access, raw, &position) }) else {
            return 0;
        };
        let input = abi::XpEpubSavedProgress {
            xpath: saved.xpath.wire(),
            percentage: saved.percentage,
        };
        let mapped = unsafe { session.mapped_progress(&access, raw, &input, 0, 3) };
        (saved, mapped)
    };
    // The owned XPath remains valid after the guard, book and session are gone.
    drop(book);
    drop(session);
    unsafe {
        observe(
            saved.xpath.wire(),
            saved.percentage,
            mapped.spine,
            mapped.page,
        )
    };
    1
}

#[test]
fn chapter_callbacks_end_borrows_before_owned_href_and_stream_bytes_are_released() {
    let mut href: Option<Text> = None;
    {
        let row = abi::XpEpubSpine {
            href: app_abi::Text::from_bytes(b"chapter.xhtml"),
            bytes: 10,
            toc_index: 0,
        };
        assert_eq!(
            unsafe { receive_href((&mut href as *mut Option<Text>).cast(), &row) },
            1
        );
    }
    assert_eq!(href.unwrap().bytes(), b"chapter.xhtml");
    let mut sum = 0;
    {
        let mut callback = |bytes: &[u8]| sum += bytes.iter().map(|&n| u32::from(n)).sum::<u32>();
        let mut callback: &mut dyn FnMut(&[u8]) = &mut callback;
        let context = (&mut callback as *mut &mut dyn FnMut(&[u8])).cast();
        let bytes = [1, 2, 3];
        assert_eq!(
            unsafe { consume_chunk(context, bytes.as_ptr(), bytes.len()) },
            1
        );
        assert_eq!(unsafe { consume_chunk(context, core::ptr::null(), 0) }, 1);
    }
    assert_eq!(sum, 6);
}
