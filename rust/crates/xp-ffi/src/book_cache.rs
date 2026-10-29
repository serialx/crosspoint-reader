//! Metadata cache reads and cumulative-size windows over borrowed owners.
use crate::book_limits::METADATA_WINDOW;
use core::ffi::c_void;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    ctx: *mut c_void,
    operation: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    millis: Option<unsafe extern "C" fn(*mut c_void) -> u32>,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32)>,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Op {
    ResetWriter,
    Open,
    ReadVersion,
    Close,
    ReadLut,
    ReadSpineCount,
    ReadTocCount,
    ReadMetadata,
    ResetWindow,
    ReserveWindow,
    SeekEntries,
    ReadCumulative,
    AddWindowStart,
    SetLoaded,
    SpineCount,
    TocCount,
    BookSize,
    Loaded,
    WindowFirst,
    SetWindowFirst,
    WindowStart,
    WindowValue,
    WindowCount,
    Lock,
    FillOpen,
    FillSeekLut,
    FillReadPosition,
    FillSeekPosition,
    FillReadHref,
    FillSkipHref,
    FillReadWindow,
    FillSkipToc,
    FillDrop,
    EntrySeek,
    EntryPosition,
    PrepareTemporary,
    TemporaryExists,
    TemporaryRemove,
    DropTemporary,
}
impl Services {
    fn call(&self, op: Op, arg: u32) -> u32 {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, arg) })
    }
    fn get(&self, op: Op) -> u32 {
        self.call(op, 0)
    }
    fn log(&self, event: u32, arg: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event, arg) };
        }
    }
}
fn fill(s: &Services, first: u32) -> bool {
    use Op::*;
    s.call(SetWindowFirst, u32::MAX);
    let mut ok = s.get(FillOpen) != 0
        && s.call(FillSeekLut, first) != 0
        && s.get(FillReadPosition) != 0
        && s.get(FillSeekPosition) != 0;
    let mut i = 0;
    while ok && i < METADATA_WINDOW && first + i < s.get(SpineCount) {
        ok = s.get(FillReadHref) != 0
            && s.get(FillSkipHref) != 0
            && s.call(FillReadWindow, i) != 0
            && s.get(FillSkipToc) != 0;
        i += 1;
    }
    if ok {
        s.call(SetWindowFirst, first);
    }
    s.get(FillDrop);
    ok
}
/// # Safety
/// Services exclusively own the metadata object until this call returns.
#[no_mangle]
pub unsafe extern "C" fn xp_book_metadata_load(services: *const Services) -> u8 {
    use Op::*;
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    s.get(ResetWriter);
    if s.get(Open) == 0 {
        return 0;
    }
    let version = s.get(ReadVersion);
    if version != 10 {
        s.log(0, version);
        s.get(Close);
        return 0;
    }
    s.get(ReadLut);
    s.get(ReadSpineCount);
    s.get(ReadTocCount);
    for i in 0..5 {
        s.call(ReadMetadata, i);
    }
    s.get(ResetWindow);
    s.call(ReserveWindow, s.get(SpineCount).div_ceil(METADATA_WINDOW));
    s.call(SeekEntries, (s.get(SpineCount) + s.get(TocCount)) * 4);
    let mut i = 0;
    while i < s.get(SpineCount) {
        if i % METADATA_WINDOW == 0 {
            s.get(AddWindowStart);
        }
        s.get(ReadCumulative);
        i += 1;
    }
    s.call(SetLoaded, 1);
    s.log(1, 0);
    1
}
/// # Safety
/// Services are borrowed; Lock establishes the existing window mutex until context destruction.
#[no_mangle]
pub unsafe extern "C" fn xp_book_metadata_cumulative(services: *const Services, index: i32) -> u32 {
    use Op::*;
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    if s.get(Loaded) == 0 || index < 0 || index as u32 >= s.get(SpineCount) {
        return 0;
    }
    let index = index as u32;
    if index == s.get(SpineCount) - 1 {
        return s.get(BookSize);
    }
    let first = index / METADATA_WINDOW * METADATA_WINDOW;
    if index - first == METADATA_WINDOW - 1 {
        return s.call(WindowStart, index / METADATA_WINDOW + 1);
    }
    s.get(Lock);
    if s.get(WindowFirst) != first && !fill(&s, first) {
        return s.call(WindowStart, index / METADATA_WINDOW);
    }
    s.call(WindowValue, index - first)
}
/// # Safety
/// Services and window storage remain valid; Lock establishes the existing window mutex.
#[no_mangle]
pub unsafe extern "C" fn xp_book_metadata_spine(services: *const Services, size: u32) -> i32 {
    use Op::*;
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return -1;
    };
    if s.get(Loaded) == 0 || s.get(SpineCount) == 0 || size > s.get(BookSize) {
        return -1;
    }
    let mut low = 1;
    let mut count = s.get(WindowCount) - 1;
    while count > 0 {
        let step = count / 2;
        let middle = low + step;
        if s.call(WindowStart, middle) < size {
            low = middle + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    let first = (low - 1) * METADATA_WINDOW;
    let last = s.get(SpineCount).min(first + METADATA_WINDOW) - 1;
    s.get(Lock);
    if s.get(WindowFirst) != first && !fill(&s, first) {
        return last as i32;
    }
    for i in first..last {
        if s.call(WindowValue, i - first) >= size {
            return i as i32;
        }
    }
    last as i32
}

#[repr(u32)]
#[derive(Clone, Copy)]
enum SizeOp {
    Zip,
    RawSize,
    OpenZip,
    Count,
    AllocateSizes,
    Rewind,
    Append,
    Targets,
    Sort,
    Fill,
    Clear,
}
impl Services {
    fn size_op(&self, op: SizeOp, arg: u32) -> u32 {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, arg) })
    }
}
/// # Safety
/// Services own all temporary ZipFile/deque objects through this synchronous call.
#[no_mangle]
pub unsafe extern "C" fn xp_book_sizes(services: *const Services) -> u8 {
    use SizeOp::*;
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    let get = |op| s.size_op(op, 0);
    if get(Zip) == 0 {
        get(RawSize);
        return 1;
    }
    if get(OpenZip) == 0 {
        s.log(0, 0);
        return 0;
    }
    if get(Count) >= 400 {
        s.log(1, 0);
        let mut matched = 0u32;
        s.size_op(AllocateSizes, get(Count));
        get(Rewind);
        let mut i = 0;
        while i < get(Count) {
            s.size_op(Append, i);
            if get(Targets) >= 2048 || i + 1 >= get(Count) {
                get(Sort);
                matched = matched.wrapping_add(get(Fill));
                get(Clear);
            }
            i += 1;
        }
        s.log(2, matched);
    }
    1
}
#[no_mangle]
pub extern "C" fn xp_book_size_less(a_hash: u64, a_length: u16, b_hash: u64, b_length: u16) -> u8 {
    (a_hash < b_hash || (a_hash == b_hash && a_length < b_length)) as u8
}

