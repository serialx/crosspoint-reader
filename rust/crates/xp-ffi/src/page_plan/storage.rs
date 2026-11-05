use super::Command;

#[repr(C)]
pub struct Plan {
    pub(crate) storage: *mut Command,
    pub(crate) capacity_bytes: usize,
    pub(crate) count: usize,
    pub(crate) valid: u8,
}
