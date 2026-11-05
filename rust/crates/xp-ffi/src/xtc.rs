//! XTC ABI: all storage is borrowed for one call; no allocation or retained pointers.
use core::slice;
use xp_core::xtc::{self};

unsafe fn bytes<'a>(data: *const u8, len: usize) -> Option<&'a [u8]> {
    if len > isize::MAX as usize || (len != 0 && data.is_null()) {
        return None;
    }
    Some(if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    })
}

/// # Safety
/// Nonempty input is readable for len bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_bit_depth(data: *const u8, len: usize) -> u8 {
    bytes(data, len).map_or(0, xtc::bit_depth)
}
