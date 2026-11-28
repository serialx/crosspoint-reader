//! Serialized host-fixture observation after real image binding and before book drop.
use super::ReaderBook;
use crate::app::epub::abi::XpEpubBookView;
use core::{cell::UnsafeCell, ffi::c_void};
type Observe = unsafe extern "C" fn(*mut c_void, u8, *const XpEpubBookView);
struct Observer(UnsafeCell<Option<Observe>>);
// App-test fixtures are serialized and callbacks do not reenter the owner.
unsafe impl Sync for Observer {}
static OBSERVER: Observer = Observer(UnsafeCell::new(None));
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_book_observer(callback: Option<Observe>) {
    *OBSERVER.0.get() = callback;
}
pub(super) unsafe fn observe(raw: *mut c_void, event: u8) {
    let Some(callback) = *OBSERVER.0.get() else {
        return;
    };
    if raw.is_null() || event != 0 {
        callback(raw, event, core::ptr::null());
    } else {
        let book = &*raw.cast::<ReaderBook>();
        let view = XpEpubBookView {
            cache_path: book.cache_path.wire(),
            language: super::app_abi::Text::from_bytes(&book.metadata.language),
            spine_count: i32::from(book.cache.spine_count),
            toc_count: i32::from(book.cache.toc_count),
        };
        callback(raw, event, &view);
    }
}
