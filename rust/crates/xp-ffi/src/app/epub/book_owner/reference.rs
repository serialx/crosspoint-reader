//! Explicit native comparison fixture, never part of ReaderBook/session ownership.
use super::{queries, ReaderBook};
use crate::app::epub::abi;
use core::{cell::UnsafeCell, ffi::c_void, ptr::NonNull};
static BORROW: abi::XpEpubBookBorrow = abi::XpEpubBookBorrow {
    spine: queries::spine,
    toc: queries::toc,
    cumulative: test_cumulative,
    spine_for_size: queries::spine_for_size,
    stream: queries::stream,
    item_size: queries::item_size,
};

unsafe extern "C" fn test_cumulative(raw: *mut c_void, index: i32) -> u32 {
    unsafe { queries::cumulative_size(raw, index) }.expect("test facade size query failed")
}

type Open = unsafe extern "C" fn(
    *mut c_void,
    *const abi::XpEpubBookView,
    *const abi::XpEpubBookBorrow,
) -> *mut c_void;
type Close = unsafe extern "C" fn(*mut c_void);
#[derive(Clone, Copy)]
struct Calls {
    open: Open,
    close: Close,
    extractor: Close,
}
struct FixtureCalls(UnsafeCell<Option<Calls>>);
// Host fixtures are serialized; registration happens before any reference is opened.
unsafe impl Sync for FixtureCalls {}
static CALLS: FixtureCalls = FixtureCalls(UnsafeCell::new(None));
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_native_reference_bind(
    open: Open,
    close: Close,
    extractor: Close,
) {
    *CALLS.0.get() = Some(Calls {
        open,
        close,
        extractor,
    });
}

pub(in crate::app::epub) struct NativeReference {
    handle: NonNull<c_void>,
    calls: Calls,
}
impl NativeReference {
    pub(in crate::app::epub) unsafe fn open(raw: NonNull<c_void>) -> Option<Self> {
        let book = raw.cast::<ReaderBook>().as_ptr();
        let view = unsafe {
            assert!(
                (*book).css.is_none(),
                "load releases CSS before Section borrows the book"
            );
            abi::XpEpubBookView {
                cache_path: (*book).cache_path.wire(),
                language: super::app_abi::Text::from_bytes(&(*book).metadata.language),
                spine_count: i32::from((*book).cache.spine_count),
                toc_count: i32::from((*book).cache.toc_count),
            }
        };
        let calls = unsafe { *CALLS.0.get() }.expect("native comparison callbacks installed");
        let handle = NonNull::new(unsafe { (calls.open)(raw.as_ptr(), &view, &BORROW) })?;
        Some(Self { handle, calls })
    }
    pub(in crate::app::epub) fn as_ptr(&self) -> *mut c_void {
        self.handle.as_ptr()
    }
}
impl Drop for NativeReference {
    fn drop(&mut self) {
        unsafe {
            (self.calls.extractor)(core::ptr::null_mut());
            (self.calls.close)(self.handle.as_ptr());
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_book_reference(
    raw: *mut c_void,
    view: *mut abi::XpEpubBookView,
    callbacks: *mut abi::XpEpubBookBorrow,
) {
    let book = &*raw.cast::<ReaderBook>();
    assert!(book.css.is_none());
    view.write(abi::XpEpubBookView {
        cache_path: book.cache_path.wire(),
        language: super::app_abi::Text::from_bytes(&book.metadata.language),
        spine_count: i32::from(book.cache.spine_count),
        toc_count: i32::from(book.cache.toc_count),
    });
    core::ptr::copy_nonoverlapping(&BORROW, callbacks, 1);
}
