use core::ffi::c_void;

#[repr(C)]
pub struct Plan {
    pub(crate) storage: *mut c_void,
    pub(crate) capacity_bytes: usize,
    pub(crate) count: usize,
    pub(crate) valid: u8,
}
