//! CSS file transactions and streaming policy; codec and owner stay borrowed.
use crate::css::{self, CacheDiagnostic, Css};
use core::{ffi::c_void, ptr};
use xp_core::css::Diagnostics;
use xp_core::css::{
    CACHE_BUFFER_BYTES, CACHE_VERSION, MAX_CACHE_KEY_BYTES, MAX_RULES, READ_BUFFER_BYTES,
    SELECTOR_CAP, WIRE_BYTES,
};
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub(crate) ctx: *mut c_void,
    pub(crate) operation: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    pub(crate) millis: Option<unsafe extern "C" fn(*mut c_void) -> u32>,
    pub(crate) diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32)>,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub(crate) enum Op {
    EmptyPath = 0,
    PreparePaths = 1,
    ExistsFinal = 2,
    ExistsBackup = 3,
    RenameBackupFinal = 4,
    RemoveFinal = 5,
    RemoveTemporary = 6,
    RemoveBackup = 7,
    RenameFinalBackup = 8,
    RenameTemporaryFinal = 9,
    OpenFinal = 10,
    OpenTemporary = 11,
    Close = 12,
    ReadWord = 13,
    Available = 15,
    Skip = 16,
    AllocateBuffer = 19,
    ReadBuffer = 20,
    AllocateOrder = 27,
    WriteBuffer = 31,
    WriteWord = 32,
    DropOrder = 33,
    SourceValid = 34,
    Restore = 37,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub(crate) enum Action {
    Restore = 0,
    Delete = 1,
    Inspect = 2,
    Save = 3,
    Load = 4,
    Stream = 5,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub(crate) enum Event {
    RestoreFailed,
    Restored,
    OutputOom,
    OrderOom,
    OrderFailed,
    WriteFailed,
    BackupFailed,
    PromoteFailed,
    RollbackFailed,
    SavedComplete,
    SavedPartial,
    Version,
    InputOom,
    LoadedComplete,
    LoadedPartial,
    InvalidSource,
    SourceOom,
    Parsed,
    HandleOom,
}
#[repr(C)]
pub struct Io {
    pub(crate) owner: *mut *mut Css,
    pub(crate) buffer: *mut u8,
    pub(crate) order: *mut u16,
    pub(crate) word: [u8; 4],
    pub(crate) capacity: usize,
}
const _: () = {
    let pointer = core::mem::size_of::<*mut Css>();
    assert!(core::mem::size_of::<Io>() == pointer * 5);
    assert!(core::mem::align_of::<Io>() == pointer);
    assert!(core::mem::offset_of!(Io, buffer) == pointer);
    assert!(core::mem::offset_of!(Io, order) == pointer * 2);
    assert!(core::mem::offset_of!(Io, word) == pointer * 3);
    assert!(core::mem::offset_of!(Io, capacity) == pointer * 4);
};
struct Driver {
    services: Services,
    io: *mut Io,
    diagnostics: Diagnostics,
}
impl Driver {
    fn call(&self, op: Op, arg: u32) -> u32 {
        self.services
            .operation
            .map_or(0, |call| unsafe { call(self.services.ctx, op as u32, arg) })
    }
    fn get(&self, op: Op) -> u32 {
        self.call(op, 0)
    }
    fn log(&self, event: Event, arg: u32) {
        if let Some(log) = self.services.diagnostic {
            unsafe { log(self.services.ctx, event as u32, arg) };
        }
    }
    fn value(&self) -> u32 {
        unsafe { u32::from_le_bytes((*self.io).word) }
    }
    fn word(&self, width: u32) -> Option<u32> {
        unsafe {
            (*self.io).word = [0; 4];
        }
        (self.call(Op::ReadWord, width) == width).then(|| self.value())
    }
    fn handle(&self) -> *mut Css {
        unsafe { *(*self.io).owner }
    }
    fn count(&self) -> usize {
        let handle = self.handle();
        if handle.is_null() {
            0
        } else {
            unsafe { (*handle).store.rule_count() }
        }
    }
    fn clear(&self) {
        unsafe {
            css::xp_css_destroy(self.handle());
            *(*self.io).owner = ptr::null_mut();
        }
    }
    fn ensure(&self) -> bool {
        if self.handle().is_null() {
            unsafe {
                *(*self.io).owner = css::xp_css_create();
            }
        }
        if self.handle().is_null() {
            self.log(Event::HandleOom, 0);
            false
        } else {
            true
        }
    }
    fn read_cache(&self, data: *const u8, len: usize, end: bool) -> u32 {
        let mut detail = CacheDiagnostic::default();
        let status = unsafe { css::cache_read(self.handle(), data, len, end as u8, &mut detail) };
        if detail.event != 0 {
            if let Some(log) = self.diagnostics.diagnostic {
                log(self.diagnostics.ctx, detail.event, detail.a, detail.b, 0, 0);
            }
        }
        status as u32
    }
    fn feed(&self, count: usize, end: bool) -> u32 {
        unsafe {
            css::feed(
                self.handle(),
                if end { ptr::null() } else { (*self.io).buffer },
                count,
                end as u8,
                &self.diagnostics,
            ) as u32
        }
    }
}

fn restore(s: &Driver) -> bool {
    use Op::*;
    if s.get(EmptyPath) != 0 {
        return false;
    }
    s.call(PreparePaths, 1);
    if s.get(ExistsFinal) != 0 {
        return true;
    }
    s.call(PreparePaths, 4);
    if s.get(ExistsBackup) == 0 {
        return false;
    }
    if s.get(RenameBackupFinal) == 0 {
        s.log(Event::RestoreFailed, 0);
        return false;
    }
    s.log(Event::Restored, 0);
    true
}
fn inspect(s: &Driver) -> Option<bool> {
    use Op::*;
    if s.get(OpenFinal) == 0 {
        return None;
    }
    if s.word(1)? != u32::from(CACHE_VERSION) {
        return None;
    }
    let flags = s.word(1)?;
    if flags & !1 != 0 {
        return None;
    }
    let count = s.word(2)?;
    if count > MAX_RULES as u32 {
        return None;
    }
    if flags == 0 {
        return Some(false);
    }
    let mut bytes = 0;
    for _ in 0..count {
        let length = s.word(2)?;
        if length == 0 || length > MAX_CACHE_KEY_BYTES as u32 {
            return None;
        }
        bytes += length;
        let skip = length + WIRE_BYTES as u32;
        if bytes > SELECTOR_CAP as u32 || s.get(Available) < skip || s.call(Skip, skip) == 0 {
            return None;
        }
    }
    if s.get(Available) != 0 {
        return None;
    }
    Some(true)
}
fn cleanup(s: &Driver) {
    s.get(Op::Close);
    s.get(Op::RemoveTemporary);
}
fn save(s: &Driver, complete: bool) -> bool {
    use Op::*;
    if s.get(EmptyPath) != 0 {
        return false;
    }
    s.call(PreparePaths, 7);
    s.get(RemoveTemporary);
    if s.get(OpenTemporary) == 0 {
        return false;
    }
    if s.call(AllocateBuffer, CACHE_BUFFER_BYTES as u32) == 0 {
        s.log(Event::OutputOom, 0);
        cleanup(s);
        return false;
    }
    let count = s.count() as u16 as u32;
    let mut ok = true;
    if !s.handle().is_null() {
        let order = s.call(AllocateOrder, count) != 0;
        if !order
            || unsafe {
                css::cache_write_begin(s.handle(), complete as u8, (*s.io).order, s.count())
            } != 0
        {
            s.log(
                if order {
                    Event::OrderFailed
                } else {
                    Event::OrderOom
                },
                0,
            );
            cleanup(s);
            s.get(DropOrder);
            return false;
        }
        while ok {
            let mut written = 0;
            if unsafe {
                css::cache_write(
                    s.handle(),
                    (*s.io).order,
                    s.count(),
                    (*s.io).buffer,
                    (*s.io).capacity,
                    &mut written,
                )
            } != 0
            {
                ok = false;
                break;
            }
            if written == 0 {
                break;
            }
            ok = s.call(WriteBuffer, written as u32) == written as u32;
        }
        s.get(DropOrder);
    } else {
        unsafe {
            (*s.io).word = [CACHE_VERSION, u8::from(!complete), 0, 0];
        }
        ok = s.get(WriteWord) == 4;
    }
    if !ok || s.get(Close) == 0 {
        s.log(Event::WriteFailed, 0);
        cleanup(s);
        return false;
    }
    if s.get(ExistsFinal) != 0 {
        s.get(RemoveBackup);
        if s.get(RenameFinalBackup) == 0 {
            s.log(Event::BackupFailed, 0);
            s.get(RemoveTemporary);
            return false;
        }
    }
    if s.get(RenameTemporaryFinal) == 0 {
        s.log(Event::PromoteFailed, 0);
        s.get(RemoveTemporary);
        if s.get(ExistsBackup) != 0 && s.get(RenameBackupFinal) == 0 {
            s.log(Event::RollbackFailed, 0);
        }
        return false;
    }
    s.get(RemoveBackup);
    s.log(
        if complete {
            Event::SavedComplete
        } else {
            Event::SavedPartial
        },
        count,
    );
    true
}
fn load(s: &Driver) -> u32 {
    use Op::*;
    if s.get(EmptyPath) != 0 {
        return 2;
    }
    if s.get(OpenFinal) == 0 && (s.get(Restore) == 0 || s.get(OpenFinal) == 0) {
        return 2;
    }
    s.clear();
    if s.word(1) != Some(u32::from(CACHE_VERSION)) {
        s.log(Event::Version, s.value());
        s.get(Close);
        s.get(RemoveFinal);
        return 2;
    }
    if !s.ensure() {
        return 1;
    }
    if s.call(AllocateBuffer, READ_BUFFER_BYTES as u32) == 0 {
        s.log(Event::InputOom, 0);
        s.clear();
        return 1;
    }
    let mut flags = 0;
    let mut first = true;
    let version = [CACHE_VERSION];
    let mut status = s.read_cache(version.as_ptr(), 1, false);
    while status == 0 && s.get(Available) != 0 {
        let count = s.get(ReadBuffer) as i32;
        if count <= 0 {
            status = 2;
            break;
        }
        if first {
            flags = unsafe { *(*s.io).buffer } as u32;
            first = false;
        }
        status = s.read_cache(unsafe { (*s.io).buffer }, count as usize, false);
    }
    if status == 0 {
        status = s.read_cache(ptr::null(), 0, true);
    }
    if status != 0 {
        s.clear();
        return if status == 1 { 1 } else { 2 };
    }
    s.log(
        if flags & 1 != 0 {
            Event::LoadedPartial
        } else {
            Event::LoadedComplete
        },
        s.count() as u32,
    );
    0
}
fn stream(s: &Driver) -> u32 {
    use Op::*;
    if s.get(SourceValid) == 0 {
        s.log(Event::InvalidSource, 0);
        return 2;
    }
    if !s.ensure() {
        return 1;
    }
    if s.call(AllocateBuffer, READ_BUFFER_BYTES as u32) == 0 {
        s.log(Event::SourceOom, 0);
        return 1;
    }
    let mut partial = false;
    let mut total = 0u32;
    while s.get(Available) != 0 {
        let count = s.get(ReadBuffer) as i32;
        if count <= 0 {
            break;
        }
        total = total.wrapping_add(count as u32);
        partial |= s.feed(count as usize, false) != 0;
    }
    let result = s.feed(0, true);
    s.log(Event::Parsed, total);
    if partial || result == 1 {
        1
    } else {
        result
    }
}
/// # Safety
/// Services and io own valid disjoint file/buffer/owner slots until return.
/// Callbacks can update io buffer/order, but cannot mutate CSS or retain pointers.
/// Restore uses a separate native file/path scope with its own io slot.
#[no_mangle]
pub unsafe extern "C" fn xp_book_css(
    services: *const Services,
    io: *mut Io,
    diagnostics: *const Diagnostics,
    action: u32,
    complete: u8,
) -> u32 {
    use Op::*;
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    let s = Driver {
        services: s,
        io,
        diagnostics: diagnostics.as_ref().copied().unwrap_or_default(),
    };
    match action {
        x if x == Action::Restore as u32 => restore(&s) as u32,
        x if x == Action::Delete as u32 => {
            if s.get(ExistsFinal) != 0 {
                s.get(RemoveFinal);
            }
            s.get(RemoveTemporary);
            s.get(RemoveBackup);
            0
        }
        x if x == Action::Inspect as u32 => {
            if s.get(EmptyPath) != 0 || (s.get(ExistsFinal) == 0 && s.get(Restore) == 0) {
                return 0;
            }
            match inspect(&s) {
                Some(false) => 1,
                Some(true) => 2,
                None => 3,
            }
        }
        x if x == Action::Save as u32 => save(&s, complete != 0) as u32,
        x if x == Action::Load as u32 => load(&s),
        x if x == Action::Stream as u32 => stream(&s),
        _ => 0,
    }
}
