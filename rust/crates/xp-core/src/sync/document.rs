//! KOReader's twelve partial-MD5 samples; hashing and I/O are platform leaves.
use core::ffi::c_void;
pub fn filename(path: &[u8]) -> &[u8] {
    let at = path.iter().rposition(|&b| b == b'/').map_or(0, |p| p + 1);
    &path[at..]
}
/// The callback returns the bytes added to the digest, or zero after a seek/read failure.
/// # Safety
/// `read` must accept `ctx` and preserve its lifetime for the call.
pub unsafe fn samples(
    ctx: *mut c_void,
    size: usize,
    read: unsafe extern "C" fn(*mut c_void, usize, usize) -> usize,
) -> usize {
    let mut total = 0usize;
    for sample in 0..12 {
        let offset = if sample == 0 {
            0
        } else {
            1024usize << (2 * (sample - 1))
        };
        if offset < size {
            total = total.wrapping_add(read(ctx, offset, 1024.min(size - offset)));
        }
    }
    total
}
