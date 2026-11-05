use core::ffi::c_void;

#[repr(C)]
pub struct DisplaySource {
    pub struct_size: u32,
    pub ctx: *mut c_void,
    pub scratch: *mut u8,
    pub capacity: u32,
    pub rows: u16,
    pub width: u16,
    pub paint: Option<unsafe extern "C" fn(*mut c_void, *mut u8, u16, u16) -> u32>,
}
