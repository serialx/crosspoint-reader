//! Chapter build and page-restore sequencing over borrowed native resources.
use super::services::{Core, Guard};
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    Spine = 0,
    CachedSpine = 1,
    CachedCount = 2,
    HasCachedOffset = 3,
    CachedOffset = 4,
    HasPendingPage = 5,
    PendingPage = 6,
    NextPage = 7,
    HasPendingOffset = 8,
    PendingOffset = 9,
    PendingPercent = 10,
    SpineProgress = 11,
    Automatic = 12,
    SyncError = 13,
    PartialStartFailed = 14,
    BuildPopup = 15,
    BuildWidth = 16,
    BuildHeight = 17,
    PageStale = 18,
    Retry = 19,
    CurrentOffset = 20,
    MarginLeft = 21,
    MarginTop = 22,
    LastRender = 23,
    LastSavedSpine = 24,
    LastSavedPage = 25,
    LastSavedCount = 26,
    Screenshot = 27,
    BookmarkNotice = 28,
    BookmarkRemoved = 29,
    DictionaryNotice = 30,
    FontNotice = 31,
    FontNoticeMs = 32,
    ClippingNotice = 33,
    Overlay = 34,
    Stored = 35,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    HasBook = 0,
    SpineCount = 1,
    HasSection = 2,
    MarginTop = 3,
    MarginRight = 4,
    MarginBottom = 5,
    MarginLeft = 6,
    ScreenMargin = 7,
    StatusHeight = 8,
    ProgressHeight = 9,
    StatusMargin = 10,
    Width = 11,
    Height = 12,
    Partial = 13,
    Building = 14,
    Complete = 15,
    PageCount = 16,
    Page = 17,
    HasAnchor = 18,
    HasHtml = 19,
    FindAnchor = 20,
    PageForAnchor = 21,
    HasTtf = 22,
    Now = 24,
    PopupActive = 25,
    UsesToolbar = 26,
    HasPage = 28,
    PageOffset = 29,
    EstimatedCount = 30,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    ClearMetadata = 0,
    RadioSettle = 1,
    SettleOverlay = 2,
    SampleMargins = 3,
    SetMargin = 4,
    SetSpec = 5,
    GetPath = 6,
    DropPath = 7,
    LogLoading = 8,
    NewSection = 9,
    LoadCache = 10,
    LogPartial = 11,
    LogNoCache = 12,
    Counter = 13,
    Room = 14,
    DropSection = 15,
    LoanBegin = 16,
    LoanEnd = 17,
    LoanDrop = 18,
    CreateAll = 19,
    LogPersistFailed = 20,
    LogDeferred = 21,
    Cumulative = 22,
    ReleaseFontCache = 23,
    LogHeap = 24,
    StartWithPopup = 25,
    LogStartFailed = 26,
    ReachedOffset = 27,
    ShowBuildPopup = 28,
    BuildMore = 29,
    LogStepFailed = 30,
    LogCacheFound = 31,
    Page = 32,
    MapOffset = 33,
    ClearDeferred = 34,
    LogAnchor = 35,
    ClearAnchor = 36,
    StartPartial = 37,
    LogPartialStartFailed = 38,
    ApplyDeferred = 39,
    ClearScreen = 40,
    LogEmpty = 41,
    StatusBar = 42,
    Display = 43,
    LogBounds = 44,
    BookmarkFlag = 45,
    LoadPage = 46,
    DropPage = 47,
    AssignMetadata = 48,
    LogPageLoadFailed = 49,
    LogMetadataFailed = 50,
    AbandonBuild = 51,
    ClearCache = 52,
    LogRetryFailed = 53,
    Update = 54,
    DiscardOverlay = 55,
    RenderContents = 56,
    LogRendered = 57,
    MarkRendered = 58,
    SaveProgress = 59,
    Screenshot = 60,
    Store = 61,
    RenderOverlay = 62,
    RenderPopup = 63,
    PushRefresh = 64,
    BackHints = 65,
    PopupSync = 66,
    PopupRadioError = 67,
    PopupBuildError = 68,
    PopupIndex = 69,
    PopupBookmarkRemoved = 70,
    PopupBookmarkAdded = 71,
    PopupDictionary = 72,
    PopupFont = 73,
    PopupClipping = 74,
    DrawEmpty = 75,
    DrawBounds = 76,
    DrawLoadError = 77,
    SampleFontCache = 78,
    TakeFontNotice = 79,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub write: Option<unsafe extern "C" fn(*mut c_void, u8, u32)>,
    pub query: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
}
struct Engine {
    core: Core,
    query: unsafe extern "C" fn(*mut c_void, u8) -> u32,
}

