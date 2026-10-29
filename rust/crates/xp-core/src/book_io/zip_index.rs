//! ZIP lookup/cache sequencing over the facade's retained map and names.
use crate::book::residual_abi::*;
use crate::{book::residual::Span, zip::Entry};
use core::ffi::c_void;
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub control: unsafe extern "C" fn(*mut c_void, u32, u32) -> usize,
    pub next: unsafe extern "C" fn(*mut c_void, *mut Entry) -> bool,
    pub name: unsafe extern "C" fn(*mut c_void, *const Entry, bool) -> Span,
    pub cache: unsafe extern "C" fn(*mut c_void, u32, *const Entry, Span) -> bool,
}
impl Services {
    fn call(&self, op: u32, arg: u32) -> usize {
        unsafe { (self.control)(self.ctx, op, arg) }
    }
}
/// # Safety
/// Names are valid until the next callback; cache copies them before returning.
/// The context owns the directory walker and closes only a file opened by this call.
pub unsafe fn lookup(s: &Services, target: Span, last: Span) -> bool {
    if s.call(XP_BOOK_ZIP_INDEX_CACHE_SIZE, 0) != 0 {
        return (s.cache)(s.ctx, XP_BOOK_ZIP_CACHE_FIND, core::ptr::null(), target);
    }
    if s.call(XP_BOOK_ZIP_INDEX_LAST_VALID, 0) != 0 && target.bytes() == last.bytes() {
        return (s.cache)(s.ctx, XP_BOOK_ZIP_CACHE_LAST, core::ptr::null(), target);
    }
    if s.call(XP_BOOK_ZIP_INDEX_OPEN, 0) == 0 || s.call(XP_BOOK_ZIP_INDEX_DETAILS, 0) == 0 {
        return false;
    }
    let start = if s.call(XP_BOOK_ZIP_INDEX_CURSOR_VALID, 0) != 0 {
        s.call(XP_BOOK_ZIP_INDEX_CURSOR_VALUE, 0)
    } else {
        s.call(XP_BOOK_ZIP_INDEX_CENTRAL, 0)
    };
    s.call(XP_BOOK_ZIP_INDEX_START, start as u32);
    let mut entry = Entry::default();
    while (s.next)(s.ctx, &mut entry) {
        if entry.name.length as usize != target.len || entry.name.length >= 256 {
            continue;
        }
        let name = (s.name)(s.ctx, &entry, true);
        if name.data.is_null() {
            break;
        }
        if name.bytes() == target.bytes() {
            (s.cache)(s.ctx, XP_BOOK_ZIP_CACHE_REMEMBER, &entry, target);
            s.call(XP_BOOK_ZIP_INDEX_CURSOR, entry.next_offset);
            return true;
        }
    }
    false
}
/// # Safety
/// Same as lookup. Empty/partial cache success matches the original directory scan.
pub unsafe fn populate(s: &Services) -> bool {
    if s.call(XP_BOOK_ZIP_INDEX_OPEN, 0) == 0 || s.call(XP_BOOK_ZIP_INDEX_DETAILS, 0) == 0 {
        return false;
    }
    s.call(
        XP_BOOK_ZIP_INDEX_START,
        s.call(XP_BOOK_ZIP_INDEX_CENTRAL, 0) as u32,
    );
    s.call(XP_BOOK_ZIP_INDEX_CLEAR, 0);
    s.call(XP_BOOK_ZIP_INDEX_RESERVE, 0);
    let mut entry = Entry::default();
    while (s.next)(s.ctx, &mut entry) {
        if entry.name.length >= 256 {
            continue;
        }
        let name = (s.name)(s.ctx, &entry, false);
        if name.data.is_null() {
            break;
        }
        (s.cache)(s.ctx, XP_BOOK_ZIP_CACHE_INSERT, &entry, name);
    }
    let central = s.call(XP_BOOK_ZIP_INDEX_CENTRAL, 0) as u32;
    s.call(XP_BOOK_ZIP_INDEX_CURSOR, central);
    true
}
/// # Safety
/// Same as lookup. Visit borrows each path only for its call.
pub unsafe fn enumerate(s: &Services, paths: bool) -> bool {
    if paths && s.call(XP_BOOK_ZIP_INDEX_CACHE_SIZE, 0) != 0 {
        return (s.cache)(
            s.ctx,
            XP_BOOK_ZIP_CACHE_PATHS,
            core::ptr::null(),
            Span::new(&[]),
        );
    }
    if s.call(XP_BOOK_ZIP_INDEX_OPEN, 0) == 0 || s.call(XP_BOOK_ZIP_INDEX_DETAILS, 0) == 0 {
        return false;
    }
    s.call(
        XP_BOOK_ZIP_INDEX_START,
        s.call(XP_BOOK_ZIP_INDEX_CENTRAL, 0) as u32,
    );
    let mut entry = Entry::default();
    while (s.next)(s.ctx, &mut entry) {
        if entry.name.length >= 256 {
            continue;
        }
        let name = (s.name)(s.ctx, &entry, false);
        if name.data.is_null() {
            break;
        }
        (s.cache)(s.ctx, XP_BOOK_ZIP_CACHE_VISIT, &entry, name);
    }
    true
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Target {
    pub hash: u64,
    pub len: u16,
    pub index: u16,
}
/// # Safety
/// Callbacks borrow the deque for this call and return its in-range elements.
pub unsafe fn batch_match(
    name: &[u8],
    size: u32,
    count: usize,
    ctx: *mut c_void,
    get: unsafe extern "C" fn(*mut c_void, usize) -> Target,
    set: unsafe extern "C" fn(*mut c_void, u16, u32) -> bool,
) -> usize {
    let hash = crate::fnv1a64(name);
    let mut lo = 0;
    let mut hi = count;
    while lo < hi {
        let mid = lo + (hi - lo) / 2;
        let value = get(ctx, mid);
        if value.hash < hash || (value.hash == hash && (value.len as usize) < name.len()) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    let mut matched = 0;
    for i in lo..count {
        let value = get(ctx, i);
        if value.hash != hash || value.len as usize != name.len() {
            break;
        }
        if set(ctx, value.index, size) {
            matched += 1;
        }
    }
    matched
}

/// # Safety
/// Same directory service and borrowed deque contracts as lookup and batch_match.
pub unsafe fn batch(
    s: &Services,
    count: usize,
    ctx: *mut c_void,
    get: unsafe extern "C" fn(*mut c_void, usize) -> Target,
    set: unsafe extern "C" fn(*mut c_void, u16, u32) -> bool,
) -> usize {
    if count == 0
        || s.call(XP_BOOK_ZIP_INDEX_OPEN, 0) == 0
        || s.call(XP_BOOK_ZIP_INDEX_DETAILS, 0) == 0
    {
        return 0;
    }
    s.call(
        XP_BOOK_ZIP_INDEX_START,
        s.call(XP_BOOK_ZIP_INDEX_CENTRAL, 0) as u32,
    );
    let mut entry = Entry::default();
    let mut matched = 0;
    while (s.next)(s.ctx, &mut entry) {
        if entry.name.length >= 256 {
            continue;
        }
        let name = (s.name)(s.ctx, &entry, false);
        if name.data.is_null() {
            break;
        }
        matched += batch_match(name.bytes(), entry.uncompressed_size, count, ctx, get, set);
        if matched >= count {
            break;
        }
    }
    matched
}
#[repr(C)]
pub struct DetailServices {
    pub ctx: *mut c_void,
    pub open: unsafe extern "C" fn(*mut c_void) -> bool,
    pub size: unsafe extern "C" fn(*mut c_void) -> usize,
    pub allocate: unsafe extern "C" fn(*mut c_void, usize) -> *mut u8,
    pub read: unsafe extern "C" fn(*mut c_void, u32, *mut u8, usize) -> bool,
    pub save: unsafe extern "C" fn(*mut c_void, *const crate::zip::Details),
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32)>,
}
impl DetailServices {
    fn log(&self, event: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event) }
        }
    }
}
/// # Safety
/// Allocation is writable for the requested size; the context releases it on return.
pub unsafe fn details(s: &DetailServices, cached: bool) -> bool {
    if cached {
        return true;
    }
    if !(s.open)(s.ctx) {
        return false;
    }
    let size = (s.size)(s.ctx);
    let error = if size < 22 {
        Some(XP_BOOK_ZIP_DETAIL_LOG_SMALL)
    } else if size > u32::MAX as usize {
        Some(XP_BOOK_ZIP_DETAIL_LOG_SIZE)
    } else {
        None
    };
    if let Some(e) = error {
        s.log(e);
        return false;
    }
    let Some(request) = crate::zip::eocd_request(size as u32) else {
        s.log(XP_BOOK_ZIP_DETAIL_LOG_SIZE);
        return false;
    };
    let ptr = (s.allocate)(s.ctx, request.length as usize);
    if ptr.is_null() {
        s.log(XP_BOOK_ZIP_DETAIL_LOG_ALLOCATION);
        return false;
    }
    if (s.read)(s.ctx, request.offset, ptr, request.length as usize) {
        if let Some(value) =
            crate::zip::eocd(core::slice::from_raw_parts(ptr, request.length as usize))
        {
            (s.save)(s.ctx, &value);
            return true;
        }
    }
    s.log(XP_BOOK_ZIP_DETAIL_LOG_SIGNATURE);
    false
}
