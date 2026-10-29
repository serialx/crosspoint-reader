use core::ffi::c_void;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Span {
    pub data: *const u8,
    pub len: usize,
}

#[repr(C)]
pub struct CssEntry {
    pub path_hash: u64,
    pub content_key: u64,
    pub css_index: usize,
}

pub type CssPath = unsafe extern "C" fn(*mut c_void, usize) -> Span;

pub fn raster_kind(path: &[u8]) -> u32 {
    for (extension, kind) in [
        (b".jpg".as_slice(), 1),
        (b".jpeg", 1),
        (b".png", 2),
        (b".gif", 3),
    ] {
        if path.len() >= extension.len()
            && path
                .get(path.len() - extension.len()..)
                .is_some_and(|s| s.eq_ignore_ascii_case(extension))
        {
            return kind;
        }
    }
    0
}

/// # Safety
/// The path callback returns borrowed bytes valid for this call and never mutates entries.
pub unsafe fn css_observe(
    entries: &mut [CssEntry],
    path: &[u8],
    crc: u32,
    compressed: u32,
    ctx: *mut c_void,
    get_path: CssPath,
) {
    if !path
        .get(path.len().saturating_sub(4)..)
        .is_some_and(|s| s.eq_ignore_ascii_case(b".css"))
    {
        return;
    }
    let hash = crate::fnv1a64(path);
    let first = entries.partition_point(|entry| entry.path_hash < hash);
    for entry in entries.get_mut(first..).into_iter().flatten() {
        if entry.path_hash != hash {
            break;
        }
        let candidate = unsafe { get_path(ctx, entry.css_index) };
        if candidate.len > isize::MAX as usize || candidate.data.is_null() {
            continue;
        }
        let candidate = unsafe { core::slice::from_raw_parts(candidate.data, candidate.len) };
        if candidate == path {
            entry.content_key = ((crc as u64) << 32) | compressed as u64;
            break;
        }
    }
}

pub fn css_duplicate(entries: &[CssEntry], index: usize) -> bool {
    let Some(current) = entries.get(index) else {
        return false;
    };
    current.content_key != 0
        && entries
            .get(..index)
            .is_some_and(|prior| prior.iter().any(|e| e.content_key == current.content_key))
}
