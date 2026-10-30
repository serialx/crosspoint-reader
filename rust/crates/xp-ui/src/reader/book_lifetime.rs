//! EPUB load, progress restore and teardown sequencing.
use super::services::{Core, Guard};
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Load = 0,
    Teardown = 1,
    Progress = 2,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    Spine = 0,
    NextPage = 1,
    CachedSpine = 2,
    CachedCount = 3,
    HasCachedOffset = 4,
    CachedOffset = 5,
    RadioReleased = 6,
    FootnoteDepth = 7,
    PendingReadMove = 8,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    CachePresent = 0,
    EmbeddedStyle = 1,
    ProgressByte = 2,
    TextReference = 3,
    HasBook = 4,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    NewLoaded = 0,
    DropLoaded = 1,
    LogAllocationFailed = 2,
    DisableFast = 3,
    PopupIndex = 4,
    LoanBegin = 5,
    LoanDrop = 6,
    Load = 7,
    ProtectionError = 8,
    LogLoadFailed = 9,
    MoveLoaded = 10,
    ClearImageFailures = 11,
    InstallExtractor = 12,
    SetupCache = 13,
    OpenProgress = 14,
    DropFile = 15,
    ReadProgress = 16,
    LogSentinel = 17,
    LogProgress = 18,
    LogTextReference = 19,
    LoadLinks = 20,
    LoadBookmarks = 21,
    LoadClippings = 22,
    LogClippingsFailed = 23,
    RemoveExtractor = 24,
    SettleOverlay = 25,
    DiscardOverlay = 26,
    RadioAfter = 27,
    SaveLinks = 28,
    DropSection = 29,
    UnloadClippings = 30,
    PrepareMove = 31,
    DropMovePaths = 32,
    DropBook = 33,
    MoveBook = 34,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub write: Option<unsafe extern "C" fn(*mut c_void, u8, u32)>,
    pub query: Option<unsafe extern "C" fn(*mut c_void, u8, u32) -> u32>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
}
struct Engine {
    core: Core,
    query: unsafe extern "C" fn(*mut c_void, u8, u32) -> u32,
}
impl Engine {
    fn f(&self, field: Field) -> u32 {
        self.core.read(field as u8)
    }
    fn set(&self, field: Field, value: u32) {
        self.core.write(field as u8, value);
    }
    fn q(&self, query: Query) -> u32 {
        self.qa(query, 0)
    }
    fn qa(&self, query: Query, a: u32) -> u32 {
        unsafe { (self.query)(self.core.context, query as u8, a) }
    }
    fn c(&self, command: Command, a: i32) -> u32 {
        self.core.command(command as u8, a, 0)
    }
    fn op(&self, command: Command) -> u32 {
        self.core.op(command as u8)
    }
    fn guard(&self, release: Command) -> Guard<'_> {
        self.core.guard(release as u8)
    }
    fn decode_progress(&self) {
        let length = self.op(Command::ReadProgress);
        if !matches!(length, 4 | 6 | 10) {
            return;
        }
        let mut data = [0; 10];
        for (i, byte) in data.iter_mut().enumerate().take(length as usize) {
            *byte = self.qa(Query::ProgressByte, i as u32) as u8;
        }
        let Some((progress, sentinel)) = super::progress_decode(&data[..length as usize]) else {
            return;
        };
        self.set(Field::Spine, u32::from(progress.spine));
        self.set(Field::NextPage, u32::from(progress.page));
        if sentinel {
            self.op(Command::LogSentinel);
        }
        self.set(Field::CachedSpine, self.f(Field::Spine));
        self.op(Command::LogProgress);
        if progress.has_count != 0 {
            self.set(Field::CachedCount, u32::from(progress.count));
        }
        if progress.has_offset != 0 {
            self.set(Field::CachedOffset, progress.offset);
        }
    }
    fn progress(&self) -> Guard<'_> {
        let opened = self.op(Command::OpenProgress) != 0;
        let file = self.guard(Command::DropFile);
        if opened {
            self.decode_progress();
        }
        file
    }
    fn load(&self) -> bool {
        let allocated = self.op(Command::NewLoaded) != 0;
        let _loaded = self.guard(Command::DropLoaded);
        if !allocated {
            self.op(Command::LogAllocationFailed);
            return false;
        }
        let uncached = self.q(Query::CachePresent) == 0;
        if uncached {
            self.op(Command::DisableFast);
            self.op(Command::PopupIndex);
        }
        let loaded = {
            let _loan = if uncached {
                self.op(Command::LoanBegin);
                Some(self.guard(Command::LoanDrop))
            } else {
                None
            };
            self.c(Command::Load, i32::from(self.q(Query::EmbeddedStyle) == 0)) != 0
        };
        if !loaded {
            self.op(Command::ProtectionError);
            self.op(Command::LogLoadFailed);
            return false;
        }
        self.op(Command::MoveLoaded);
        self.op(Command::ClearImageFailures);
        self.op(Command::InstallExtractor);
        self.op(Command::SetupCache);
        // The original file remains open through link/bookmark/clipping restoration.
        let _file = self.progress();
        if self.f(Field::Spine) == 0 {
            let text_spine = self.q(Query::TextReference);
            if text_spine != 0 {
                self.set(Field::Spine, text_spine);
                self.set(Field::HasCachedOffset, 0);
                self.c(Command::LogTextReference, text_spine as i32);
            }
        }
        self.op(Command::LoadLinks);
        self.op(Command::LoadBookmarks);
        if self.op(Command::LoadClippings) == 0 {
            self.op(Command::LogClippingsFailed);
        }
        true
    }
    fn teardown(&self) {
        self.op(Command::RemoveExtractor);
        self.op(Command::SettleOverlay);
        self.op(Command::DiscardOverlay);
        if self.f(Field::RadioReleased) != 0 {
            self.op(Command::RadioAfter);
        }
        if (self.f(Field::FootnoteDepth) as i32) > 0 && self.q(Query::HasBook) != 0 {
            self.op(Command::SaveLinks);
        }
        self.op(Command::DropSection);
        self.op(Command::UnloadClippings);
        if self.f(Field::PendingReadMove) != 0 && self.q(Query::HasBook) != 0 {
            self.op(Command::PrepareMove);
            let _paths = self.guard(Command::DropMovePaths);
            self.op(Command::DropBook);
            self.op(Command::MoveBook);
        } else {
            self.op(Command::DropBook);
        }
    }
}
/// # Safety
/// Services remain live for the synchronous call. Native callbacks retain only
/// the installed Epub extractor owner; service/context pointers never escape.
pub unsafe fn run(s: *const Services, operation: u8) -> u8 {
    if operation > Operation::Progress as u8 {
        return 0;
    }
    let Some(s) = s.as_ref() else {
        return 0;
    };
    let (Some(read), Some(write), Some(query), Some(command)) =
        (s.read, s.write, s.query, s.command)
    else {
        return 0;
    };
    let e = Engine {
        core: Core {
            context: s.context,
            read,
            write,
            command,
        },
        query,
    };
    match operation {
        0 => e.load().into(),
        1 => {
            e.teardown();
            0
        }
        2 => {
            let _file = e.progress();
            0
        }
        _ => 0,
    }
}

