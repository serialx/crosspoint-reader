//! ZIP lookup/cache sequencing over the facade's retained map and names.
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
#[repr(u32)]
enum Op {
    CacheSize,
    Open,
    Details,
    Central,
    Start,
    Clear,
    Reserve,
    Cursor,
    LastValid,
    CursorValid,
    CursorValue,
}
impl Services {
    fn call(&self, op: Op, arg: u32) -> usize {
        unsafe { (self.control)(self.ctx, op as u32, arg) }
    }
}
/// # Safety
/// Names are valid until the next callback; cache copies them before returning.
/// The context owns the directory walker and closes only a file opened by this call.
pub unsafe fn lookup(s: &Services, target: Span, last: Span) -> bool {
    if s.call(Op::CacheSize, 0) != 0 {
        return (s.cache)(s.ctx, 0, core::ptr::null(), target);
    }
    if s.call(Op::LastValid, 0) != 0 && target.bytes() == last.bytes() {
        return (s.cache)(s.ctx, 1, core::ptr::null(), target);
    }
    if s.call(Op::Open, 0) == 0 || s.call(Op::Details, 0) == 0 {
        return false;
    }
    let start = if s.call(Op::CursorValid, 0) != 0 {
        s.call(Op::CursorValue, 0)
    } else {
        s.call(Op::Central, 0)
    };
    s.call(Op::Start, start as u32);
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
            (s.cache)(s.ctx, 2, &entry, target);
            s.call(Op::Cursor, entry.next_offset);
            return true;
        }
    }
    false
}
/// # Safety
/// Same as lookup. Empty/partial cache success matches the original directory scan.
pub unsafe fn populate(s: &Services) -> bool {
    if s.call(Op::Open, 0) == 0 || s.call(Op::Details, 0) == 0 {
        return false;
    }
    s.call(Op::Start, s.call(Op::Central, 0) as u32);
    s.call(Op::Clear, 0);
    s.call(Op::Reserve, 0);
    let mut entry = Entry::default();
    while (s.next)(s.ctx, &mut entry) {
        if entry.name.length >= 256 {
            continue;
        }
        let name = (s.name)(s.ctx, &entry, false);
        if name.data.is_null() {
            break;
        }
        (s.cache)(s.ctx, 3, &entry, name);
    }
    let central = s.call(Op::Central, 0) as u32;
    s.call(Op::Cursor, central);
    true
}
/// # Safety
/// Same as lookup. Visit borrows each path only for its call.
pub unsafe fn enumerate(s: &Services, paths: bool) -> bool {
    if paths && s.call(Op::CacheSize, 0) != 0 {
        return (s.cache)(s.ctx, 4, core::ptr::null(), Span::new(&[]));
    }
    if s.call(Op::Open, 0) == 0 || s.call(Op::Details, 0) == 0 {
        return false;
    }
    s.call(Op::Start, s.call(Op::Central, 0) as u32);
    let mut entry = Entry::default();
    while (s.next)(s.ctx, &mut entry) {
        if entry.name.length >= 256 {
            continue;
        }
        let name = (s.name)(s.ctx, &entry, false);
        if name.data.is_null() {
            break;
        }
        (s.cache)(s.ctx, 5, &entry, name);
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
    if count == 0 || s.call(Op::Open, 0) == 0 || s.call(Op::Details, 0) == 0 {
        return 0;
    }
    s.call(Op::Start, s.call(Op::Central, 0) as u32);
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
    pub diagnostic: unsafe extern "C" fn(*mut c_void, u32),
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
        Some(0)
    } else if size > u32::MAX as usize {
        Some(1)
    } else {
        None
    };
    if let Some(e) = error {
        (s.diagnostic)(s.ctx, e);
        return false;
    }
    let Some(request) = crate::zip::eocd_request(size as u32) else {
        (s.diagnostic)(s.ctx, 1);
        return false;
    };
    let ptr = (s.allocate)(s.ctx, request.length as usize);
    if ptr.is_null() {
        (s.diagnostic)(s.ctx, 2);
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
    (s.diagnostic)(s.ctx, 3);
    false
}
