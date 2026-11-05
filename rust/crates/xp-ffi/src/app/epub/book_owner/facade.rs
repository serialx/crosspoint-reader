extern crate alloc;

use super::{queries, ReaderBook, Text};
use crate::app::epub::abi;
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

static BORROW: abi::XpEpubBookBorrow = abi::XpEpubBookBorrow {
    spine: queries::spine,
    toc: queries::toc,
    cumulative: queries::cumulative_size,
    spine_for_size: queries::spine_for_size,
    stream: queries::stream,
    item_size: queries::item_size,
};

impl ReaderBook {
    pub(in crate::app::epub) fn into_raw(book: Box<Self>) -> NonNull<c_void> {
        // Do not reborrow a Box after publishing its allocation to native code.
        NonNull::new(Box::into_raw(book).cast()).unwrap()
    }

    pub(in crate::app::epub) unsafe fn open_facade(raw: NonNull<c_void>) -> bool {
        let book = raw.cast::<Self>().as_ptr();
        let (view, leaves) = unsafe {
            assert!((*book).facade.is_none());
            assert!(
                (*book).css.is_none(),
                "load releases CSS before Section borrows the book"
            );
            (
                abi::XpEpubBookView {
                    cache_path: (*book).cache_path.wire(),
                    language: super::app_abi::Text::from_bytes(&(*book).metadata.language),
                    spine_count: i32::from((*book).cache.spine_count),
                    toc_count: i32::from((*book).cache.toc_count),
                },
                (*book).cache.file.leaves,
            )
        };
        let facade =
            NonNull::new(unsafe { (leaves.book_facade_open)(raw.as_ptr(), &view, &BORROW) });
        unsafe { (*book).facade = facade };
        facade.is_some()
    }

    pub(in crate::app::epub) unsafe fn take_path(raw: NonNull<c_void>) -> Text {
        unsafe { core::mem::take(&mut (*raw.cast::<Self>().as_ptr()).path) }
    }

    pub(in crate::app::epub) unsafe fn close(raw: NonNull<c_void>) {
        let book = raw.cast::<Self>().as_ptr();
        let (facade, leaves) = unsafe { ((*book).facade.take(), (*book).cache.file.leaves) };
        if let Some(facade) = facade {
            // The handle is retired before native close; it cannot be rebound.
            unsafe { (leaves.book_facade_close)(facade.as_ptr()) };
        }
        unsafe { drop(Box::from_raw(book)) };
    }
}
