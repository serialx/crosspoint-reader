//! Reader child selection, retained results, and sync resource policy.
use super::services::{Core, Guard};
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Dictionary = 0,
    Selection = 1,
    SelectionResult = 2,
    LookupResult = 3,
    Footnote = 4,
    FootnoteResult = 5,
    Clippings = 6,
    ClippingsResult = 7,
    Sync = 8,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    DictionaryEmpty = 0,
    HasSection = 1,
    HasBook = 2,
    Page = 3,
    PageCount = 4,
    Spine = 5,
    BuildWidth = 6,
    BuildHeight = 7,
    Window = 8,
    HasClippings = 9,
    ScreenMargin = 10,
    Cancelled = 11,
    ResultAction = 12,
    StartPageOffset = 13,
    EndPageOffset = 14,
    CapturedSpine = 15,
    CapturedPage = 16,
    CapturedPageCount = 17,
    HasProgressResult = 18,
    FootnoteCount = 19,
    HasCredentials = 20,
    NextPage = 21,
    CachedPageCount = 22,
    FootnoteDepth = 23,
    ShowDictionary = 24,
    DictionaryTime = 25,
    ShowBookmark = 26,
    BookmarkTime = 27,
    ShowClipping = 28,
    ClippingTime = 29,
    ClippingMessage = 30,
    PendingSyncError = 31,
    StripsEnabled = 32,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    Clock = 0,
    Update = 1,
    Lock = 2,
    Unlock = 3,
    ReleaseStrips = 4,
    PagesBegin = 5,
    PagesEnd = 6,
    ReservePages = 7,
    PageLoad = 8,
    PageEnd = 9,
    AppendPage = 10,
    Hit = 11,
    RemoveClipping = 12,
    LogRemoveFailed = 13,
    LogNoPage = 14,
    LogNoViewport = 15,
    Signature = 16,
    Margins = 17,
    TocIndex = 18,
    ChapterBegin = 19,
    ChapterEnd = 20,
    TitleBegin = 21,
    TitleEnd = 22,
    AuthorBegin = 23,
    AuthorEnd = 24,
    NewSelection = 25,
    LaunchSelection = 26,
    DropActivity = 27,
    LogSelectionOom = 28,
    ReadVisibleOffset = 29,
    AddBookmark = 30,
    LookupPage = 31,
    NewLookup = 32,
    LaunchLookup = 33,
    LogLookupPage = 34,
    LogLookupOom = 35,
    ParagraphIndex = 36,
    AddClipping = 37,
    ExportClipping = 38,
    HrefBegin = 39,
    HrefEnd = 40,
    CopyFirstHref = 41,
    NavigateHref = 42,
    NewFootnote = 43,
    LaunchFootnote = 44,
    LogFootnoteOom = 45,
    OpenMenu = 46,
    NavigateResultHref = 47,
    NewClippings = 48,
    LaunchClippings = 49,
    LogClippingsOom = 50,
    ApplyResultProgress = 51,
    FlushSession = 52,
    LaunchEventsOnly = 53,
    Estimate = 54,
    PositionBegin = 55,
    PositionEnd = 56,
    SavedPositionBegin = 57,
    SavedPositionEnd = 58,
    PathBegin = 59,
    PathEnd = 60,
    SaveProgress = 61,
    LogSyncSaveFailed = 62,
    LogSyncBefore = 63,
    DiscardOverlay = 64,
    ReleaseImages = 65,
    ClearExtractor = 66,
    DropSection = 67,
    CacheBegin = 68,
    ReleaseCache = 69,
    CacheEnd = 70,
    LoanBegin = 71,
    LoanEnd = 72,
    MapProgress = 73,
    SaveLinks = 74,
    DropBook = 75,
    LogSyncAfter = 76,
    LaunchSync = 77,
    Dictionary = 78,
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
    fn set(&self, f: Field, v: u32) {
        self.0.write(f as u8, v);
    }
    fn c(&self, c: Command, a: i32, b: i32) -> u32 {
        self.0.command(c as u8, a, b)
    }
    fn op(&self, c: Command) -> u32 {
        self.0.op(c as u8)
    }
    fn guard(&self, c: Command) -> Guard<'_> {
        self.0.guard(c as u8)
    }
    fn lock(&self) -> Guard<'_> {
        self.op(Command::Lock);
        self.guard(Command::Unlock)
    }
    fn dictionary(&self) {
        if self.f(Field::DictionaryEmpty) != 0 {
            self.set(Field::ShowDictionary, 1);
            self.set(Field::DictionaryTime, self.op(Command::Clock));
            self.op(Command::Update);
        } else {
            self.selection(-1, -1);
        }
    }
    fn notice(&self, message: u32) {
        self.set(Field::ClippingMessage, message);
        self.set(Field::ShowClipping, 1);
        self.set(Field::ClippingTime, self.op(Command::Clock));
        self.op(Command::Update);
    }
    fn selection(&self, x: i32, y: i32) {
        if self.f(Field::HasSection) == 0
            || self.f(Field::HasBook) == 0
            || (self.f(Field::Page) as i32) < 0
            || self.f(Field::Page) >= self.f(Field::PageCount)
        {
            return;
        }
        if self.f(Field::StripsEnabled) != 0 {
            let _lock = self.lock();
            self.op(Command::ReleaseStrips);
        }
        let page = self.f(Field::Page) as i32;
        let count = self.f(Field::PageCount) as u16;
        let spine = self.f(Field::Spine) as u16;
        self.set(Field::CapturedPage, page as u32);
        self.set(Field::CapturedPageCount, u32::from(count));
        self.set(Field::CapturedSpine, u32::from(spine));
        self.op(Command::PagesBegin);
        let _pages = self.guard(Command::PagesEnd);
        self.c(Command::ReservePages, self.f(Field::Window) as i32, 0);
        let mut offset = 0;
        while offset < (self.f(Field::Window) as i32)
            && page.wrapping_add(offset) < i32::from(count)
        {
            let loaded = self.c(Command::PageLoad, page.wrapping_add(offset), 0) != 0;
            let _page = self.guard(Command::PageEnd);
            if !loaded {
                break;
            }
            if offset == 0 && x >= 0 && self.f(Field::HasClippings) != 0 {
                let _lock = self.lock();
                let index = self.c(Command::Hit, x, y) as i32;
                if index >= 0 {
                    let removed = self.c(Command::RemoveClipping, index, 0) != 0;
                    if !removed {
                        self.c(Command::LogRemoveFailed, index, 0);
                    }
                    self.notice(u32::from(!removed));
                    return;
                }
            }
            self.op(Command::AppendPage);
            offset += 1;
        }
        if offset == 0 {
            self.c(Command::LogNoPage, page, 0);
            self.op(Command::Update);
            return;
        }
        if self.f(Field::BuildWidth) == 0 || self.f(Field::BuildHeight) == 0 {
            self.op(Command::LogNoViewport);
            self.op(Command::Update);
            return;
        }
        self.op(Command::Signature);
        self.op(Command::Margins);
        let toc = self.op(Command::TocIndex) as i32;
        self.c(Command::ChapterBegin, toc, 0);
        let _chapter = self.guard(Command::ChapterEnd);
        self.op(Command::TitleBegin);
        let _title = self.guard(Command::TitleEnd);
        self.op(Command::AuthorBegin);
        let _author = self.guard(Command::AuthorEnd);
        let ok = self.c(Command::NewSelection, x, y) != 0;
        let _activity = self.guard(Command::DropActivity);
        if !ok {
            self.op(Command::LogSelectionOom);
            self.op(Command::Update);
            return;
        }
        self.op(Command::LaunchSelection);
    }
    fn selection_result(&self) {
        if self.f(Field::Cancelled) != 0 {
            self.op(Command::Update);
            return;
        }
        let start = (self.f(Field::CapturedPage) as u16)
            .wrapping_add(self.f(Field::StartPageOffset) as u16);
        let end =
            (self.f(Field::CapturedPage) as u16).wrapping_add(self.f(Field::EndPageOffset) as u16);
        if self.f(Field::HasSection) != 0
            && self.f(Field::Spine) == self.f(Field::CapturedSpine)
            && u32::from(end) < self.f(Field::PageCount)
        {
            self.set(Field::Page, u32::from(end));
            self.c(Command::ReadVisibleOffset, i32::from(end), 0);
        }
        match self.f(Field::ResultAction) {
            2 => {
                self.op(Command::AddBookmark);
                self.set(Field::ShowBookmark, 1);
                self.set(Field::BookmarkTime, self.op(Command::Clock));
                self.op(Command::Update);
            }
            1 => {
                let ok = self.op(Command::LookupPage) != 0;
                let _page = self.guard(Command::PageEnd);
                if !ok {
                    self.op(Command::LogLookupPage);
                    self.op(Command::Update);
                    return;
                }
                self.op(Command::Margins);
                let ok = self.op(Command::NewLookup) != 0;
                let _activity = self.guard(Command::DropActivity);
                if !ok {
                    self.op(Command::LogLookupOom);
                    self.op(Command::Update);
                    return;
                }
                self.op(Command::LaunchLookup);
            }
            _ => {
                if self.f(Field::HasSection) != 0 {
                    self.c(Command::ParagraphIndex, i32::from(start), 0);
                }
                let result = self.c(Command::AddClipping, i32::from(start), i32::from(end));
                let exported =
                    result == 0 && self.c(Command::ExportClipping, i32::from(start) + 1, 0) != 0;
                self.notice(if result == 1 {
                    2
                } else if exported {
                    3
                } else if result == 0 {
                    4
                } else {
                    5
                });
            }
        }
    }
    fn footnote(&self, reopen: i32) {
        self.op(Command::HrefBegin);
        let _href = self.guard(Command::HrefEnd);
        let _page = self.guard(Command::PageEnd);
        let single;
        let loaded;
        {
            let _lock = self.lock();
            if self.f(Field::HasSection) == 0 || self.f(Field::FootnoteCount) == 0 {
                return;
            }
            single = self.f(Field::FootnoteCount) == 1;
            if single {
                self.op(Command::CopyFirstHref);
                loaded = false;
            } else {
                loaded = self.c(Command::PageLoad, self.f(Field::Page) as i32, 0) != 0;
            }
        }
        if single {
            self.op(Command::NavigateHref);
            return;
        }
        if !loaded {
            return;
        }
        self.op(Command::Margins);
        let ok = self.op(Command::NewFootnote) != 0;
        let _activity = self.guard(Command::DropActivity);
        if !ok {
            self.op(Command::LogFootnoteOom);
            return;
        }
        self.c(Command::LaunchFootnote, reopen, 0);
    }
    fn footnote_result(&self, reopen: i32) {
        if self.f(Field::Cancelled) != 0 {
            self.op(if reopen != 0 {
                Command::OpenMenu
            } else {
                Command::Update
            });
            return;
        }
        self.op(Command::NavigateResultHref);
        self.op(Command::Update);
    }
    fn clippings(&self) {
        if self.f(Field::HasClippings) == 0 {
            self.op(Command::Update);
            return;
        }
        let ok = self.op(Command::NewClippings) != 0;
        let _activity = self.guard(Command::DropActivity);
        if !ok {
            self.op(Command::LogClippingsOom);
            return;
        }
        self.op(Command::LaunchClippings);
    }
    fn sync(&self) {
        self.op(Command::FlushSession);
        if self.f(Field::HasCredentials) == 0 {
            self.op(Command::LaunchEventsOnly);
            return;
        }
        let _lock = self.lock();
        let page = if self.f(Field::HasSection) != 0 {
            self.f(Field::Page)
        } else {
            self.f(Field::NextPage)
        };
        let count = if self.f(Field::HasSection) != 0 {
            self.op(Command::Estimate)
        } else {
            self.f(Field::CachedPageCount)
        };
        self.op(Command::PositionBegin);
        let _position = self.guard(Command::PositionEnd);
        self.op(Command::SavedPositionBegin);
        let _saved = self.guard(Command::SavedPositionEnd);
        let toc = self.op(Command::TocIndex) as i32;
        self.c(Command::ChapterBegin, toc, 0);
        let _chapter = self.guard(Command::ChapterEnd);
        self.op(Command::PathBegin);
        let _path = self.guard(Command::PathEnd);
        if self.c(Command::SaveProgress, page as i32, count as i32) == 0 {
            self.op(Command::LogSyncSaveFailed);
            self.set(Field::PendingSyncError, 1);
            self.op(Command::Update);
            return;
        }
        self.op(Command::LogSyncBefore);
        if self.f(Field::HasSection) != 0 {
            self.set(Field::NextPage, self.f(Field::Page));
        }
        self.op(Command::DiscardOverlay);
        self.op(Command::ReleaseImages);
        self.op(Command::ClearExtractor);
        self.op(Command::DropSection);
        {
            let present = self.op(Command::CacheBegin) != 0;
            let _cache = self.guard(Command::CacheEnd);
            if present {
                self.op(Command::ReleaseCache);
            }
        }
        {
            self.op(Command::LoanBegin);
            let _loan = self.guard(Command::LoanEnd);
            self.op(Command::MapProgress);
        }
        if self.f(Field::FootnoteDepth) > 0 {
            self.op(Command::SaveLinks);
        }
        self.op(Command::DropBook);
        self.op(Command::LogSyncAfter);
        self.op(Command::LaunchSync);
    }
}
/// # Safety
/// Native services/resources and result borrows stay live for this call only.
pub unsafe fn run(s: *const Services, operation: u8, a: i32, b: i32, c: u8) -> u8 {
    let Some(s) = s.as_ref() else {
        return 0;
    };
    let (Some(read), Some(write), Some(command)) = (s.read, s.write, s.command) else {
        return 0;
    };
    if operation == Operation::Selection as u8 && c > 2 {
        return 0;
    }
    let p = Policy(Core {
        context: s.context,
        read,
        write,
        command,
    });
    match operation {
        x if x == Operation::Dictionary as u8 => p.dictionary(),
        x if x == Operation::Selection as u8 => p.selection(a, b),
        x if x == Operation::SelectionResult as u8 => p.selection_result(),
        x if x == Operation::LookupResult as u8 => {
            if p.f(Field::Cancelled) == 0 {
                p.dictionary();
            }
        }
        x if x == Operation::Footnote as u8 => p.footnote(a),
        x if x == Operation::FootnoteResult as u8 => p.footnote_result(a),
        x if x == Operation::Clippings as u8 => p.clippings(),
        x if x == Operation::ClippingsResult as u8 => {
            if p.f(Field::Cancelled) == 0 && p.f(Field::HasProgressResult) != 0 {
                p.op(Command::ApplyResultProgress);
            } else {
                p.op(Command::Update);
            }
        }
        x if x == Operation::Sync as u8 => {
            p.sync();
            return 1;
        }
        _ => {}
    }
    0
}
const _: () = {
    assert!(core::mem::size_of::<Services>() == 4 * core::mem::size_of::<usize>());
    assert!(core::mem::align_of::<Services>() == core::mem::align_of::<usize>());
    assert!(Operation::Dictionary as u8 == 0);
    assert!(Operation::Selection as u8 == 1);
    assert!(Operation::SelectionResult as u8 == 2);
    assert!(Operation::LookupResult as u8 == 3);
    assert!(Operation::Footnote as u8 == 4);
    assert!(Operation::FootnoteResult as u8 == 5);
    assert!(Operation::Clippings as u8 == 6);
    assert!(Operation::ClippingsResult as u8 == 7);
    assert!(Operation::Sync as u8 == 8);
    assert!(Field::DictionaryEmpty as u8 == 0);
    assert!(Field::HasSection as u8 == 1);
    assert!(Field::HasBook as u8 == 2);
    assert!(Field::Page as u8 == 3);
    assert!(Field::PageCount as u8 == 4);
    assert!(Field::Spine as u8 == 5);
    assert!(Field::BuildWidth as u8 == 6);
    assert!(Field::BuildHeight as u8 == 7);
    assert!(Field::Window as u8 == 8);
    assert!(Field::HasClippings as u8 == 9);
    assert!(Field::ScreenMargin as u8 == 10);
    assert!(Field::Cancelled as u8 == 11);
    assert!(Field::ResultAction as u8 == 12);
    assert!(Field::StartPageOffset as u8 == 13);
    assert!(Field::EndPageOffset as u8 == 14);
    assert!(Field::CapturedSpine as u8 == 15);
    assert!(Field::CapturedPage as u8 == 16);
    assert!(Field::CapturedPageCount as u8 == 17);
    assert!(Field::HasProgressResult as u8 == 18);
    assert!(Field::FootnoteCount as u8 == 19);
    assert!(Field::HasCredentials as u8 == 20);
    assert!(Field::NextPage as u8 == 21);
    assert!(Field::CachedPageCount as u8 == 22);
    assert!(Field::FootnoteDepth as u8 == 23);
    assert!(Field::ShowDictionary as u8 == 24);
    assert!(Field::DictionaryTime as u8 == 25);
    assert!(Field::ShowBookmark as u8 == 26);
    assert!(Field::BookmarkTime as u8 == 27);
    assert!(Field::ShowClipping as u8 == 28);
    assert!(Field::ClippingTime as u8 == 29);
    assert!(Field::ClippingMessage as u8 == 30);
    assert!(Field::PendingSyncError as u8 == 31);
    assert!(Field::StripsEnabled as u8 == 32);
    assert!(Command::Clock as u8 == 0);
    assert!(Command::Update as u8 == 1);
    assert!(Command::Lock as u8 == 2);
    assert!(Command::Unlock as u8 == 3);
    assert!(Command::ReleaseStrips as u8 == 4);
    assert!(Command::PagesBegin as u8 == 5);
    assert!(Command::PagesEnd as u8 == 6);
    assert!(Command::ReservePages as u8 == 7);
    assert!(Command::PageLoad as u8 == 8);
    assert!(Command::PageEnd as u8 == 9);
    assert!(Command::AppendPage as u8 == 10);
    assert!(Command::Hit as u8 == 11);
    assert!(Command::RemoveClipping as u8 == 12);
    assert!(Command::LogRemoveFailed as u8 == 13);
    assert!(Command::LogNoPage as u8 == 14);
    assert!(Command::LogNoViewport as u8 == 15);
    assert!(Command::Signature as u8 == 16);
    assert!(Command::Margins as u8 == 17);
    assert!(Command::TocIndex as u8 == 18);
    assert!(Command::ChapterBegin as u8 == 19);
    assert!(Command::ChapterEnd as u8 == 20);
    assert!(Command::TitleBegin as u8 == 21);
    assert!(Command::TitleEnd as u8 == 22);
    assert!(Command::AuthorBegin as u8 == 23);
    assert!(Command::AuthorEnd as u8 == 24);
    assert!(Command::NewSelection as u8 == 25);
    assert!(Command::LaunchSelection as u8 == 26);
    assert!(Command::DropActivity as u8 == 27);
    assert!(Command::LogSelectionOom as u8 == 28);
    assert!(Command::ReadVisibleOffset as u8 == 29);
    assert!(Command::AddBookmark as u8 == 30);
    assert!(Command::LookupPage as u8 == 31);
    assert!(Command::NewLookup as u8 == 32);
    assert!(Command::LaunchLookup as u8 == 33);
    assert!(Command::LogLookupPage as u8 == 34);
    assert!(Command::LogLookupOom as u8 == 35);
    assert!(Command::ParagraphIndex as u8 == 36);
    assert!(Command::AddClipping as u8 == 37);
    assert!(Command::ExportClipping as u8 == 38);
    assert!(Command::HrefBegin as u8 == 39);
    assert!(Command::HrefEnd as u8 == 40);
    assert!(Command::CopyFirstHref as u8 == 41);
    assert!(Command::NavigateHref as u8 == 42);
    assert!(Command::NewFootnote as u8 == 43);
    assert!(Command::LaunchFootnote as u8 == 44);
    assert!(Command::LogFootnoteOom as u8 == 45);
    assert!(Command::OpenMenu as u8 == 46);
    assert!(Command::NavigateResultHref as u8 == 47);
    assert!(Command::NewClippings as u8 == 48);
    assert!(Command::LaunchClippings as u8 == 49);
    assert!(Command::LogClippingsOom as u8 == 50);
    assert!(Command::ApplyResultProgress as u8 == 51);
    assert!(Command::FlushSession as u8 == 52);
    assert!(Command::LaunchEventsOnly as u8 == 53);
    assert!(Command::Estimate as u8 == 54);
    assert!(Command::PositionBegin as u8 == 55);
    assert!(Command::PositionEnd as u8 == 56);
    assert!(Command::SavedPositionBegin as u8 == 57);
    assert!(Command::SavedPositionEnd as u8 == 58);
    assert!(Command::PathBegin as u8 == 59);
    assert!(Command::PathEnd as u8 == 60);
    assert!(Command::SaveProgress as u8 == 61);
    assert!(Command::LogSyncSaveFailed as u8 == 62);
    assert!(Command::LogSyncBefore as u8 == 63);
    assert!(Command::DiscardOverlay as u8 == 64);
    assert!(Command::ReleaseImages as u8 == 65);
    assert!(Command::ClearExtractor as u8 == 66);
    assert!(Command::DropSection as u8 == 67);
    assert!(Command::CacheBegin as u8 == 68);
    assert!(Command::ReleaseCache as u8 == 69);
    assert!(Command::CacheEnd as u8 == 70);
    assert!(Command::LoanBegin as u8 == 71);
    assert!(Command::LoanEnd as u8 == 72);
    assert!(Command::MapProgress as u8 == 73);
    assert!(Command::SaveLinks as u8 == 74);
    assert!(Command::DropBook as u8 == 75);
    assert!(Command::LogSyncAfter as u8 == 76);
    assert!(Command::LaunchSync as u8 == 77);
    assert!(Command::Dictionary as u8 == 78);
    assert!(core::mem::offset_of!(Services, context) == 0);
    assert!(core::mem::offset_of!(Services, read) == core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(Services, write) == 2 * core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(Services, command) == 3 * core::mem::size_of::<usize>());
};

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::vec::Vec;
    struct Fake {
        fields: [u32; 33],
        bits: u32,
        events: Vec<u8>,
        scopes: Vec<u8>,
        page_calls: u32,
        activity: bool,
        launched: bool,
    }
    impl Fake {
        fn new(bits: u32) -> Self {
            let mut f = Self {
                fields: [0; 33],
                bits,
                events: Vec::new(),
                scopes: Vec::new(),
                page_calls: 0,
                activity: false,
                launched: false,
            };
            for field in [
                Field::HasSection,
                Field::HasBook,
                Field::HasClippings,
                Field::HasCredentials,
            ] {
                f.fields[field as usize] = 1;
            }
            f.fields[Field::Page as usize] = 2;
            f.fields[Field::PageCount as usize] = 6;
            f.fields[Field::Spine as usize] = 2;
            f.fields[Field::BuildWidth as usize] = 80;
            f.fields[Field::BuildHeight as usize] = 100;
            f.fields[Field::Window as usize] = 3;
            f.fields[Field::FootnoteDepth as usize] = 2;
            f.fields[Field::CapturedSpine as usize] = 2;
            f.fields[Field::CapturedPage as usize] = 2;
            f.fields[Field::CapturedPageCount as usize] = 6;
            f.fields[Field::FootnoteCount as usize] = bits % 3;
            f.fields[Field::ResultAction as usize] = (bits >> 2) % 3;
            f.fields[Field::EndPageOffset as usize] = if bits & 8 != 0 { 4 } else { 1 };
            for (field, bit) in [
                (Field::DictionaryEmpty, 16),
                (Field::Cancelled, 32),
                (Field::HasProgressResult, 64),
                (Field::StripsEnabled, 128),
            ] {
                f.fields[field as usize] = u32::from(bits & bit != 0);
            }
            if bits & 256 != 0 {
                f.fields[Field::HasSection as usize] = 0;
            }
            if bits & 512 != 0 {
                f.fields[Field::HasBook as usize] = 0;
            }
            if bits & 1024 != 0 {
                f.fields[Field::BuildWidth as usize] = 0;
            }
            if bits & 2048 != 0 {
                f.fields[Field::HasCredentials as usize] = 0;
            }
            f
        }
        fn services(&mut self) -> Services {
            Services {
                context: (self as *mut Self).cast(),
                read: Some(read),
                write: Some(write),
                command: Some(command),
            }
        }
    }
    unsafe extern "C" fn read(c: *mut c_void, f: u8) -> u32 {
        (&*c.cast::<Fake>()).fields[usize::from(f)]
    }
    unsafe extern "C" fn write(c: *mut c_void, f: u8, v: u32) {
        (&mut *c.cast::<Fake>()).fields[usize::from(f)] = v;
    }
    fn release(id: u8) -> Option<Command> {
        [
            (Command::Lock, Command::Unlock),
            (Command::PagesBegin, Command::PagesEnd),
            (Command::ChapterBegin, Command::ChapterEnd),
            (Command::TitleBegin, Command::TitleEnd),
            (Command::AuthorBegin, Command::AuthorEnd),
            (Command::HrefBegin, Command::HrefEnd),
            (Command::PositionBegin, Command::PositionEnd),
            (Command::SavedPositionBegin, Command::SavedPositionEnd),
            (Command::PathBegin, Command::PathEnd),
            (Command::CacheBegin, Command::CacheEnd),
            (Command::LoanBegin, Command::LoanEnd),
        ]
        .iter()
        .find_map(|(a, b)| if *a as u8 == id { Some(*b) } else { None })
    }
    unsafe extern "C" fn command(c: *mut c_void, id: u8, _a: i32, _b: i32) -> u32 {
        let f = &mut *c.cast::<Fake>();
        f.events.push(id);
        if let Some(end) = release(id) {
            f.scopes.push(end as u8);
        } else if [
            Command::Unlock,
            Command::PagesEnd,
            Command::ChapterEnd,
            Command::TitleEnd,
            Command::AuthorEnd,
            Command::HrefEnd,
            Command::PositionEnd,
            Command::SavedPositionEnd,
            Command::PathEnd,
            Command::CacheEnd,
            Command::LoanEnd,
        ]
        .iter()
        .any(|v| *v as u8 == id)
        {
            assert_eq!(
                f.scopes.pop(),
                Some(id),
                "resource releases must be in original reverse scope order"
            );
        }
        if [
            Command::NewSelection,
            Command::NewLookup,
            Command::NewFootnote,
            Command::NewClippings,
        ]
        .iter()
        .any(|v| *v as u8 == id)
        {
            assert!(!f.activity);
            f.activity = true;
            f.launched = false;
            return u32::from(f.bits & 4 == 0);
        }
        if [
            Command::LaunchSelection,
            Command::LaunchLookup,
            Command::LaunchFootnote,
            Command::LaunchClippings,
        ]
        .iter()
        .any(|v| *v as u8 == id)
        {
            assert!(f.activity && !f.launched && f.bits & 4 == 0);
            f.launched = true;
        }
        if id == Command::DropActivity as u8 {
            assert!(f.activity);
            f.activity = false;
        }
        match id {
            x if x == Command::PageLoad as u8 || x == Command::LookupPage as u8 => {
                f.page_calls += 1;
                u32::from(f.page_calls <= (f.bits & 3))
            }
            x if x == Command::Hit as u8 => {
                if f.bits & 64 != 0 {
                    1
                } else {
                    u32::MAX
                }
            }
            x if x == Command::RemoveClipping as u8
                || x == Command::SaveProgress as u8
                || x == Command::ExportClipping as u8 =>
            {
                u32::from(f.bits & 8 == 0)
            }
            x if x == Command::AddClipping as u8 => f.bits % 3,
            x if x == Command::TocIndex as u8 => {
                if f.bits & 16 != 0 {
                    u32::MAX
                } else {
                    1
                }
            }
            x if x == Command::CacheBegin as u8 => u32::from(f.bits & 64 != 0),
            x if x == Command::DropSection as u8 => {
                f.fields[Field::HasSection as usize] = 0;
                0
            }
            x if x == Command::DropBook as u8 => {
                f.fields[Field::HasBook as usize] = 0;
                0
            }
            x if x == Command::Clock as u8 => 123456,
            x if x == Command::Estimate as u8 => 9,
            _ => 0,
        }
    }
    #[test]
    fn all_child_resource_scopes_unwind_in_original_order() {
        let mut coverage = [false; 79];
        for bits in 0..4096 {
            for operation in 0..9 {
                for x in [-1, 10] {
                    let mut f = Fake::new(bits);
                    unsafe {
                        run(&f.services(), operation, x, 20, 1);
                    }
                    assert!(f.scopes.is_empty() && !f.activity);
                    let count = |c: Command| f.events.iter().filter(|v| **v == c as u8).count();
                    let loads = count(Command::PageLoad) + count(Command::LookupPage);
                    assert_eq!(
                        count(Command::PageEnd),
                        if operation == Operation::Footnote as u8 {
                            1
                        } else {
                            loads
                        }
                    );
                    for id in &f.events {
                        coverage[usize::from(*id)] = true;
                    }
                }
            }
        }
        // Dictionary recursively enters the policy; the ABI ID remains reserved.
        for (i, seen) in coverage.iter().enumerate() {
            assert!(
                *seen || i == Command::Dictionary as usize,
                "command {i} missing"
            );
        }
    }
    #[test]
    fn sync_returns_with_original_lock_and_temporary_destruction_order() {
        for fail in [false, true] {
            let mut f = Fake::new(if fail { 8 } else { 64 });
            unsafe {
                assert_eq!(run(&f.services(), Operation::Sync as u8, 0, 0, 0), 1);
            }
            let tail = [
                Command::PathEnd,
                Command::ChapterEnd,
                Command::SavedPositionEnd,
                Command::PositionEnd,
                Command::Unlock,
            ]
            .map(|v| v as u8);
            assert!(f.events.ends_with(&tail));
            if !fail {
                let launch = f
                    .events
                    .iter()
                    .position(|v| *v == Command::LaunchSync as u8)
                    .unwrap();
                assert_eq!(&f.events[launch + 1..], &tail);
                let loan_end = f
                    .events
                    .iter()
                    .position(|v| *v == Command::LoanEnd as u8)
                    .unwrap();
                let links = f
                    .events
                    .iter()
                    .position(|v| *v == Command::SaveLinks as u8)
                    .unwrap();
                assert!(loan_end < links && links < launch);
            }
        }
    }
    #[test]
    fn invalid_child_abi_has_no_effects() {
        let mut f = Fake::new(0);
        let valid = f.services();
        unsafe {
            assert_eq!(run(core::ptr::null(), 0, 0, 0, 0), 0);
        }
        for s in [
            Services {
                read: None,
                ..valid
            },
            Services {
                write: None,
                ..valid
            },
            Services {
                command: None,
                ..valid
            },
        ] {
            unsafe {
                assert_eq!(run(&s, 0, 0, 0, 0), 0);
            }
        }
        unsafe {
            assert_eq!(run(&valid, 255, 0, 0, 0), 0);
            assert_eq!(run(&valid, Operation::Selection as u8, 0, 0, 255), 0);
        }
        assert!(f.events.is_empty());
    }
}