impl Engine {
    fn f(&self, field: Field) -> u32 {
        self.core.read(field as u8)
    }
    fn set(&self, field: Field, value: u32) {
        self.core.write(field as u8, value);
    }
    fn q(&self, q: Query) -> u32 {
        unsafe { (self.query)(self.core.context, q as u8) }
    }
    fn c(&self, command: Command, a: i32, b: i32) -> u32 {
        self.core.command(command as u8, a, b)
    }
    fn op(&self, command: Command) -> u32 {
        self.core.op(command as u8)
    }
    fn resource(&self, acquire: Command, release: Command) -> Guard<'_> {
        self.op(acquire);
        self.core.guard(release as u8)
    }
    fn sync_error(&self) {
        if self.f(Field::SyncError) != 0 {
            self.set(Field::SyncError, 0);
            self.op(Command::PopupSync);
        }
    }
    fn error(&self, radio: bool) {
        self.op(Command::ClearScreen);
        self.op(Command::BackHints);
        self.op(if radio {
            Command::PopupRadioError
        } else {
            Command::PopupBuildError
        });
        self.set(Field::Automatic, 0);
    }
    fn viewport(&self) {
        self.op(Command::SampleMargins);
        let margin = self.q(Query::ScreenMargin) as i32;
        let top = (self.q(Query::MarginTop) as i32).wrapping_add(margin);
        let right = (self.q(Query::MarginRight) as i32).wrapping_add(margin);
        let left = (self.q(Query::MarginLeft) as i32).wrapping_add(margin);
        let status = self.q(Query::StatusHeight) as u8;
        let extra = if self.f(Field::Automatic) != 0
            && (status == 0 || u32::from(status) == self.q(Query::ProgressHeight))
        {
            margin.max(i32::from(
                status.wrapping_add(self.q(Query::StatusMargin) as u8),
            ))
        } else {
            margin.max(i32::from(status))
        };
        let bottom = (self.q(Query::MarginBottom) as i32).wrapping_add(extra);
        for (index, value) in [top, right, bottom, left].into_iter().enumerate() {
            self.c(Command::SetMargin, index as i32, value);
        }
        self.set(
            Field::BuildWidth,
            (self.q(Query::Width) as i32)
                .wrapping_sub(left)
                .wrapping_sub(right) as u16 as u32,
        );
        self.set(
            Field::BuildHeight,
            (self.q(Query::Height) as i32)
                .wrapping_sub(top)
                .wrapping_sub(bottom) as u16 as u32,
        );
        self.op(Command::SetSpec);
    }
    fn initial_section(&self) -> bool {
        let _path = self.resource(Command::GetPath, Command::DropPath);
        self.op(Command::LogLoading);
        self.op(Command::NewSection);
        self.set(Field::PartialStartFailed, 0);
        let cached = self.op(Command::LoadCache) != 0;
        if cached {
            self.set(Field::CachedCount, 0);
            self.set(Field::HasCachedOffset, 0);
        }
        let complete = cached && self.q(Query::Partial) == 0;
        let explicit_offset = self.f(Field::HasPendingOffset) != 0;
        let offset = if explicit_offset {
            Some(self.f(Field::PendingOffset))
        } else if self.f(Field::HasPendingPage) != 0
            || self.q(Query::HasAnchor) != 0
            || self.f(Field::Spine) != self.f(Field::CachedSpine)
        {
            None
        } else if self.f(Field::HasCachedOffset) != 0 {
            Some(self.f(Field::CachedOffset))
        } else {
            None
        };
        if !complete {
            self.op(if self.q(Query::Partial) != 0 {
                Command::LogPartial
            } else {
                Command::LogNoCache
            });
            if self.f(Field::PendingPercent) != 0 {
                self.op(Command::PopupIndex);
                self.c(Command::Counter, 1, 0);
                if self.op(Command::Room) == 0 {
                    self.op(Command::DropSection);
                    self.error(true);
                    return false;
                }
                let _loan = self.resource(Command::LoanBegin, Command::LoanDrop);
                if self.op(Command::CreateAll) == 0 {
                    self.op(Command::LogPersistFailed);
                    self.op(Command::DropSection);
                    self.op(Command::LoanEnd);
                    self.error(false);
                    return false;
                }
                self.op(Command::LoanEnd);
            } else {
                let target = if self.f(Field::HasPendingPage) != 0 {
                    self.f(Field::PendingPage) as i32
                } else {
                    (self.f(Field::NextPage) as i32).max(0)
                };
                let anchor = self.q(Query::HasAnchor) != 0;
                let covered = self.q(Query::Partial) != 0
                    && if anchor {
                        self.q(Query::PageForAnchor) & 0x10000 != 0
                    } else {
                        target.wrapping_add(15) < self.q(Query::PageCount) as i32
                    };
                if covered {
                    self.c(Command::LogDeferred, target, 0);
                } else {
                    let spine = self.f(Field::Spine) as i32;
                    let bytes = self
                        .c(Command::Cumulative, spine, 0)
                        .wrapping_sub(if spine > 0 {
                            self.c(Command::Cumulative, spine - 1, 0)
                        } else {
                            0
                        });
                    let inflate = self.q(Query::HasHtml) == 0;
                    let popup = if anchor {
                        self.q(Query::FindAnchor) & 0x10000 == 0 && bytes > 96 * 1024
                    } else {
                        target >= self.q(Query::PageCount) as i32
                            && ((bytes > 96 * 1024 && inflate) || target > 20)
                    };
                    if popup {
                        self.op(Command::PopupIndex);
                        self.c(Command::Counter, 1, 0);
                    }
                    self.set(Field::BuildPopup, u32::from(!popup));
                    if self.q(Query::HasTtf) != 0 {
                        let _cache =
                            self.resource(Command::SampleFontCache, Command::ReleaseFontCache);
                    }
                    if self.op(Command::Room) == 0 {
                        self.op(Command::DropSection);
                        self.set(Field::BuildPopup, 0);
                        self.error(true);
                        return false;
                    }
                    self.op(Command::LogHeap);
                    let started_at = self.q(Query::Now);
                    let started = {
                        let _loan = self.resource(Command::LoanBegin, Command::LoanDrop);
                        self.op(Command::StartWithPopup) != 0
                    };
                    if !started {
                        self.op(Command::LogStartFailed);
                        self.op(Command::DropSection);
                        self.set(Field::BuildPopup, 0);
                        self.error(false);
                        return false;
                    }
                    while self.q(Query::Complete) == 0
                        && if anchor {
                            self.q(Query::FindAnchor) & 0x10000 == 0
                        } else if let Some(offset) = offset {
                            self.c(Command::ReachedOffset, offset as i32, 0) == 0
                        } else {
                            self.q(Query::PageCount) as i32 <= target
                        }
                    {
                        if self.f(Field::BuildPopup) != 0
                            && self.q(Query::Now).wrapping_sub(started_at) >= 1000
                        {
                            self.op(Command::ShowBuildPopup);
                        }
                        if self.c(Command::BuildMore, 8, 0) == 0 {
                            self.op(Command::LogStepFailed);
                            self.op(Command::DropSection);
                            self.set(Field::BuildPopup, 0);
                            self.error(false);
                            return false;
                        }
                    }
                    self.set(Field::BuildPopup, 0);
                }
            }
        } else {
            self.op(Command::LogCacheFound);
        }
        if self.f(Field::HasPendingPage) != 0 {
            self.c(Command::Page, self.f(Field::PendingPage) as i32, 0);
            self.set(Field::HasPendingPage, 0);
        } else {
            self.c(Command::Page, (self.f(Field::NextPage) as i32).max(0), 0);
        }
        if let Some(offset) = offset {
            let page = self.c(Command::MapOffset, offset as i32, 0);
            if page & 0x10000 != 0 {
                self.c(Command::Page, (page & 0xffff) as i32, 0);
                self.op(Command::ClearDeferred);
            }
        }
        if explicit_offset {
            self.op(Command::ClearDeferred);
        }
        self.set(Field::HasPendingOffset, 0);
        if self.q(Query::HasAnchor) != 0 {
            let page = self.q(Query::FindAnchor);
            if page & 0x10000 != 0 {
                self.c(Command::Page, (page & 0xffff) as i32, 0);
                self.c(Command::LogAnchor, (page & 0xffff) as i32, 0);
            }
            self.op(Command::ClearAnchor);
        }
        if self.f(Field::PendingPercent) != 0 && self.q(Query::PageCount) > 0 {
            let count = self.q(Query::PageCount) as i32;
            let mut page = (f32::from_bits(self.f(Field::SpineProgress)) * count as f32) as i32;
            if page >= count {
                page = count - 1;
            }
            self.c(Command::Page, page, 0);
            self.set(Field::PendingPercent, 0);
        }
        true
    }
    fn extend(&self) -> bool {
        if (self.q(Query::Partial) != 0 || self.q(Query::Building) != 0)
            && self.q(Query::Page) as i32 >= self.q(Query::PageCount) as i32
            && self.op(Command::Room) == 0
        {
            self.op(Command::DropSection);
            self.error(true);
            return false;
        }
        if self.q(Query::Partial) != 0
            && self.q(Query::Page) as i32 >= self.q(Query::PageCount) as i32
        {
            self.op(Command::PopupIndex);
            self.c(Command::Counter, 1, 0);
        }
        while self.q(Query::Partial) != 0
            && self.q(Query::Page) as i32 >= self.q(Query::PageCount) as i32
        {
            if self.q(Query::Building) == 0 && self.op(Command::StartPartial) == 0 {
                self.op(Command::LogPartialStartFailed);
                self.op(Command::DropSection);
                self.error(false);
                return false;
            }
            if !self.build_to_page() {
                return false;
            }
        }
        if self.q(Query::Building) != 0 && !self.build_to_page() {
            return false;
        }
        true
    }
    fn build_to_page(&self) -> bool {
        while self.q(Query::Complete) == 0
            && self.q(Query::Page) as i32 >= self.q(Query::PageCount) as i32
        {
            if self.c(Command::BuildMore, 8, 0) == 0 {
                self.op(Command::LogStepFailed);
                self.op(Command::DropSection);
                self.error(false);
                return false;
            }
        }
        true
    }
    fn page(&self) -> bool {
        let _page = self.resource(Command::LoadPage, Command::DropPage);
        if self.q(Query::HasPage) == 0 || self.op(Command::AssignMetadata) == 0 {
            self.op(if self.q(Query::HasPage) == 0 {
                Command::LogPageLoadFailed
            } else {
                Command::LogMetadataFailed
            });
            self.set(Field::Automatic, 0);
            let retry = (self.f(Field::Retry) as u8).wrapping_add(1);
            self.set(Field::Retry, u32::from(retry));
            let give_up = retry > 3;
            self.op(Command::AbandonBuild);
            self.op(Command::ClearCache);
            self.op(Command::DropSection);
            if give_up {
                self.op(Command::LogRetryFailed);
                self.set(Field::Retry, 0);
                self.op(Command::ClearScreen);
                self.c(Command::DrawLoadError, 300, 0);
                self.op(Command::Display);
                self.sync_error();
                return false;
            }
            self.op(Command::Update);
            self.sync_error();
            return false;
        }
        self.set(Field::Retry, 0);
        self.set(Field::CurrentOffset, self.q(Query::PageOffset));
        self.set(Field::MarginLeft, self.q(Query::MarginLeft));
        self.set(Field::MarginTop, self.q(Query::MarginTop));
        self.op(Command::DiscardOverlay);
        let start = self.q(Query::Now);
        self.op(Command::RenderContents);
        self.c(Command::LogRendered, start as i32, 0);
        self.set(Field::LastRender, self.q(Query::Now));
        self.op(Command::MarkRendered);
        true
    }
    fn notices(&self) {
        self.sync_error();
        if self.f(Field::Screenshot) != 0 {
            self.set(Field::Screenshot, 0);
            self.op(Command::Screenshot);
        }
        if self.f(Field::BookmarkNotice) != 0 {
            self.op(if self.f(Field::BookmarkRemoved) != 0 {
                Command::PopupBookmarkRemoved
            } else {
                Command::PopupBookmarkAdded
            });
        }
        if self.f(Field::DictionaryNotice) != 0 {
            self.op(Command::PopupDictionary);
        }
        if self.op(Command::TakeFontNotice) != 0 {
            self.set(Field::FontNotice, 1);
            self.set(Field::FontNoticeMs, self.q(Query::Now));
        }
        if self.f(Field::FontNotice) != 0 {
            self.op(Command::PopupFont);
        }
        if self.f(Field::ClippingNotice) != 0 {
            self.op(Command::PopupClipping);
        }
    }
    fn run(&self) {
        self.op(Command::ClearMetadata);
        if self.q(Query::HasBook) == 0 {
            return;
        }
        self.op(Command::RadioSettle);
        self.op(Command::SettleOverlay);
        if (self.f(Field::Spine) as i32) < 0 {
            self.set(Field::Spine, 0);
        }
        if self.f(Field::Spine) as i32 > self.q(Query::SpineCount) as i32 {
            self.set(Field::Spine, self.q(Query::SpineCount));
        }
        if self.f(Field::Spine) == self.q(Query::SpineCount) {
            return;
        }
        self.viewport();
        if self.q(Query::HasSection) == 0 && !self.initial_section() {
            return;
        }
        if !self.extend() {
            return;
        }
        if self.q(Query::Building) == 0
            && self.q(Query::PageCount) > 0
            && self.q(Query::Page) as i32 >= self.q(Query::PageCount) as i32
        {
            self.c(Command::Page, self.q(Query::PageCount) as i32 - 1, 0);
        }
        self.op(Command::ApplyDeferred);
        self.op(Command::ClearScreen);
        if self.q(Query::PageCount) == 0 {
            self.op(Command::LogEmpty);
            self.c(Command::DrawEmpty, 300, 0);
            self.op(Command::StatusBar);
            self.op(Command::Display);
            self.set(Field::PageStale, 0);
            self.set(Field::Automatic, 0);
            self.sync_error();
            return;
        }
        if (self.q(Query::Page) as i32) < 0 || self.q(Query::Page) >= self.q(Query::PageCount) {
            self.op(Command::LogBounds);
            self.c(Command::DrawBounds, 300, 0);
            self.op(Command::StatusBar);
            self.op(Command::Display);
            self.set(Field::Automatic, 0);
            self.sync_error();
            return;
        }
        self.op(Command::BookmarkFlag);
        if !self.page() {
            return;
        }
        if (self.f(Field::Spine) != self.f(Field::LastSavedSpine)
            || self.q(Query::Page) != self.f(Field::LastSavedPage)
            || self.q(Query::PageCount) != self.f(Field::LastSavedCount))
            && self.op(Command::SaveProgress) != 0
        {
            self.set(Field::LastSavedSpine, self.f(Field::Spine));
            self.set(Field::LastSavedPage, self.q(Query::Page));
            self.set(Field::LastSavedCount, self.q(Query::EstimatedCount));
        }
        self.notices();
        self.set(Field::PageStale, 0);
        if self.f(Field::Overlay) != 0 && self.q(Query::UsesToolbar) != 0 {
            self.set(Field::Stored, self.op(Command::Store));
            self.op(Command::RenderOverlay);
            if self.q(Query::PopupActive) != 0 {
                self.op(Command::RenderPopup);
            }
            self.op(Command::PushRefresh);
        }
    }
}
/// # Safety
/// Services remain valid for the synchronous call under RenderLock, never ModelLock.
/// Native build callbacks retain only the activity owner, never this context or its stack.
pub unsafe fn run(s: *const Services) {
    let Some(s) = s.as_ref() else {
        return;
    };
    let (Some(read), Some(write), Some(query), Some(command)) =
        (s.read, s.write, s.query, s.command)
    else {
        return;
    };
    Engine {
        core: Core {
            context: s.context,
            read,
            write,
            command,
        },
        query,
    }
    .run();
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
    assert!(Field::Spine as u8 == 0);
    assert!(Field::CachedSpine as u8 == 1);
    assert!(Field::CachedCount as u8 == 2);
    assert!(Field::HasCachedOffset as u8 == 3);
    assert!(Field::CachedOffset as u8 == 4);
    assert!(Field::HasPendingPage as u8 == 5);
    assert!(Field::PendingPage as u8 == 6);
    assert!(Field::NextPage as u8 == 7);
    assert!(Field::HasPendingOffset as u8 == 8);
    assert!(Field::PendingOffset as u8 == 9);
    assert!(Field::PendingPercent as u8 == 10);
    assert!(Field::SpineProgress as u8 == 11);
    assert!(Field::Automatic as u8 == 12);
    assert!(Field::SyncError as u8 == 13);
    assert!(Field::PartialStartFailed as u8 == 14);
    assert!(Field::BuildPopup as u8 == 15);
    assert!(Field::BuildWidth as u8 == 16);
    assert!(Field::BuildHeight as u8 == 17);
    assert!(Field::PageStale as u8 == 18);
    assert!(Field::Retry as u8 == 19);
    assert!(Field::CurrentOffset as u8 == 20);
    assert!(Field::MarginLeft as u8 == 21);
    assert!(Field::MarginTop as u8 == 22);
    assert!(Field::LastRender as u8 == 23);
    assert!(Field::LastSavedSpine as u8 == 24);
    assert!(Field::LastSavedPage as u8 == 25);
    assert!(Field::LastSavedCount as u8 == 26);
    assert!(Field::Screenshot as u8 == 27);
    assert!(Field::BookmarkNotice as u8 == 28);
    assert!(Field::BookmarkRemoved as u8 == 29);
    assert!(Field::DictionaryNotice as u8 == 30);
    assert!(Field::FontNotice as u8 == 31);
    assert!(Field::FontNoticeMs as u8 == 32);
    assert!(Field::ClippingNotice as u8 == 33);
    assert!(Field::Overlay as u8 == 34);
    assert!(Field::Stored as u8 == 35);
    assert!(Query::HasBook as u8 == 0);
    assert!(Query::SpineCount as u8 == 1);
    assert!(Query::HasSection as u8 == 2);
    assert!(Query::MarginTop as u8 == 3);
    assert!(Query::MarginRight as u8 == 4);
    assert!(Query::MarginBottom as u8 == 5);
    assert!(Query::MarginLeft as u8 == 6);
    assert!(Query::ScreenMargin as u8 == 7);
    assert!(Query::StatusHeight as u8 == 8);
    assert!(Query::ProgressHeight as u8 == 9);
    assert!(Query::StatusMargin as u8 == 10);
    assert!(Query::Width as u8 == 11);
    assert!(Query::Height as u8 == 12);
    assert!(Query::Partial as u8 == 13);
    assert!(Query::Building as u8 == 14);
    assert!(Query::Complete as u8 == 15);
    assert!(Query::PageCount as u8 == 16);
    assert!(Query::Page as u8 == 17);
    assert!(Query::HasAnchor as u8 == 18);
    assert!(Query::HasHtml as u8 == 19);
    assert!(Query::FindAnchor as u8 == 20);
    assert!(Query::PageForAnchor as u8 == 21);
    assert!(Query::HasTtf as u8 == 22);
    assert!(Query::Now as u8 == 24);
    assert!(Query::PopupActive as u8 == 25);
    assert!(Query::UsesToolbar as u8 == 26);
    assert!(Query::HasPage as u8 == 28);
    assert!(Query::PageOffset as u8 == 29);
    assert!(Query::EstimatedCount as u8 == 30);
    assert!(Command::ClearMetadata as u8 == 0);
    assert!(Command::RadioSettle as u8 == 1);
    assert!(Command::SettleOverlay as u8 == 2);
    assert!(Command::SampleMargins as u8 == 3);
    assert!(Command::SetMargin as u8 == 4);
    assert!(Command::SetSpec as u8 == 5);
    assert!(Command::GetPath as u8 == 6);
    assert!(Command::DropPath as u8 == 7);
    assert!(Command::LogLoading as u8 == 8);
    assert!(Command::NewSection as u8 == 9);
    assert!(Command::LoadCache as u8 == 10);
    assert!(Command::LogPartial as u8 == 11);
    assert!(Command::LogNoCache as u8 == 12);
    assert!(Command::Counter as u8 == 13);
    assert!(Command::Room as u8 == 14);
    assert!(Command::DropSection as u8 == 15);
    assert!(Command::LoanBegin as u8 == 16);
    assert!(Command::LoanEnd as u8 == 17);
    assert!(Command::LoanDrop as u8 == 18);
    assert!(Command::CreateAll as u8 == 19);
    assert!(Command::LogPersistFailed as u8 == 20);
    assert!(Command::LogDeferred as u8 == 21);
    assert!(Command::Cumulative as u8 == 22);
    assert!(Command::ReleaseFontCache as u8 == 23);
    assert!(Command::LogHeap as u8 == 24);
    assert!(Command::StartWithPopup as u8 == 25);
    assert!(Command::LogStartFailed as u8 == 26);
    assert!(Command::ReachedOffset as u8 == 27);
    assert!(Command::ShowBuildPopup as u8 == 28);
    assert!(Command::BuildMore as u8 == 29);
    assert!(Command::LogStepFailed as u8 == 30);
    assert!(Command::LogCacheFound as u8 == 31);
    assert!(Command::Page as u8 == 32);
    assert!(Command::MapOffset as u8 == 33);
    assert!(Command::ClearDeferred as u8 == 34);
    assert!(Command::LogAnchor as u8 == 35);
    assert!(Command::ClearAnchor as u8 == 36);
    assert!(Command::StartPartial as u8 == 37);
    assert!(Command::LogPartialStartFailed as u8 == 38);
    assert!(Command::ApplyDeferred as u8 == 39);
    assert!(Command::ClearScreen as u8 == 40);
    assert!(Command::LogEmpty as u8 == 41);
    assert!(Command::StatusBar as u8 == 42);
    assert!(Command::Display as u8 == 43);
    assert!(Command::LogBounds as u8 == 44);
    assert!(Command::BookmarkFlag as u8 == 45);
    assert!(Command::LoadPage as u8 == 46);
    assert!(Command::DropPage as u8 == 47);
    assert!(Command::AssignMetadata as u8 == 48);
    assert!(Command::LogPageLoadFailed as u8 == 49);
    assert!(Command::LogMetadataFailed as u8 == 50);
    assert!(Command::AbandonBuild as u8 == 51);
    assert!(Command::ClearCache as u8 == 52);
    assert!(Command::LogRetryFailed as u8 == 53);
    assert!(Command::Update as u8 == 54);
    assert!(Command::DiscardOverlay as u8 == 55);
    assert!(Command::RenderContents as u8 == 56);
    assert!(Command::LogRendered as u8 == 57);
    assert!(Command::MarkRendered as u8 == 58);
    assert!(Command::SaveProgress as u8 == 59);
    assert!(Command::Screenshot as u8 == 60);
    assert!(Command::Store as u8 == 61);
    assert!(Command::RenderOverlay as u8 == 62);
    assert!(Command::RenderPopup as u8 == 63);
    assert!(Command::PushRefresh as u8 == 64);
    assert!(Command::BackHints as u8 == 65);
    assert!(Command::PopupSync as u8 == 66);
    assert!(Command::PopupRadioError as u8 == 67);
    assert!(Command::PopupBuildError as u8 == 68);
    assert!(Command::PopupIndex as u8 == 69);
    assert!(Command::PopupBookmarkRemoved as u8 == 70);
    assert!(Command::PopupBookmarkAdded as u8 == 71);
    assert!(Command::PopupDictionary as u8 == 72);
    assert!(Command::PopupFont as u8 == 73);
    assert!(Command::PopupClipping as u8 == 74);
    assert!(Command::DrawEmpty as u8 == 75);
    assert!(Command::DrawBounds as u8 == 76);
    assert!(Command::DrawLoadError as u8 == 77);
    assert!(Command::SampleFontCache as u8 == 78);
    assert!(Command::TakeFontNotice as u8 == 79);
};

