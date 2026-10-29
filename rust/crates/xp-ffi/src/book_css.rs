//! CSS file transactions and streaming policy; codec and owner stay borrowed.
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
    EmptyPath,
    PreparePaths,
    ExistsFinal,
    ExistsBackup,
    RenameBackupFinal,
    RemoveFinal,
    RemoveTemporary,
    RemoveBackup,
    RenameFinalBackup,
    RenameTemporaryFinal,
    OpenFinal,
    OpenTemporary,
    Close,
    ReadWord,
    Word,
    Available,
    Skip,
    Ensure,
    Clear,
    AllocateBuffer,
    ReadBuffer,
    FirstByte,
    ReadCacheVersion,
    ReadCacheBuffer,
    ReadCacheEnd,
    RuleCount,
    HasHandle,
    AllocateOrder,
    PrepareOrder,
    WriteNext,
    Written,
    WriteBuffer,
    WriteWord,
    DropOrder,
    SourceValid,
    FeedBuffer,
    FeedEnd,
    Restore,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Event {
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
}
impl Services {
    fn call(&self, op: Op, arg: u32) -> u32 {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, arg) })
    }
    fn get(&self, op: Op) -> u32 {
        self.call(op, 0)
    }
    fn log(&self, event: Event, arg: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, arg) };
        }
    }
    fn word(&self, width: u32) -> Option<u32> {
        (self.call(Op::ReadWord, width) == width).then(|| self.get(Op::Word))
    }
}
fn restore(s: &Services) -> bool {
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
fn inspect(s: &Services) -> Option<bool> {
    use Op::*;
    if s.get(OpenFinal) == 0 {
        return None;
    }
    if s.word(1)? != 19 {
        return None;
    }
    let flags = s.word(1)?;
    if flags & !1 != 0 {
        return None;
    }
    let count = s.word(2)?;
    if count > 1500 {
        return None;
    }
    if flags == 0 {
        return Some(false);
    }
    let mut bytes = 0;
    for _ in 0..count {
        let length = s.word(2)?;
        if length == 0 || length > 257 {
            return None;
        }
        bytes += length;
        let skip = length + 88;
        if bytes > 32 * 1024 || s.get(Available) < skip || s.call(Skip, skip) == 0 {
            return None;
        }
    }
    if s.get(Available) != 0 {
        return None;
    }
    Some(true)
}
fn cleanup(s: &Services) {
    s.get(Op::Close);
    s.get(Op::RemoveTemporary);
}
fn save(s: &Services, complete: bool) -> bool {
    use Op::*;
    if s.get(EmptyPath) != 0 {
        return false;
    }
    s.call(PreparePaths, 7);
    s.get(RemoveTemporary);
    if s.get(OpenTemporary) == 0 {
        return false;
    }
    if s.call(AllocateBuffer, 347) == 0 {
        s.log(Event::OutputOom, 0);
        cleanup(s);
        return false;
    }
    let count = s.get(RuleCount) as u16 as u32;
    let mut ok = true;
    if s.get(HasHandle) != 0 {
        let order = s.call(AllocateOrder, count) != 0;
        if !order || s.call(PrepareOrder, complete as u32) != 0 {
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
            if s.get(WriteNext) != 0 {
                ok = false;
                break;
            }
            if s.get(Written) == 0 {
                break;
            }
            ok = s.get(WriteBuffer) != 0;
        }
        s.get(DropOrder);
    } else {
        ok = s.call(WriteWord, 19 | ((!complete as u32) << 8)) != 0;
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
fn load(s: &Services) -> u32 {
    use Op::*;
    if s.get(EmptyPath) != 0 {
        return 2;
    }
    if s.get(OpenFinal) == 0 && (s.get(Restore) == 0 || s.get(OpenFinal) == 0) {
        return 2;
    }
    s.get(Clear);
    if s.word(1) != Some(19) {
        s.log(Event::Version, s.get(Word));
        s.get(Close);
        s.get(RemoveFinal);
        return 2;
    }
    if s.get(Ensure) == 0 {
        return 1;
    }
    if s.call(AllocateBuffer, 512) == 0 {
        s.log(Event::InputOom, 0);
        s.get(Clear);
        return 1;
    }
    let mut flags = 0;
    let mut first = true;
    let mut status = s.get(ReadCacheVersion);
    while status == 0 && s.get(Available) != 0 {
        if s.get(ReadBuffer) as i32 <= 0 {
            status = 2;
            break;
        }
        if first {
            flags = s.get(FirstByte);
            first = false;
        }
        status = s.get(ReadCacheBuffer);
    }
    if status == 0 {
        status = s.get(ReadCacheEnd);
    }
    if status != 0 {
        s.get(Clear);
        return if status == 1 { 1 } else { 2 };
    }
    s.log(
        if flags & 1 != 0 {
            Event::LoadedPartial
        } else {
            Event::LoadedComplete
        },
        s.get(RuleCount),
    );
    0
}
fn stream(s: &Services) -> u32 {
    use Op::*;
    if s.get(SourceValid) == 0 {
        s.log(Event::InvalidSource, 0);
        return 2;
    }
    if s.get(Ensure) == 0 {
        return 1;
    }
    if s.call(AllocateBuffer, 512) == 0 {
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
        partial |= s.get(FeedBuffer) != 0;
    }
    let result = s.get(FeedEnd);
    s.log(Event::Parsed, total);
    if partial || result == 1 {
        1
    } else {
        result
    }
}
/// # Safety
/// Services own all file/buffer/codec objects until return; no same-operation reentry.
#[no_mangle]
pub unsafe extern "C" fn xp_book_css(services: *const Services, action: u32, complete: u8) -> u32 {
    use Op::*;
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    match action {
        0 => restore(&s) as u32,
        1 => {
            if s.get(ExistsFinal) != 0 {
                s.get(RemoveFinal);
            }
            s.get(RemoveTemporary);
            s.get(RemoveBackup);
            0
        }
        2 => {
            if s.get(EmptyPath) != 0 || (s.get(ExistsFinal) == 0 && s.get(Restore) == 0) {
                return 0;
            }
            match inspect(&s) {
                Some(false) => 1,
                Some(true) => 2,
                None => 3,
            }
        }
        3 => save(&s, complete != 0) as u32,
        4 => load(&s),
        5 => stream(&s),
        _ => 0,
    }
}
