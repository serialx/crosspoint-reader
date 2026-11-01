use super::abi;
use core::{ffi::c_void, ptr::NonNull};

pub(super) struct Page {
    pub(super) handle: NonNull<c_void>,
    leaves: &'static abi::XpEpubLeaves,
}
impl Page {
    // The handle is uniquely owned until Drop or an explicit child transfer.
    pub(super) unsafe fn from_raw(
        handle: *mut c_void,
        leaves: &'static abi::XpEpubLeaves,
    ) -> Option<Self> {
        NonNull::new(handle).map(|handle| Self { handle, leaves })
    }
}
impl Drop for Page {
    fn drop(&mut self) {
        unsafe { (self.leaves.page_close)(self.handle.as_ptr()) };
    }
}
