use super::abi;

impl abi::Text {
    /// The platform lends these bytes for the synchronous callback only.
    pub unsafe fn bytes<'a>(self) -> Option<&'a [u8]> {
        if self.len == 0 {
            Some(&[])
        } else if self.data.is_null() || self.len > isize::MAX as usize {
            None
        } else {
            Some(unsafe { core::slice::from_raw_parts(self.data, self.len) })
        }
    }

    pub fn from_bytes(bytes: &[u8]) -> Self {
        Self {
            data: bytes.as_ptr(),
            len: bytes.len(),
        }
    }

    /// Borrow the terminator too, while keeping it outside the published length.
    pub fn from_terminated(bytes: &[u8]) -> Self {
        assert_eq!(bytes.last(), Some(&0));
        Self {
            data: bytes.as_ptr(),
            len: bytes.len() - 1,
        }
    }
}
