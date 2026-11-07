use crate::app::abi;
use core::{ffi::c_void, ptr};

extern "C" {
    #[cfg_attr(
        any(test, feature = "app-test"),
        link_name = "xp_test_definition_malloc"
    )]
    fn malloc(bytes: usize) -> *mut c_void;
    #[cfg_attr(any(test, feature = "app-test"), link_name = "xp_test_definition_free")]
    fn free(data: *mut c_void);
    #[cfg(feature = "app-test")]
    fn xp_test_definition_copied(source: *const u8, target: *mut u8, length: usize);
}

// This block crosses the native result boundary; its allocator callback moves with it.
#[repr(transparent)]
pub(super) struct OwnedText(abi::DefinitionOwnedText);
impl Default for OwnedText {
    fn default() -> Self {
        Self(abi::DefinitionOwnedText {
            data: ptr::null_mut(),
            len: 0,
            capacity: 0,
            release: None,
        })
    }
}
impl Drop for OwnedText {
    fn drop(&mut self) {
        if let Some(release) = self.0.release.take() {
            unsafe { release(self.0.data, self.0.capacity) };
        }
    }
}
impl OwnedText {
    /// The caller transfers the allocation and its matching release obligation.
    pub(super) unsafe fn from_wire(text: abi::DefinitionOwnedText) -> Self {
        Self(text)
    }
    pub(super) fn view(&self) -> abi::Text {
        abi::Text {
            data: if self.0.data.is_null() {
                c"".as_ptr().cast()
            } else {
                self.0.data
            },
            len: self.0.len as usize,
        }
    }
    pub(super) fn is_empty(&self) -> bool {
        self.0.len == 0
    }
    pub(super) fn clear(&mut self) {
        self.0.len = 0;
        if !self.0.data.is_null() {
            unsafe { self.0.data.write(0) };
        }
    }
    pub(super) fn assign(&mut self, bytes: &[u8]) -> bool {
        if bytes.is_empty() {
            self.clear();
            return true;
        }
        let Some(needed) = bytes
            .len()
            .checked_add(1)
            .and_then(|n| u32::try_from(n).ok())
        else {
            return false;
        };
        unsafe extern "C" fn release(data: *mut u8, _: u32) {
            unsafe { free(data.cast()) };
        }
        if self.0.capacity < needed {
            // Keep the old block on failure; the final Rust owner uses the native default heap.
            let data = unsafe { malloc(needed as usize) }.cast::<u8>();
            if data.is_null() {
                return false;
            }
            let mut replacement = unsafe {
                Self::from_wire(abi::DefinitionOwnedText {
                    data,
                    len: 0,
                    capacity: needed,
                    release: Some(release),
                })
            };
            replacement.copy(bytes);
            *self = replacement;
        } else {
            self.copy(bytes);
        }
        true
    }
    fn copy(&mut self, bytes: &[u8]) {
        unsafe {
            ptr::copy_nonoverlapping(bytes.as_ptr(), self.0.data, bytes.len());
            self.0.data.add(bytes.len()).write(0);
            #[cfg(feature = "app-test")]
            xp_test_definition_copied(bytes.as_ptr(), self.0.data, bytes.len());
        }
        self.0.len = bytes.len() as u32;
    }
}
