use crate::book::residual_abi::*;
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
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, usize, usize)>,
}
impl ParseServices {
    fn call(&self, op: u32, index: usize, arg: usize) -> usize {
        unsafe { (self.call)(self.ctx, op, index, arg) }
    }
    #[inline(never)]
    fn log(&self, event: u32, index: usize, value: usize) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event, index, value) }
        }
    }
}
/// # Safety
/// Dedup returns null or an immutable CssEntry table of count elements, live through this call.
/// The borrowed services preserve file storage until the matching release.
pub unsafe fn parse_files(s: &ParseServices, count: usize, partial_cache: bool) -> u32 {
    if count == 0 {
        s.log(XP_BOOK_CSS_LOG_NO_FILES, 0, 0);
    }
    s.log(XP_BOOK_CSS_LOG_COUNT, 0, count);
    s.call(XP_BOOK_CSS_FILE_CLEAR, 0, 0);
    let entries = if count > 1 {
        let ptr = s.call(XP_BOOK_CSS_FILE_DEDUP, 0, count) as *const CssEntry;
        if ptr.is_null() {
            s.log(XP_BOOK_CSS_LOG_DEDUP_OOM, 0, 0);
            &[]
        } else {
            core::slice::from_raw_parts(ptr, count)
        }
    } else {
        &[]
    };
    let mut result = XP_BOOK_CSS_PARSE_COMPLETE as usize;
    let mut duplicates = 0;
    for index in 0..count {
        if css_duplicate(entries, index) {
            duplicates += 1;
            continue;
        }
        s.log(XP_BOOK_CSS_LOG_PARSING, index, 0);
        let heap = s.call(XP_BOOK_CSS_FILE_HEAP, index, 0);
        if heap < 65536 {
            s.log(XP_BOOK_CSS_LOG_LOW_HEAP, index, heap);
            if result == XP_BOOK_CSS_PARSE_COMPLETE as usize {
                result = XP_BOOK_CSS_PARSE_PARTIAL as usize;
            }
            continue;
        }
        let size = s.call(XP_BOOK_CSS_FILE_SIZE, index, 0);
        if size > 131072 {
            s.log(XP_BOOK_CSS_LOG_OVERSIZE, index, size);
            if result == XP_BOOK_CSS_PARSE_COMPLETE as usize {
                result = XP_BOOK_CSS_PARSE_PARTIAL as usize;
            }
            continue;
        }
        if s.call(XP_BOOK_CSS_FILE_OPEN_WRITE, index, 0) == 0 {
            s.log(XP_BOOK_CSS_LOG_CREATE, index, 0);
            result = XP_BOOK_CSS_PARSE_ERROR as usize;
        } else if s.call(XP_BOOK_CSS_FILE_STREAM, index, 1024) == 0 {
            s.log(XP_BOOK_CSS_LOG_READ, index, 0);
            s.call(XP_BOOK_CSS_FILE_CLOSE, index, 0);
            s.call(XP_BOOK_CSS_FILE_REMOVE, index, 0);
            result = XP_BOOK_CSS_PARSE_ERROR as usize;
        } else {
            s.call(XP_BOOK_CSS_FILE_CLOSE, index, 0);
            if s.call(XP_BOOK_CSS_FILE_OPEN_READ, index, 0) == 0 {
                s.log(XP_BOOK_CSS_LOG_OPEN, index, 0);
                s.call(XP_BOOK_CSS_FILE_REMOVE, index, 0);
                result = XP_BOOK_CSS_PARSE_ERROR as usize;
            } else {
                let parsed = s.call(XP_BOOK_CSS_FILE_PARSE, index, 0);
                s.call(XP_BOOK_CSS_FILE_CLOSE, index, 0);
                s.call(XP_BOOK_CSS_FILE_REMOVE, index, 0);
                if parsed == XP_BOOK_CSS_PARSE_ERROR as usize
                    || (parsed == XP_BOOK_CSS_PARSE_PARTIAL as usize
                        && result == XP_BOOK_CSS_PARSE_COMPLETE as usize)
                {
                    result = parsed;
                }
            }
        }
        s.call(XP_BOOK_CSS_FILE_DROP_TEMP, index, 0);
    }
    if result == XP_BOOK_CSS_PARSE_ERROR as usize {
        s.log(XP_BOOK_CSS_LOG_FAILED, 0, 0);
    } else if result == XP_BOOK_CSS_PARSE_PARTIAL as usize
        && s.call(XP_BOOK_CSS_FILE_EMPTY, 0, 0) != 0
    {
        s.log(XP_BOOK_CSS_LOG_EMPTY_PARTIAL, 0, 0);
        result = XP_BOOK_CSS_PARSE_ERROR as usize;
    } else if result == XP_BOOK_CSS_PARSE_PARTIAL as usize && partial_cache {
        s.log(XP_BOOK_CSS_LOG_KEEP_PARTIAL, 0, 0);
    } else if s.call(
        XP_BOOK_CSS_FILE_SAVE,
        0,
        usize::from(result == XP_BOOK_CSS_PARSE_COMPLETE as usize),
    ) == 0
    {
        s.log(XP_BOOK_CSS_LOG_SAVE, 0, 0);
        result = XP_BOOK_CSS_PARSE_ERROR as usize;
    } else {
        s.log(XP_BOOK_CSS_LOG_LOADED, duplicates, result);
    }
    s.call(XP_BOOK_CSS_FILE_CLEAR, 0, 0);
    result as u32
}
