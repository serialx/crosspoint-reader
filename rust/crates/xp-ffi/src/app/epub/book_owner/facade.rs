extern crate alloc;

use super::{ReaderBook, Text};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

impl ReaderBook {
    pub(in crate::app::epub) fn into_raw(book: Box<Self>) -> NonNull<c_void> {
        // Do not reborrow a Box after publishing its allocation to native code.
        NonNull::new(Box::into_raw(book).cast()).unwrap()
    }

    pub(in crate::app::epub) unsafe fn take_path(raw: NonNull<c_void>) -> Text {
        unsafe { core::mem::take(&mut (*raw.cast::<Self>().as_ptr()).path) }
    }

    pub(in crate::app::epub) unsafe fn close(raw: NonNull<c_void>) {
        let book = raw.cast::<Self>().as_ptr();
        #[cfg(feature = "app-test")]
        unsafe {
            super::observer::observe(raw.as_ptr(), 1)
        };
        unsafe { drop(Box::from_raw(book)) };
    }
}
