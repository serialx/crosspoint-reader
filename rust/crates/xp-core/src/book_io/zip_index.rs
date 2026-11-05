//! ZIP lookup/cache sequencing over the facade's retained map and names.
use crate::book::residual_abi::*;
use crate::{
    book::residual::Span,
    zip::{self, Entry, Step, Walker},
};
use core::ffi::c_void;
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub control: unsafe extern "C" fn(*mut c_void, u32, u32) -> usize,
    pub seek: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    pub read: unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32,
    pub cache: unsafe extern "C" fn(*mut c_void, u32, *mut Entry, Span) -> bool,
    pub name: *mut u8,
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, i32)>,
}
impl Services {
    pub(super) fn call(&self, op: u32, arg: u32) -> usize {
        unsafe { (self.control)(self.ctx, op, arg) }
    }
    fn log(&self, event: u32, expected: u32, actual: i32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event, expected, actual) }
        }
    }
    unsafe fn next(&self, walker: &mut Walker) -> Option<Entry> {
        let mut header = [0; 46];
        while let Some(request) = walker.request() {
            if !(self.seek)(self.ctx, request.offset as usize) {
                self.log(XP_BOOK_ZIP_LOG_CENTRAL_SEEK, 0, 0);
                return None;
            }
            let count = (self.read)(self.ctx, header.as_mut_ptr(), request.length as usize);
            match walker.advance(&header[..count.max(0) as usize]) {
                Some(Step::Entry(entry)) => return Some(entry),
                Some(Step::Read(_)) => continue,
                Some(Step::Done) => return None,
                None => {
                    self.log(XP_BOOK_ZIP_LOG_CENTRAL_INVALID, 0, 0);
                    return None;
                }
            }
        }
        None
    }
    unsafe fn name(&self, entry: &Entry, target: bool) -> Option<&[u8]> {
        let count = (self.read)(self.ctx, self.name, entry.name.length as usize);
        if count != entry.name.length as i32 {
            self.log(
                if target {
                    XP_BOOK_ZIP_LOG_TARGET_NAME
                } else {
                    XP_BOOK_ZIP_LOG_NAME
                },
                entry.name.length,
                count,
            );
            return None;
        }
        self.name.add(entry.name.length as usize).write(0);
        Some(core::slice::from_raw_parts(
            self.name,
            entry.name.length as usize,
        ))
    }
    /// Preserve the native signed-long offset check before the unchecked data seek.
    pub(super) unsafe fn data_offset(&self, entry: &Entry) -> Option<usize> {
        let request = zip::local_request(entry.local_offset);
        let mut header = [0; 30];
        if !(self.seek)(self.ctx, request.offset as usize)
            || (self.read)(self.ctx, header.as_mut_ptr(), request.length as usize)
                != request.length as i32
        {
            self.log(XP_BOOK_ZIP_LOG_LOCAL_READ, 0, 0);
            return None;
        }
        let Some(offset) = zip::data_offset(entry.local_offset, &header) else {
            self.log(XP_BOOK_ZIP_LOG_LOCAL_INVALID, 0, 0);
            return None;
        };
        let offset = offset as core::ffi::c_long;
        (offset >= 0).then_some(offset as usize)
    }
}
/// # Safety
/// Names are valid until the next callback; cache copies them before returning.
/// Name storage has 256 writable bytes. The context closes only a file opened here.
pub unsafe fn lookup(s: &Services, target: Span, last: Span) -> Option<Entry> {
    let mut entry = Entry::default();
    if s.call(XP_BOOK_ZIP_INDEX_CACHE_SIZE, 0) != 0 {
        return (s.cache)(s.ctx, XP_BOOK_ZIP_CACHE_FIND, &mut entry, target).then_some(entry);
    }
    if s.call(XP_BOOK_ZIP_INDEX_LAST_VALID, 0) != 0 && target.bytes() == last.bytes() {
        return (s.cache)(s.ctx, XP_BOOK_ZIP_CACHE_LAST, &mut entry, target).then_some(entry);
    }
    if s.call(XP_BOOK_ZIP_INDEX_OPEN, 0) == 0 || s.call(XP_BOOK_ZIP_INDEX_DETAILS, 0) == 0 {
        return None;
    }
    let start = if s.call(XP_BOOK_ZIP_INDEX_CURSOR_VALID, 0) != 0 {
        s.call(XP_BOOK_ZIP_INDEX_CURSOR_VALUE, 0)
    } else {
        s.call(XP_BOOK_ZIP_INDEX_CENTRAL, 0)
    };
    let mut walker = Walker::new(s.call(XP_BOOK_ZIP_INDEX_CENTRAL, 0) as u32, start as u32);
    while let Some(mut entry) = s.next(&mut walker) {
        if entry.name.length as usize != target.len || entry.name.length >= 256 {
            continue;
        }
        let Some(name) = s.name(&entry, true) else {
            break;
        };
        if name == target.bytes() {
            (s.cache)(s.ctx, XP_BOOK_ZIP_CACHE_REMEMBER, &mut entry, target);
            s.call(XP_BOOK_ZIP_INDEX_CURSOR, entry.next_offset);
            return Some(entry);
        }
    }
    None
}
/// # Safety
/// Same as lookup. Empty/partial cache success matches the original directory scan.
pub unsafe fn populate(s: &Services) -> bool {
    if s.call(XP_BOOK_ZIP_INDEX_OPEN, 0) == 0 || s.call(XP_BOOK_ZIP_INDEX_DETAILS, 0) == 0 {
        return false;
    }
    let central = s.call(XP_BOOK_ZIP_INDEX_CENTRAL, 0) as u32;
    let mut walker = Walker::new(central, central);
    s.call(XP_BOOK_ZIP_INDEX_CLEAR, 0);
    s.call(XP_BOOK_ZIP_INDEX_RESERVE, 0);
    while let Some(mut entry) = s.next(&mut walker) {
        if entry.name.length >= 256 {
            continue;
        }
        let Some(name) = s.name(&entry, false) else {
            break;
        };
        (s.cache)(s.ctx, XP_BOOK_ZIP_CACHE_INSERT, &mut entry, Span::new(name));
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
            core::ptr::null_mut(),
            Span::new(&[]),
        );
    }
    if s.call(XP_BOOK_ZIP_INDEX_OPEN, 0) == 0 || s.call(XP_BOOK_ZIP_INDEX_DETAILS, 0) == 0 {
        return false;
    }
    let central = s.call(XP_BOOK_ZIP_INDEX_CENTRAL, 0) as u32;
    let mut walker = Walker::new(central, central);
    while let Some(mut entry) = s.next(&mut walker) {
        if entry.name.length >= 256 {
            continue;
        }
        let Some(name) = s.name(&entry, false) else {
            break;
        };
        (s.cache)(s.ctx, XP_BOOK_ZIP_CACHE_VISIT, &mut entry, Span::new(name));
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
    let central = s.call(XP_BOOK_ZIP_INDEX_CENTRAL, 0) as u32;
    let mut walker = Walker::new(central, central);
    let mut matched = 0;
    while let Some(entry) = s.next(&mut walker) {
        if entry.name.length >= 256 {
            continue;
        }
        let Some(name) = s.name(&entry, false) else {
            break;
        };
        matched += batch_match(name, entry.uncompressed_size, count, ctx, get, set);
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
