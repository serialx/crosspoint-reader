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

#[repr(C)]
pub struct ParseServices {
    pub ctx: *mut c_void,
    pub call: unsafe extern "C" fn(*mut c_void, u32, usize, usize) -> usize,
    pub diagnostic: unsafe extern "C" fn(*mut c_void, u32, usize, usize),
}
#[repr(u32)]
enum ParseOp {
    Clear,
    Dedup,
    Heap,
    Size,
    OpenWrite,
    Stream,
    Close,
    Remove,
    OpenRead,
    Parse,
    DropTemp,
    Empty,
    Save,
}
impl ParseServices {
    fn call(&self, op: ParseOp, index: usize, arg: usize) -> usize {
        unsafe { (self.call)(self.ctx, op as u32, index, arg) }
    }
    fn log(&self, event: u32, index: usize, value: usize) {
        unsafe { (self.diagnostic)(self.ctx, event, index, value) }
    }
}
/// # Safety
/// Dedup returns null or an immutable CssEntry table of count elements, live through this call.
/// The borrowed services preserve file storage until the matching release.
pub unsafe fn parse_files(s: &ParseServices, count: usize, partial_cache: bool) -> u32 {
    use ParseOp as O;
    if count == 0 {
        s.log(0, 0, 0);
    }
    s.log(1, 0, count);
    s.call(O::Clear, 0, 0);
    let entries = if count > 1 {
        let ptr = s.call(O::Dedup, 0, count) as *const CssEntry;
        if ptr.is_null() {
            s.log(2, 0, 0);
            &[]
        } else {
            core::slice::from_raw_parts(ptr, count)
        }
    } else {
        &[]
    };
    let mut result = 0;
    let mut duplicates = 0;
    for index in 0..count {
        if css_duplicate(entries, index) {
            duplicates += 1;
            continue;
        }
        s.log(3, index, 0);
        let heap = s.call(O::Heap, index, 0);
        if heap < 65536 {
            s.log(4, index, heap);
            if result == 0 {
                result = 1;
            }
            continue;
        }
        let size = s.call(O::Size, index, 0);
        if size > 131072 {
            s.log(5, index, size);
            if result == 0 {
                result = 1;
            }
            continue;
        }
        if s.call(O::OpenWrite, index, 0) == 0 {
            s.log(6, index, 0);
            result = 2;
        } else if s.call(O::Stream, index, 1024) == 0 {
            s.log(7, index, 0);
            s.call(O::Close, index, 0);
            s.call(O::Remove, index, 0);
            result = 2;
        } else {
            s.call(O::Close, index, 0);
            if s.call(O::OpenRead, index, 0) == 0 {
                s.log(8, index, 0);
                s.call(O::Remove, index, 0);
                result = 2;
            } else {
                let parsed = s.call(O::Parse, index, 0);
                s.call(O::Close, index, 0);
                s.call(O::Remove, index, 0);
                if parsed == 2 || (parsed == 1 && result == 0) {
                    result = parsed;
                }
            }
        }
        s.call(O::DropTemp, index, 0);
    }
    if result == 2 {
        s.log(9, 0, 0);
    } else if result == 1 && s.call(O::Empty, 0, 0) != 0 {
        s.log(10, 0, 0);
        result = 2;
    } else if result == 1 && partial_cache {
        s.log(11, 0, 0);
    } else if s.call(O::Save, 0, usize::from(result == 0)) == 0 {
        s.log(12, 0, 0);
        result = 2;
    } else {
        s.log(13, duplicates, result);
    }
    s.call(O::Clear, 0, 0);
    result as u32
}