const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<Services>() == 5 * size_of::<usize>());
    assert!(align_of::<Services>() == align_of::<usize>());
    assert!(offset_of!(Services, context) == 0);
    assert!(offset_of!(Services, read) == size_of::<usize>());
    assert!(offset_of!(Services, write) == 2 * size_of::<usize>());
    assert!(offset_of!(Services, query) == 3 * size_of::<usize>());
    assert!(offset_of!(Services, command) == 4 * size_of::<usize>());
    assert!(Operation::Load as u8 == 0);
    assert!(Operation::Teardown as u8 == 1);
    assert!(Operation::Progress as u8 == 2);
    assert!(Field::Spine as u8 == 0);
    assert!(Field::NextPage as u8 == 1);
    assert!(Field::CachedSpine as u8 == 2);
    assert!(Field::CachedCount as u8 == 3);
    assert!(Field::HasCachedOffset as u8 == 4);
    assert!(Field::CachedOffset as u8 == 5);
    assert!(Field::RadioReleased as u8 == 6);
    assert!(Field::FootnoteDepth as u8 == 7);
    assert!(Field::PendingReadMove as u8 == 8);
    assert!(Query::CachePresent as u8 == 0);
    assert!(Query::EmbeddedStyle as u8 == 1);
    assert!(Query::ProgressByte as u8 == 2);
    assert!(Query::TextReference as u8 == 3);
    assert!(Query::HasBook as u8 == 4);
    assert!(Command::NewLoaded as u8 == 0);
    assert!(Command::DropLoaded as u8 == 1);
    assert!(Command::LogAllocationFailed as u8 == 2);
    assert!(Command::DisableFast as u8 == 3);
    assert!(Command::PopupIndex as u8 == 4);
    assert!(Command::LoanBegin as u8 == 5);
    assert!(Command::LoanDrop as u8 == 6);
    assert!(Command::Load as u8 == 7);
    assert!(Command::ProtectionError as u8 == 8);
    assert!(Command::LogLoadFailed as u8 == 9);
    assert!(Command::MoveLoaded as u8 == 10);
    assert!(Command::ClearImageFailures as u8 == 11);
    assert!(Command::InstallExtractor as u8 == 12);
    assert!(Command::SetupCache as u8 == 13);
    assert!(Command::OpenProgress as u8 == 14);
    assert!(Command::DropFile as u8 == 15);
    assert!(Command::ReadProgress as u8 == 16);
    assert!(Command::LogSentinel as u8 == 17);
    assert!(Command::LogProgress as u8 == 18);
    assert!(Command::LogTextReference as u8 == 19);
    assert!(Command::LoadLinks as u8 == 20);
    assert!(Command::LoadBookmarks as u8 == 21);
    assert!(Command::LoadClippings as u8 == 22);
    assert!(Command::LogClippingsFailed as u8 == 23);
    assert!(Command::RemoveExtractor as u8 == 24);
    assert!(Command::SettleOverlay as u8 == 25);
    assert!(Command::DiscardOverlay as u8 == 26);
    assert!(Command::RadioAfter as u8 == 27);
    assert!(Command::SaveLinks as u8 == 28);
    assert!(Command::DropSection as u8 == 29);
    assert!(Command::UnloadClippings as u8 == 30);
    assert!(Command::PrepareMove as u8 == 31);
    assert!(Command::DropMovePaths as u8 == 32);
    assert!(Command::DropBook as u8 == 33);
    assert!(Command::MoveBook as u8 == 34);
};