#[cfg(test)]
mod tests {
    use super::*;
    unsafe extern "C" fn read(_: *mut c_void, _: u8) -> u32 {
        panic!("incomplete services must not be called");
    }
    unsafe extern "C" fn write(_: *mut c_void, _: u8, _: u32) {
        panic!("incomplete services must not be called");
    }
    unsafe extern "C" fn query(_: *mut c_void, _: u8) -> u32 {
        panic!("incomplete services must not be called");
    }
    unsafe extern "C" fn command(_: *mut c_void, _: u8, _: i32, _: i32) -> u32 {
        panic!("incomplete services must not be called");
    }
    #[test]
    fn incomplete_services_have_no_effects() {
        unsafe {
            run(core::ptr::null());
        }
        for missing in 0..4 {
            let mut services = Services {
                context: core::ptr::null_mut(),
                read: Some(read),
                write: Some(write),
                query: Some(query),
                command: Some(command),
            };
            match missing {
                0 => services.read = None,
                1 => services.write = None,
                2 => services.query = None,
                _ => services.command = None,
            }
            unsafe {
                run(&services);
            }
        }
    }
}

#[cfg(test)]
mod resource_tests {
    use super::*;
    struct Provider {
        fields: [u32; 36],
        queries: [u32; 31],
        live: [i32; 4],
        flags: u32,
        calls: usize,
        visited: [bool; 80],
    }
    impl Provider {
        fn new(scenario: u32, flags: u32, retry: u32) -> Self {
            let mut p = Self {
                fields: [0; 36],
                queries: [0; 31],
                live: [0; 4],
                flags,
                calls: 0,
                visited: [false; 80],
            };
            for (q, value) in [
                (Query::HasBook, u32::from(scenario != 6)),
                (Query::SpineCount, 3),
                (Query::HasSection, u32::from((1..=5).contains(&scenario))),
                (Query::Width, 600),
                (Query::Height, 800),
                (Query::Partial, u32::from(scenario == 1)),
                (Query::Building, u32::from(scenario == 2)),
                (Query::Complete, u32::from(scenario != 2)),
                (
                    Query::PageCount,
                    if (1..=3).contains(&scenario) { 0 } else { 10 },
                ),
                (Query::Page, if scenario == 4 { u32::MAX } else { 2 }),
                (Query::HasAnchor, u32::from(scenario == 10)),
                (Query::HasTtf, (flags >> 7) & 1),
                (Query::HasPage, (flags >> 5) & 1),
                (Query::UsesToolbar, 1),
                (Query::PopupActive, 1),
                (Query::EstimatedCount, 10),
            ] {
                p.queries[q as usize] = value;
            }
            for (field, value) in [
                (Field::Spine, if scenario == 7 { 3 } else { 0 }),
                (Field::Retry, retry),
                (Field::NextPage, 2),
                (Field::PendingPercent, u32::from(scenario == 8)),
                (Field::HasPendingOffset, u32::from(scenario == 9)),
                (Field::PendingOffset, 123),
                (Field::Automatic, 1),
                (Field::SyncError, 1),
                (Field::Screenshot, 1),
                (Field::BookmarkNotice, 1),
                (Field::DictionaryNotice, 1),
                (Field::ClippingNotice, 1),
                (Field::Overlay, 1),
            ] {
                p.fields[field as usize] = value;
            }
            p
        }
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
    unsafe extern "C" fn read(p: *mut c_void, field: u8) -> u32 {
        (*p.cast::<Provider>()).fields[usize::from(field)]
    }
    unsafe extern "C" fn write(p: *mut c_void, field: u8, value: u32) {
        (*p.cast::<Provider>()).fields[usize::from(field)] = value;
    }
    unsafe extern "C" fn query(p: *mut c_void, id: u8) -> u32 {
        let p = &mut *p.cast::<Provider>();
        assert!(id != 23 && id != 27, "consuming operations are commands");
        if id == Query::Now as u8 {
            p.queries[usize::from(id)] = p.queries[usize::from(id)].wrapping_add(1000);
        }
        p.queries[usize::from(id)]
    }
    unsafe extern "C" fn command(p: *mut c_void, id: u8, a: i32, _b: i32) -> u32 {
        let p = &mut *p.cast::<Provider>();
        p.calls += 1;
        assert!(p.calls < 200, "build must stop or make progress");
        p.visited[usize::from(id)] = true;
        for (slot, acquire, release) in [
            (0, Command::GetPath, Command::DropPath),
            (1, Command::LoanBegin, Command::LoanDrop),
            (2, Command::LoadPage, Command::DropPage),
            (3, Command::SampleFontCache, Command::ReleaseFontCache),
        ] {
            if id == acquire as u8 {
                assert_eq!(p.live[slot], 0);
                p.live[slot] += 1;
            }
            if id == release as u8 {
                assert_eq!(p.live[slot], 1);
                p.live[slot] -= 1;
            }
        }
        if id == Command::LoanEnd as u8 {
            assert_eq!(p.live[1], 1);
        }
        if id == Command::Room as u8 {
            return p.flags & 1;
        }
        if id == Command::LoadCache as u8 {
            return (p.flags >> 1) & 1;
        }
        if id == Command::NewSection as u8 {
            p.queries[Query::HasSection as usize] = 1;
        }
        if id == Command::DropSection as u8 {
            p.queries[Query::HasSection as usize] = 0;
        }
        if id == Command::StartWithPopup as u8 || id == Command::StartPartial as u8 {
            p.queries[Query::Building as usize] = 1;
            p.queries[Query::Complete as usize] = 0;
            p.queries[Query::PageCount as usize] = 0;
            return (p.flags >> 2) & 1;
        }
        if id == Command::BuildMore as u8 || id == Command::CreateAll as u8 {
            p.queries[Query::Building as usize] = 0;
            p.queries[Query::Partial as usize] = 0;
            p.queries[Query::Complete as usize] = 1;
            p.queries[Query::PageCount as usize] = 10;
            return (p.flags >> if id == Command::BuildMore as u8 { 3 } else { 4 }) & 1;
        }
        if id == Command::Page as u8 {
            p.queries[Query::Page as usize] = a as u32;
        }
        if id == Command::AssignMetadata as u8 {
            assert_eq!(p.live[2], 1);
            return (p.flags >> 6) & 1;
        }
        if id == Command::RenderContents as u8 {
            assert_eq!(p.live[2], 1);
        }
        if id == Command::TakeFontNotice as u8 {
            return (p.flags >> 8) & 1;
        }
        if id == Command::ClearAnchor as u8 {
            p.queries[Query::HasAnchor as usize] = 0;
        }
        0
    }
    #[test]
    fn all_returns_balance_native_resources() {
        let mut visited = [false; 80];
        for scenario in 0..11 {
            for flags in 0..512 {
                for retry in [0, 3, 255] {
                    let mut p = Provider::new(scenario, flags, retry);
                    unsafe {
                        run(&p.services());
                    }
                    assert_eq!(
                        p.live, [0; 4],
                        "scenario={scenario}, flags={flags}, retry={retry}"
                    );
                    for (all, saw) in visited.iter_mut().zip(p.visited) {
                        *all |= saw;
                    }
                }
            }
        }
        for command in [
            Command::GetPath,
            Command::LoanBegin,
            Command::LoadPage,
            Command::SampleFontCache,
            Command::PopupRadioError,
            Command::PopupBuildError,
            Command::LogPersistFailed,
            Command::LogStartFailed,
            Command::LogStepFailed,
            Command::LogPartialStartFailed,
            Command::LogEmpty,
            Command::LogBounds,
            Command::LogRetryFailed,
            Command::Update,
            Command::MarkRendered,
        ] {
            assert!(
                visited[command as usize],
                "return-path fixture did not cover command {}",
                command as u8
            );
        }
    }
}
