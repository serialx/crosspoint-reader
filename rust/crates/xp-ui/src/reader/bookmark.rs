//! Bookmark policy and consumed link-stack persistence.
use super::services::Core;
use super::{links_decode, links_encode, LinkPosition};
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Load = 0,
    Add = 1,
    Flag = 2,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    HasSection = 0,
    HasBook = 1,
    Spine = 2,
    Page = 3,
    SectionCount = 4,
    BookmarkCount = 5,
    HasCurrentOffset = 6,
    CurrentOffset = 7,
    LoadedOffset = 8,
    Removed = 9,
    Bookmarked = 10,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    Lock = 0,
    Unlock = 1,
    LogToggle = 2,
    Estimate = 3,
    ProgressBegin = 4,
    ProgressEnd = 5,
    Range = 6,
    EraseMatches = 7,
    AnyMatch = 8,
    TextBegin = 9,
    TextRead = 10,
    TextEnd = 11,
    EntryBegin = 12,
    EntryOffset = 13,
    EntryInsert = 14,
    EntryEnd = 15,
    OffsetRead = 16,
    Clear = 17,
    Load = 18,
    Save = 19,
    LogSaveFailed = 20,
    Update = 21,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum LinkQuery {
    Depth = 0,
    Spine = 1,
    Page = 2,
    SpineCount = 3,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum LinkCommand {
    PathBegin = 0,
    PathEnd = 1,
    Exists = 2,
    FileBegin = 3,
    FileEnd = 4,
    OpenRead = 5,
    OpenWrite = 6,
    Remove = 7,
    LogWriteFailed = 8,
    LogLoaded = 9,
    SetDepth = 10,
    SetSpine = 11,
    SetPage = 12,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ProgressRange {
    pub start: f32,
    pub end: f32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct BookmarkPosition {
    pub percentage: f32,
    pub spine: i32,
    pub page: i32,
    pub count: i32,
}
fn clamp(value: f32, lo: f32, hi: f32) -> f32 {
    if value < lo {
        lo
    } else if hi < value {
        hi
    } else {
        value
    }
}
pub fn range(page: i32, count: i32) -> ProgressRange {
    if count <= 1 {
        return ProgressRange {
            start: 0.0,
            end: 1.0,
        };
    }
    let step = 1.0 / (count - 1) as f32;
    let anchor = clamp(page as f32 * step, 0.0, 1.0);
    ProgressRange {
        start: (anchor - step * 0.5).max(0.0),
        end: (anchor + step * 0.5).min(1.0),
    }
}
pub fn matches(
    bookmark: BookmarkPosition,
    spine: i32,
    page: i32,
    count: i32,
    range: ProgressRange,
) -> bool {
    if bookmark.spine == spine && bookmark.page == page && bookmark.count == count {
        return true;
    }
    let progress = clamp(bookmark.percentage, 0.0, 1.0);
    progress + 0.0001 >= range.start && progress - 0.0001 <= range.end
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub write: Option<unsafe extern "C" fn(*mut c_void, u8, u32)>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
}
struct Policy(Core);
impl Policy {
    fn f(&self, f: Field) -> u32 {
        self.0.read(f as u8)
    }
    fn set(&self, f: Field, v: bool) {
        self.0.write(f as u8, u32::from(v));
    }
    fn c(&self, c: Command, a: i32, b: i32) -> u32 {
        self.0.command(c as u8, a, b)
    }
    fn op(&self, c: Command) -> u32 {
        self.c(c, 0, 0)
    }
    fn flag(&self) {
        if self.f(Field::HasSection) == 0
            || self.f(Field::HasBook) == 0
            || self.f(Field::BookmarkCount) == 0
        {
            self.set(Field::Bookmarked, false);
            return;
        }
        let count = self.op(Command::Estimate) as i32;
        let page = self.f(Field::Page) as i32;
        self.c(Command::Range, page, count);
        self.set(
            Field::Bookmarked,
            self.c(Command::AnyMatch, page, count) != 0,
        );
    }
    fn load(&self) {
        self.op(Command::Clear);
        if self.f(Field::HasBook) == 0 {
            self.set(Field::Bookmarked, false);
            return;
        }
        self.op(Command::Load);
        self.flag();
    }
    fn add(&self) {
        if self.f(Field::HasSection) == 0 || self.f(Field::HasBook) == 0 {
            return;
        }
        self.op(Command::LogToggle);
        self.op(Command::Lock);
        let lock = self.0.guard(Command::Unlock as u8);
        let count = self.op(Command::Estimate) as i32;
        let page = self.f(Field::Page) as i32;
        drop(lock);
        self.op(Command::ProgressBegin);
        let _progress = self.0.guard(Command::ProgressEnd as u8);
        self.c(Command::Range, page, count);
        if self.c(Command::EraseMatches, page, count) != 0 {
            self.set(Field::Removed, true);
            self.set(Field::Bookmarked, false);
        } else {
            self.op(Command::TextBegin);
            let text = self.0.guard(Command::TextEnd as u8);
            if page >= 0 && page < count {
                self.op(Command::TextRead);
            }
            self.c(Command::EntryBegin, page, count);
            let entry = self.0.guard(Command::EntryEnd as u8);
            if self.f(Field::HasCurrentOffset) != 0 {
                self.c(Command::EntryOffset, self.f(Field::CurrentOffset) as i32, 0);
            } else if page >= 0
                && page < (self.f(Field::SectionCount) as i32)
                && self.c(Command::OffsetRead, page, 0) != 0
            {
                self.c(Command::EntryOffset, self.f(Field::LoadedOffset) as i32, 0);
            }
            self.op(Command::EntryInsert);
            self.set(Field::Removed, false);
            self.set(Field::Bookmarked, true);
            drop(entry);
            drop(text);
        }
        if self.op(Command::Save) == 0 {
            self.op(Command::LogSaveFailed);
        }
        self.op(Command::Update);
    }
}
/// # Safety
/// Services obey the synchronous bookmark resource contract.
pub unsafe fn run(s: *const Services, operation: u8) {
    let Some(s) = s.as_ref() else {
        return;
    };
    let (Some(read), Some(write), Some(command)) = (s.read, s.write, s.command) else {
        return;
    };
    let p = Policy(Core {
        context: s.context,
        read,
        write,
        command,
    });
    match operation {
        x if x == Operation::Load as u8 => p.load(),
        x if x == Operation::Add as u8 => p.add(),
        x if x == Operation::Flag as u8 => p.flag(),
        _ => {}
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LinkServices {
    pub context: *mut c_void,
    pub query: Option<unsafe extern "C" fn(*mut c_void, u8, u32) -> u32>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
    pub transfer: Option<unsafe extern "C" fn(*mut c_void, u8, *mut u8, u32) -> i32>,
}
struct Links {
    context: *mut c_void,
    query: unsafe extern "C" fn(*mut c_void, u8, u32) -> u32,
    command: unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32,
    transfer: unsafe extern "C" fn(*mut c_void, u8, *mut u8, u32) -> i32,
}
struct LinkGuard<'a>(&'a Links, LinkCommand);
impl Drop for LinkGuard<'_> {
    fn drop(&mut self) {
        self.0.op(self.1);
    }
}
impl Links {
    fn q(&self, q: LinkQuery, index: u32) -> u32 {
        unsafe { (self.query)(self.context, q as u8, index) }
    }
    fn c(&self, c: LinkCommand, a: i32, b: i32) -> u32 {
        unsafe { (self.command)(self.context, c as u8, a, b) }
    }
    fn op(&self, c: LinkCommand) -> u32 {
        self.c(c, 0, 0)
    }
    fn positions(&self) -> [LinkPosition; 3] {
        core::array::from_fn(|i| LinkPosition {
            spine: self.q(LinkQuery::Spine, i as u32) as i32,
            page: self.q(LinkQuery::Page, i as u32) as i32,
        })
    }
    fn save(&self) {
        let mut data = [0u8; 13];
        let positions = self.positions();
        let size = links_encode(&positions, self.q(LinkQuery::Depth, 0) as u8, &mut data);
        self.op(LinkCommand::FileBegin);
        let _file = LinkGuard(self, LinkCommand::FileEnd);
        if self.op(LinkCommand::OpenWrite) == 0 {
            return;
        }
        let written = unsafe { (self.transfer)(self.context, 1, data.as_mut_ptr(), size) };
        if written as u32 != size {
            self.op(LinkCommand::LogWriteFailed);
        }
    }
    fn load(&self) {
        self.op(LinkCommand::PathBegin);
        let _path = LinkGuard(self, LinkCommand::PathEnd);
        if self.op(LinkCommand::Exists) == 0 {
            return;
        }
        self.op(LinkCommand::FileBegin);
        let file = LinkGuard(self, LinkCommand::FileEnd);
        if self.op(LinkCommand::OpenRead) != 0 {
            let mut data = [0u8; 13];
            let size = unsafe { (self.transfer)(self.context, 0, data.as_mut_ptr(), 13) };
            let mut positions = self.positions();
            let count = self.q(LinkQuery::SpineCount, 0) as i32;
            let depth = if (0..=13).contains(&size) {
                links_decode(&data[..size as usize], count, &mut positions)
            } else {
                0
            };
            for (i, p) in positions.iter().enumerate() {
                self.c(LinkCommand::SetSpine, i as i32, p.spine);
                self.c(LinkCommand::SetPage, i as i32, p.page);
            }
            if depth != 0 {
                self.c(LinkCommand::SetDepth, i32::from(depth), 0);
                self.c(LinkCommand::LogLoaded, i32::from(depth), 0);
            }
        }
        drop(file);
        self.op(LinkCommand::Remove);
    }
}
/// # Safety
/// Callbacks borrow buffers synchronously and obey the link resource contract.
pub unsafe fn link_run(s: *const LinkServices, load: u8) {
    let Some(s) = s.as_ref() else {
        return;
    };
    let (Some(query), Some(command), Some(transfer)) = (s.query, s.command, s.transfer) else {
        return;
    };
    let p = Links {
        context: s.context,
        query,
        command,
        transfer,
    };
    match load {
        0 => p.save(),
        1 => p.load(),
        _ => {}
    }
}
const _: () = {
    assert!(Operation::Load as u8 == 0);
    assert!(Operation::Add as u8 == 1);
    assert!(Operation::Flag as u8 == 2);
    assert!(Field::HasSection as u8 == 0);
    assert!(Field::HasBook as u8 == 1);
    assert!(Field::Spine as u8 == 2);
    assert!(Field::Page as u8 == 3);
    assert!(Field::SectionCount as u8 == 4);
    assert!(Field::BookmarkCount as u8 == 5);
    assert!(Field::HasCurrentOffset as u8 == 6);
    assert!(Field::CurrentOffset as u8 == 7);
    assert!(Field::LoadedOffset as u8 == 8);
    assert!(Field::Removed as u8 == 9);
    assert!(Field::Bookmarked as u8 == 10);
    assert!(Command::Lock as u8 == 0);
    assert!(Command::Unlock as u8 == 1);
    assert!(Command::LogToggle as u8 == 2);
    assert!(Command::Estimate as u8 == 3);
    assert!(Command::ProgressBegin as u8 == 4);
    assert!(Command::ProgressEnd as u8 == 5);
    assert!(Command::Range as u8 == 6);
    assert!(Command::EraseMatches as u8 == 7);
    assert!(Command::AnyMatch as u8 == 8);
    assert!(Command::TextBegin as u8 == 9);
    assert!(Command::TextRead as u8 == 10);
    assert!(Command::TextEnd as u8 == 11);
    assert!(Command::EntryBegin as u8 == 12);
    assert!(Command::EntryOffset as u8 == 13);
    assert!(Command::EntryInsert as u8 == 14);
    assert!(Command::EntryEnd as u8 == 15);
    assert!(Command::OffsetRead as u8 == 16);
    assert!(Command::Clear as u8 == 17);
    assert!(Command::Load as u8 == 18);
    assert!(Command::Save as u8 == 19);
    assert!(Command::LogSaveFailed as u8 == 20);
    assert!(Command::Update as u8 == 21);
    assert!(LinkQuery::Depth as u8 == 0);
    assert!(LinkQuery::Spine as u8 == 1);
    assert!(LinkQuery::Page as u8 == 2);
    assert!(LinkQuery::SpineCount as u8 == 3);
    assert!(LinkCommand::PathBegin as u8 == 0);
    assert!(LinkCommand::PathEnd as u8 == 1);
    assert!(LinkCommand::Exists as u8 == 2);
    assert!(LinkCommand::FileBegin as u8 == 3);
    assert!(LinkCommand::FileEnd as u8 == 4);
    assert!(LinkCommand::OpenRead as u8 == 5);
    assert!(LinkCommand::OpenWrite as u8 == 6);
    assert!(LinkCommand::Remove as u8 == 7);
    assert!(LinkCommand::LogWriteFailed as u8 == 8);
    assert!(LinkCommand::LogLoaded as u8 == 9);
    assert!(LinkCommand::SetDepth as u8 == 10);
    assert!(LinkCommand::SetSpine as u8 == 11);
    assert!(LinkCommand::SetPage as u8 == 12);
    assert!(core::mem::size_of::<Services>() == 4 * core::mem::size_of::<usize>());
    assert!(core::mem::align_of::<Services>() == core::mem::align_of::<usize>());
    assert!(core::mem::offset_of!(Services, context) == 0);
    assert!(core::mem::offset_of!(Services, read) == core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(Services, write) == 2 * core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(Services, command) == 3 * core::mem::size_of::<usize>());
    assert!(core::mem::size_of::<LinkServices>() == 4 * core::mem::size_of::<usize>());
    assert!(core::mem::align_of::<LinkServices>() == core::mem::align_of::<usize>());
    assert!(core::mem::offset_of!(LinkServices, context) == 0);
    assert!(core::mem::offset_of!(LinkServices, query) == core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(LinkServices, command) == 2 * core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(LinkServices, transfer) == 3 * core::mem::size_of::<usize>());
    assert!(core::mem::size_of::<ProgressRange>() == 8);
    assert!(core::mem::size_of::<BookmarkPosition>() == 16);
};
#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::vec::Vec;
    #[derive(Default)]
    struct Fake {
        bits: u32,
        live: [bool; 4],
        events: Vec<u8>,
        fields: [u32; 11],
        file: bool,
        path: bool,
        bytes: [u8; 13],
        length: i32,
    }
    unsafe extern "C" fn read(c: *mut c_void, f: u8) -> u32 {
        let fakes = &mut *c.cast::<Fake>();
        fakes.fields[usize::from(f)]
    }
    unsafe extern "C" fn write(c: *mut c_void, f: u8, v: u32) {
        let fake = &mut *c.cast::<Fake>();
        fake.fields[usize::from(f)] = v;
    }
    unsafe extern "C" fn command(c: *mut c_void, id: u8, _a: i32, _b: i32) -> u32 {
        let f = &mut *c.cast::<Fake>();
        f.events.push(id);
        let pair = match id {
            x if x == Command::Lock as u8 => Some((0, true)),
            x if x == Command::Unlock as u8 => Some((0, false)),
            x if x == Command::ProgressBegin as u8 => Some((1, true)),
            x if x == Command::ProgressEnd as u8 => Some((1, false)),
            x if x == Command::TextBegin as u8 => Some((2, true)),
            x if x == Command::TextEnd as u8 => Some((2, false)),
            x if x == Command::EntryBegin as u8 => Some((3, true)),
            x if x == Command::EntryEnd as u8 => Some((3, false)),
            _ => None,
        };
        if let Some((index, value)) = pair {
            assert_ne!(f.live[index], value);
            f.live[index] = value;
        }
        if id == Command::ProgressBegin as u8 {
            assert!(!f.live[0]);
        }
        if id == Command::Save as u8 || id == Command::Update as u8 {
            assert_eq!(f.live, [false, true, false, false]);
        }
        if id == Command::EntryInsert as u8 {
            assert_eq!(f.live, [false, true, true, true]);
        }
        match id {
            x if x == Command::Estimate as u8 => 7,
            x if x == Command::EraseMatches as u8 => (f.bits >> 2) & 1,
            x if x == Command::OffsetRead as u8 => (f.bits >> 3) & 1,
            x if x == Command::Save as u8 => (f.bits >> 4) & 1,
            x if x == Command::AnyMatch as u8 => (f.bits >> 5) & 1,
            _ => 0,
        }
    }
    #[test]
    fn every_bookmark_exit_preserves_temporary_scopes() {
        for bits in 0..256 {
            for page in [-1, 0, 2, 7] {
                for operation in 0..3 {
                    let mut f = Fake {
                        bits,
                        ..Fake::default()
                    };
                    f.fields[Field::HasSection as usize] = bits & 1;
                    f.fields[Field::HasBook as usize] = (bits >> 1) & 1;
                    f.fields[Field::Page as usize] = page as u32;
                    f.fields[Field::SectionCount as usize] = 5;
                    f.fields[Field::BookmarkCount as usize] = (bits >> 6) & 1;
                    f.fields[Field::HasCurrentOffset as usize] = (bits >> 7) & 1;
                    let s = Services {
                        context: (&mut f as *mut Fake).cast(),
                        read: Some(read),
                        write: Some(write),
                        command: Some(command),
                    };
                    unsafe { run(&s, operation) };
                    assert_eq!(f.live, [false; 4]);
                }
            }
        }
    }
    unsafe extern "C" fn link_query(c: *mut c_void, id: u8, index: u32) -> u32 {
        let f = &mut *c.cast::<Fake>();
        match id {
            x if x == LinkQuery::Depth as u8 => f.bits & 3,
            x if x == LinkQuery::SpineCount as u8 => 3,
            _ => index,
        }
    }
    unsafe extern "C" fn link_command(c: *mut c_void, id: u8, _a: i32, _b: i32) -> u32 {
        let f = &mut *c.cast::<Fake>();
        f.events.push(id);
        match id {
            x if x == LinkCommand::PathBegin as u8 => {
                assert!(!f.path);
                f.path = true;
            }
            x if x == LinkCommand::PathEnd as u8 => {
                assert!(f.path);
                assert!(!f.file);
                f.path = false;
            }
            x if x == LinkCommand::FileBegin as u8 => {
                assert!(!f.file);
                f.file = true;
            }
            x if x == LinkCommand::FileEnd as u8 => {
                assert!(f.file);
                f.file = false;
            }
            x if x == LinkCommand::Remove as u8 => {
                assert!(f.path);
                assert!(!f.file);
            }
            x if x == LinkCommand::Exists as u8 => return (f.bits >> 2) & 1,
            x if x == LinkCommand::OpenRead as u8 || x == LinkCommand::OpenWrite as u8 => {
                return (f.bits >> 3) & 1
            }
            _ => {}
        }
        0
    }
    unsafe extern "C" fn transfer(c: *mut c_void, write: u8, bytes: *mut u8, size: u32) -> i32 {
        let f = &mut *c.cast::<Fake>();
        assert!(f.file);
        assert!(size <= 13);
        if write == 0 {
            core::ptr::copy_nonoverlapping(f.bytes.as_ptr(), bytes, size as usize);
        }
        f.length
    }
    #[test]
    fn link_file_is_released_before_consume_on_every_path() {
        for bits in 0..16 {
            for length in -1..=14 {
                for depth in [0, 1, 2, 3, 4, 255] {
                    for load in [0, 1] {
                        let mut f = Fake {
                            bits,
                            length,
                            ..Fake::default()
                        };
                        f.bytes[0] = depth;
                        let s = LinkServices {
                            context: (&mut f as *mut Fake).cast(),
                            query: Some(link_query),
                            command: Some(link_command),
                            transfer: Some(transfer),
                        };
                        unsafe { link_run(&s, load) };
                        assert!(!f.file && !f.path);
                    }
                }
            }
        }
    }
    #[test]
    fn invalid_services_are_noops() {
        let mut f = Fake::default();
        let c = (&mut f as *mut Fake).cast();
        let s = Services {
            context: c,
            read: Some(read),
            write: Some(write),
            command: Some(command),
        };
        let l = LinkServices {
            context: c,
            query: Some(link_query),
            command: Some(link_command),
            transfer: Some(transfer),
        };
        unsafe {
            run(core::ptr::null(), 0);
            run(&s, 255);
            run(&Services { read: None, ..s }, 0);
            run(&Services { write: None, ..s }, 0);
            run(&Services { command: None, ..s }, 0);
            link_run(core::ptr::null(), 0);
            link_run(&l, 255);
            link_run(&LinkServices { query: None, ..l }, 0);
            link_run(&LinkServices { command: None, ..l }, 0);
            link_run(
                &LinkServices {
                    transfer: None,
                    ..l
                },
                0,
            );
        }
        assert!(f.events.is_empty());
    }
}