#[cfg(test)]
mod tests {
    use super::*;
    struct Provider {
        fields: [u32; 9],
        live: [i32; 4],
        flags: u32,
        length: i32,
        book: bool,
        calls: u32,
        visited: [bool; 35],
    }
    impl Provider {
        fn services(&mut self) -> Services {
            Services {
                context: (self as *mut Self).cast(),
                read: Some(read),
                write: Some(write),
                query: Some(query),
                command: Some(command),
            }
        }
    }
    unsafe extern "C" fn read(context: *mut c_void, field: u8) -> u32 {
        (*context.cast::<Provider>()).fields[usize::from(field)]
    }
    unsafe extern "C" fn write(context: *mut c_void, field: u8, value: u32) {
        (*context.cast::<Provider>()).fields[usize::from(field)] = value;
    }
    unsafe extern "C" fn query(context: *mut c_void, query: u8, argument: u32) -> u32 {
        let p = &mut *context.cast::<Provider>();
        if query == Query::CachePresent as u8 {
            return (p.flags >> 2) & 1;
        }
        if query == Query::HasBook as u8 {
            return p.book.into();
        }
        if query == Query::TextReference as u8 {
            assert_eq!(p.live[2], 1);
            return 2;
        }
        if query == Query::ProgressByte as u8 {
            assert_eq!(p.live[2], 1);
            assert!(argument < 10);
            return if argument == 2 || argument == 3 {
                255
            } else {
                0
            };
        }
        assert_eq!(query, Query::EmbeddedStyle as u8);
        0
    }
    unsafe extern "C" fn command(context: *mut c_void, id: u8, _: i32, _: i32) -> u32 {
        let p = &mut *context.cast::<Provider>();
        p.calls += 1;
        p.visited[usize::from(id)] = true;
        for (slot, acquire, release) in [
            (0, Command::NewLoaded, Command::DropLoaded),
            (1, Command::LoanBegin, Command::LoanDrop),
            (2, Command::OpenProgress, Command::DropFile),
            (3, Command::PrepareMove, Command::DropMovePaths),
        ] {
            if id == acquire as u8 {
                assert_eq!(p.live[slot], 0);
                p.live[slot] = 1;
            }
            if id == release as u8 {
                assert_eq!(p.live[slot], 1);
                p.live[slot] = 0;
            }
        }
        if id == Command::NewLoaded as u8 {
            return p.flags & 1;
        }
        if id == Command::Load as u8 {
            assert_eq!(p.live[0], 1);
            assert_eq!(p.live[1], i32::from(p.flags & 4 == 0));
            return (p.flags >> 1) & 1;
        }
        if id == Command::DropLoaded as u8 {
            assert_eq!(p.live, [0; 4]);
        }
        if id == Command::OpenProgress as u8 {
            return (p.flags >> 3) & 1;
        }
        if id == Command::ReadProgress as u8 {
            assert_eq!(p.live[2], 1);
            return p.length as u32;
        }
        if id == Command::MoveLoaded as u8 {
            p.book = true;
        }
        if id == Command::DropBook as u8 {
            p.book = false;
        }
        if id == Command::MoveBook as u8 {
            assert!(!p.book);
            assert_eq!(p.live[3], 1);
        }
        if id == Command::LoadLinks as u8
            || id == Command::LoadBookmarks as u8
            || id == Command::LoadClippings as u8
        {
            assert_eq!(
                p.live[2], 1,
                "the original progress file must still be open"
            );
            return (p.flags >> 4) & 1;
        }
        0
    }
    #[test]
    fn every_return_balances_temporary_resources() {
        let mut visited = [false; 35];
        for operation in 0..4 {
            for flags in 0..256 {
                for length in [-1, 0, 3, 4, 5, 6, 9, 10, 11, 12] {
                    for depth in [0i32, 2, -1] {
                        let mut p = Provider {
                            fields: [0; 9],
                            live: [0; 4],
                            flags,
                            length,
                            book: flags & 64 != 0,
                            calls: 0,
                            visited: [false; 35],
                        };
                        p.fields[Field::Spine as usize] = (flags >> 5) & 1;
                        p.fields[Field::RadioReleased as usize] = (flags >> 5) & 1;
                        p.fields[Field::FootnoteDepth as usize] = depth as u32;
                        p.fields[Field::PendingReadMove as usize] = (flags >> 7) & 1;
                        unsafe {
                            run(&p.services(), operation);
                        }
                        assert_eq!(
                            p.live, [0; 4],
                            "operation={operation}, flags={flags}, length={length}, depth={depth}"
                        );
                        if operation == 3 {
                            assert_eq!(p.calls, 0);
                        }
                        for (all, saw) in visited.iter_mut().zip(p.visited) {
                            *all |= saw;
                        }
                    }
                }
            }
        }
        assert!(
            visited.into_iter().all(|v| v),
            "all command paths must be exercised"
        );
    }
    #[test]
    fn null_or_incomplete_services_have_no_effects() {
        unsafe {
            assert_eq!(run(core::ptr::null(), 0), 0);
        }
        for missing in 0..4 {
            let mut p = Provider {
                fields: [0; 9],
                live: [0; 4],
                flags: 0,
                length: 0,
                book: false,
                calls: 0,
                visited: [false; 35],
            };
            let mut s = p.services();
            match missing {
                0 => s.read = None,
                1 => s.write = None,
                2 => s.query = None,
                _ => s.command = None,
            }
            unsafe {
                assert_eq!(run(&s, 0), 0);
            }
            assert_eq!(p.calls, 0);
            assert_eq!(p.live, [0; 4]);
        }
    }
}
