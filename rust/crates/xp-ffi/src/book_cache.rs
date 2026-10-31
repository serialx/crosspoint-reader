//! Metadata cache reads and cumulative-size windows over borrowed owners.
use crate::book_limits::METADATA_WINDOW;
use core::ffi::c_void;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    ctx: *mut c_void,
    operation: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    read: unsafe extern "C" fn(*mut c_void, u32, *mut u8, usize) -> usize,
    field: unsafe extern "C" fn(*mut c_void, u32, usize) -> *mut u8,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32)>,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Op {
    ResetWriter = 0,
    Open = 1,
    Close = 3,
    ResetWindow = 8,
    ReserveWindow = 9,
    Seek = 10,
    AddWindowStart = 12,
    WindowStart = 20,
    WindowCount = 22,
    Lock = 23,
    FillFile = 24,
    FillSeek = 25,
    FillPosition = 29,
    PrepareTemporary = 35,
    TemporaryExists = 36,
    TemporaryRemove = 37,
    DropTemporary = 38,
    SpineRecord = 39,
}

#[repr(u32)]
#[derive(Clone, Copy)]
enum Field {
    Lut,
    SpineCount,
    TocCount,
    Title,
    Author,
    Language,
    Cover,
    Text,
    Href,
    Cumulative,
    TocIndex,
    TocTitle,
    TocHref,
    TocAnchor,
    Level,
    SpineIndex,
    BookSize,
    Loaded,
    WindowFirst,
    Window,
}
impl Field {
    fn width(self) -> usize {
        match self {
            Self::SpineCount | Self::TocCount | Self::TocIndex | Self::SpineIndex => 2,
            Self::Loaded | Self::Level => 1,
            _ => 4,
        }
    }
}
impl Services {
    fn window(&self, index: u32) -> u32 {
        unsafe {
            u32::from_le(core::ptr::read_unaligned(
                (self.field)(self.ctx, Field::Window as u32 + index, 0).cast(),
            ))
        }
    }
    fn scalar(&self, field: Field) -> u32 {
        let mut bytes = [0; 4];
        unsafe {
            core::ptr::copy_nonoverlapping(
                (self.field)(self.ctx, field as u32, 0),
                bytes.as_mut_ptr(),
                field.width(),
            );
        }
        u32::from_le_bytes(bytes)
    }
    fn set(&self, field: Field, value: u32) {
        unsafe {
            core::ptr::copy_nonoverlapping(
                value.to_le_bytes().as_ptr(),
                (self.field)(self.ctx, field as u32, 0),
                field.width(),
            );
        }
    }
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
impl Services {
    fn read(&self, file: u32, bytes: &mut [u8]) -> bool {
        unsafe { (self.read)(self.ctx, file, bytes.as_mut_ptr(), bytes.len()) == bytes.len() }
    }
    fn pod(&self, file: u32, field: u32, width: usize) -> bool {
        // Native POD destinations are initialized and little endian on supported targets.
        unsafe { (self.read)(self.ctx, file, (self.field)(self.ctx, field, width), width) == width }
    }
    fn string(&self, field: Field) {
        let mut bytes = [0; 4];
        self.read(0, &mut bytes);
        let len = u32::from_le_bytes(bytes) as usize;
        unsafe {
            (self.read)(self.ctx, 0, (self.field)(self.ctx, field as u32, len), len);
        }
    }
    fn spine(&self) {
        self.call(Op::SpineRecord, 1);
        self.string(Field::Href);
        self.pod(0, Field::Cumulative as u32, 4);
        self.pod(0, Field::TocIndex as u32, 2);
    }
}
fn fill(s: &Services, first: u32) -> bool {
    use Op::*;
    s.set(Field::WindowFirst, u32::MAX);
    let mut position = [0; 4];
    let mut ok = s.call(FillFile, 1) != 0
        && s.call(
            FillSeek,
            s.scalar(Field::Lut).wrapping_add(first.wrapping_mul(4)),
        ) != 0
        && s.read(1, &mut position)
        && s.call(FillSeek, u32::from_le_bytes(position)) != 0;
    let mut i = 0;
    while ok && i < METADATA_WINDOW && first + i < s.scalar(Field::SpineCount) {
        let mut length = [0; 4];
        ok = s.read(1, &mut length)
            && s.call(
                FillSeek,
                s.get(FillPosition).wrapping_add(u32::from_le_bytes(length)),
            ) != 0
            && s.pod(1, Field::Window as u32 + i, 4)
            && s.call(FillSeek, s.get(FillPosition).wrapping_add(2)) != 0;
        i += 1;
    }
    if ok {
        s.set(Field::WindowFirst, first);
    }
    s.call(FillFile, 0);
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
    let mut version = [0];
    s.read(0, &mut version);
    let version = u32::from(version[0]);
    if version != 10 {
        s.log(0, version);
        s.get(Close);
        return 0;
    }
    s.pod(0, Field::Lut as u32, 4);
    s.pod(0, Field::SpineCount as u32, 2);
    s.pod(0, Field::TocCount as u32, 2);
    for field in [
        Field::Title,
        Field::Author,
        Field::Language,
        Field::Cover,
        Field::Text,
    ] {
        s.string(field);
    }
    s.get(ResetWindow);
    s.set(Field::BookSize, 0);
    s.set(Field::WindowFirst, u32::MAX);
    s.call(
        ReserveWindow,
        s.scalar(Field::SpineCount).div_ceil(METADATA_WINDOW),
    );
    s.call(
        Seek,
        s.scalar(Field::Lut)
            .wrapping_add((s.scalar(Field::SpineCount) + s.scalar(Field::TocCount)) * 4),
    );
    let mut i = 0;
    while i < s.scalar(Field::SpineCount) {
        if i % METADATA_WINDOW == 0 {
            s.call(AddWindowStart, s.scalar(Field::BookSize));
        }
        s.spine();
        s.set(Field::BookSize, s.scalar(Field::Cumulative));
        s.call(SpineRecord, 0);
        i += 1;
    }
    s.set(Field::Loaded, 1);
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
    if s.scalar(Field::Loaded) == 0 || index < 0 || index as u32 >= s.scalar(Field::SpineCount) {
        return 0;
    }
    let index = index as u32;
    if index == s.scalar(Field::SpineCount) - 1 {
        return s.scalar(Field::BookSize);
    }
    let first = index / METADATA_WINDOW * METADATA_WINDOW;
    if index - first == METADATA_WINDOW - 1 {
        return s.call(WindowStart, index / METADATA_WINDOW + 1);
    }
    s.get(Lock);
    if s.scalar(Field::WindowFirst) != first && !fill(&s, first) {
        return s.call(WindowStart, index / METADATA_WINDOW);
    }
    s.window(index - first)
}
/// # Safety
/// Services and window storage remain valid; Lock establishes the existing window mutex.
#[no_mangle]
pub unsafe extern "C" fn xp_book_metadata_spine(services: *const Services, size: u32) -> i32 {
    use Op::*;
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return -1;
    };
    if s.scalar(Field::Loaded) == 0
        || s.scalar(Field::SpineCount) == 0
        || size > s.scalar(Field::BookSize)
    {
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
    let last = s.scalar(Field::SpineCount).min(first + METADATA_WINDOW) - 1;
    s.get(Lock);
    if s.scalar(Field::WindowFirst) != first && !fill(&s, first) {
        return last as i32;
    }
    for i in first..last {
        if s.window(i - first) >= size {
            return i as i32;
        }
    }
    last as i32
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
    if s.scalar(Field::Loaded) == 0 {
        s.log(if toc { 4 } else { 2 }, 0);
        return 0;
    }
    if index < 0
        || index as u32
            >= s.scalar(if toc {
                Field::TocCount
            } else {
                Field::SpineCount
            })
    {
        s.log(if toc { 5 } else { 3 }, index as u32);
        return 0;
    }
    let base = if toc { s.scalar(Field::SpineCount) } else { 0 };
    s.call(
        Op::Seek,
        s.scalar(Field::Lut).wrapping_add((base + index as u32) * 4),
    );
    let mut position = [0; 4];
    s.read(0, &mut position);
    s.call(Op::Seek, u32::from_le_bytes(position));
    if toc {
        for field in [Field::TocTitle, Field::TocHref, Field::TocAnchor] {
            s.string(field);
        }
        s.pod(0, Field::Level as u32, 1);
        s.pod(0, Field::SpineIndex as u32, 2);
    } else {
        s.spine();
    }
    1
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