/// # Safety
/// Services exclusively borrow the cache's shared read cursor until return.
#[no_mangle]
pub unsafe extern "C" fn xp_book_metadata_entry(
    services: *const Services,
    index: i32,
    toc: u8,
) -> u8 {
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    let toc = toc != 0;
    if s.get(Op::Loaded) == 0 {
        s.log(if toc { 4 } else { 2 }, 0);
        return 0;
    }
    if index < 0 || index as u32 >= s.get(if toc { Op::TocCount } else { Op::SpineCount }) {
        s.log(if toc { 5 } else { 3 }, index as u32);
        return 0;
    }
    let base = if toc { s.get(Op::SpineCount) } else { 0 };
    s.call(Op::EntrySeek, (base + index as u32) * 4);
    s.get(Op::EntryPosition);
    1
}
#[repr(C)]
pub struct ItemSizeServices {
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, u32, usize) -> usize,
}
/// # Safety
/// Services borrow cache/ZIP storage and href for this call only.
#[no_mangle]
pub unsafe extern "C" fn xp_book_item_size(
    services: *const ItemSizeServices,
    index: u16,
    zip: u8,
) -> usize {
    let Some(s) = services.as_ref() else {
        return 0;
    };
    if zip == 0 {
        return (s.operation)(s.ctx, 0, 0);
    }
    let size = (s.operation)(s.ctx, 1, index as usize);
    if size == 0 {
        (s.operation)(s.ctx, 2, 0)
    } else {
        size
    }
}

/// # Safety
/// Services and temporary path storage remain exclusively borrowed until return.
#[no_mangle]
pub unsafe extern "C" fn xp_book_metadata_cleanup(services: *const Services) -> u8 {
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    for id in 0..2 {
        s.call(Op::PrepareTemporary, id);
        if s.get(Op::TemporaryExists) != 0 {
            s.get(Op::TemporaryRemove);
        }
        s.get(Op::DropTemporary);
    }
    1
}
