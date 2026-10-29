//! System item names shared by directory listings and path guards.
pub(crate) fn hidden_name(name: &[u8]) -> bool {
    name == b"System Volume Information" || name == b"XTCache"
}

/// # Safety
/// `data` borrows `len` readable bytes; null is allowed only for zero length.
#[no_mangle]
pub unsafe extern "C" fn xp_system_item_name(data: *const u8, len: usize) -> bool {
    hidden_name(if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(data, len)
    })
}
