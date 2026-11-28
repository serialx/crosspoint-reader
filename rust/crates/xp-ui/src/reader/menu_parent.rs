//! Reader menu parents and retained child results.
use super::services::{Core, Guard};
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Open = 0,
    Confirm = 1,
    BuildMore = 2,
    ActivateMore = 3,
    MenuResult = 4,
    ChapterResult = 5,
    TextResult = 6,
    PercentResult = 7,
    ProgressResult = 8,
    ReopenResult = 9,
    OrientationResult = 10,
    AutoResult = 11,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    PendingTurn = 0,
    Overlay = 1,
    Focus = 2,
    HoldJumped = 3,
    Cursor = 4,
    CachedSpine = 5,
    CachedCount = 6,
    NextPage = 7,
    Spine = 8,
    Screenshot = 9,
    OverlayStored = 10,
    ShowBookmark = 11,
    BookmarkTime = 12,
    AutoOption = 13,
    UsesToolbar = 14,
    HasToolbar = 15,
    Touch = 16,
    HasBook = 17,
    HasSection = 18,
    Page = 19,
    PageCount = 20,
    MoreCount = 21,
    Orientation = 22,
    ScreenInverted = 23,
    FrontlightOn = 24,
    OrientationCount = 25,
    RateCount = 26,
    Cancelled = 27,
    ResultOrientation = 28,
    ResultAuto = 29,
    ResultAction = 30,
    ResultSpine = 31,
    TemporaryCount = 32,
    StripsEnabled = 33,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    Lock = 0,
    Unlock = 1,
    ReleaseStrips = 2,
    NewToolbar = 3,
    BeginToolbar = 4,
    Discard = 5,
    Update = 6,
    PreparePosition = 7,
    Percent = 8,
    FootnotesPresent = 9,
    ShowMenu = 10,
    ApplyOrientation = 11,
    AutoTurn = 12,
    OpenMenu = 13,
    CurrentToc = 14,
    RememberOffset = 15,
    DropSection = 16,
    ShowChapter = 17,
    ClearDeferred = 18,
    AssignAnchor = 19,
    ShowText = 20,
    Footnotes = 21,
    ShowPercent = 22,
    Dictionary = 23,
    PrepareQr = 24,
    DropQr = 25,
    ShowQr = 26,
    Home = 27,
    ClearCache = 28,
    SetupCache = 29,
    SaveProgress = 30,
    LogSaveFailed = 31,
    Sync = 32,
    ShowBookmarks = 33,
    StartClip = 34,
    OpenClippings = 35,
    Bookmark = 36,
    LoadBookmarks = 37,
    ApplyProgress = 38,
    JumpPercent = 39,
    BuildItems = 40,
    ActionAt = 41,
    EraseItem = 42,
    Settle = 43,
    RestorePage = 44,
    ShowOrientation = 45,
    PrepareLabels = 46,
    DropLabels = 47,
    ReserveLabels = 48,
    AddOffLabel = 49,
    AddRateLabel = 50,
    ShowAuto = 51,
    PaintPopup = 52,
    ToggleBluetooth = 53,
    RenderOverlay = 54,
    PushOverlay = 55,
    SaveSettings = 56,
    LightGet = 57,
    LightSet = 58,
    DisplayFast = 59,
    Clock = 60,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Action {
    Chapter = 0,
    Footnotes = 1,
    Text = 2,
    Night = 3,
    Frontlight = 4,
    Bluetooth = 5,
    Percent = 6,
    Auto = 7,
    Orientation = 8,
    Bookmarks = 9,
    SaveClipping = 10,
    ViewClippings = 11,
    ToggleBookmark = 12,
    Screenshot = 13,
    Qr = 14,
    Home = 15,
    Sync = 16,
    DeleteCache = 17,
    Dictionary = 18,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub write: Option<unsafe extern "C" fn(*mut c_void, u8, u32)>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
}
struct Engine(Core);
impl Engine {
    fn f(&self, field: Field) -> u32 {
        self.0.read(field as u8)
    }
    fn set(&self, field: Field, value: u32) {
        self.0.write(field as u8, value);
    }
    fn op(&self, cmd: Command) -> u32 {
        self.0.op(cmd as u8)
    }
    fn cmd(&self, cmd: Command, a: i32, b: i32) -> u32 {
        self.0.command(cmd as u8, a, b)
    }
    fn one(&self, cmd: Command, a: i32) -> u32 {
        self.cmd(cmd, a, 0)
    }
    fn lock(&self) -> Guard<'_> {
        self.op(Command::Lock);
        self.0.guard(Command::Unlock as u8)
    }
    fn cache(&self) {
        if self.f(Field::HasSection) != 0 {
            self.op(Command::RememberOffset);
            self.set(Field::CachedSpine, self.f(Field::Spine));
            self.set(Field::CachedCount, self.f(Field::PageCount));
            self.set(Field::NextPage, self.f(Field::Page));
        }
    }
    fn open(&self) {
        self.set(Field::PendingTurn, 0);
        if self.f(Field::UsesToolbar) != 0 {
            self.set(Field::Overlay, 1);
            self.set(Field::Focus, 0);
            self.set(Field::HoldJumped, 0);
            self.set(Field::Cursor, u32::from(self.f(Field::Touch) == 0));
            if self.f(Field::HasToolbar) == 0 {
                self.op(Command::NewToolbar);
            }
            self.op(Command::BeginToolbar);
            self.op(Command::Discard);
            self.op(Command::Update);
            return;
        }
        self.op(Command::PreparePosition);
        let percent = self.op(Command::Percent);
        let footnotes = {
            let _lock = self.lock();
            self.op(Command::FootnotesPresent)
        };
        self.cmd(Command::ShowMenu, percent as i32, footnotes as i32);
    }
    fn confirm(&self, action: i32) {
        if self.f(Field::StripsEnabled) != 0 {
            let _lock = self.lock();
            self.op(Command::ReleaseStrips);
        }
        match action {
            x if x == Action::Chapter as i32 => {
                let toc;
                {
                    let _lock = self.lock();
                    toc = self.op(Command::CurrentToc);
                    self.cache();
                    self.op(Command::DropSection);
                }
                self.one(Command::ShowChapter, toc as i32);
            }
            x if x == Action::Footnotes as i32 => {
                self.one(Command::Footnotes, 1);
            }
            x if x == Action::Text as i32 => {
                self.op(Command::ShowText);
            }
            x if x == Action::Percent as i32 => {
                self.op(Command::PreparePosition);
                let percent = self.op(Command::Percent);
                self.one(Command::ShowPercent, percent as i32);
            }
            x if x == Action::Dictionary as i32 => {
                self.op(Command::Dictionary);
            }
            x if x == Action::Qr as i32 => {
                if self.f(Field::HasSection) != 0
                    && (self.f(Field::Page) as i32) >= 0
                    && (self.f(Field::Page) as i32) < self.f(Field::PageCount) as i32
                {
                    let nonempty = self.op(Command::PrepareQr) != 0;
                    let _text = self.0.guard(Command::DropQr as u8);
                    if nonempty {
                        self.op(Command::ShowQr);
                        return;
                    }
                }
                self.op(Command::Update);
            }
            x if x == Action::Home as i32 => {
                self.op(Command::Home);
            }
            x if x == Action::DeleteCache as i32 => {
                {
                    let _lock = self.lock();
                    if self.f(Field::HasBook) != 0 && self.f(Field::HasSection) != 0 {
                        let spine = self.f(Field::Spine) as u16;
                        let page = self.f(Field::Page) as u16;
                        self.set(
                            Field::TemporaryCount,
                            u32::from(self.f(Field::PageCount) as u16),
                        );
                        self.op(Command::DropSection);
                        self.op(Command::ClearCache);
                        self.op(Command::SetupCache);
                        if self.cmd(Command::SaveProgress, i32::from(spine), i32::from(page)) == 0 {
                            self.op(Command::LogSaveFailed);
                        }
                    }
                }
                self.op(Command::Home);
            }
            x if x == Action::Screenshot as i32 => {
                {
                    let _lock = self.lock();
                    self.set(Field::Screenshot, 1);
                }
                self.op(Command::Update);
            }
            x if x == Action::Sync as i32 => {
                self.op(Command::Sync);
            }
            x if x == Action::Bookmarks as i32 => {
                self.op(Command::ShowBookmarks);
            }
            x if x == Action::SaveClipping as i32 => {
                self.op(Command::StartClip);
            }
            x if x == Action::ViewClippings as i32 => {
                self.op(Command::OpenClippings);
            }
            x if x == Action::ToggleBookmark as i32 => {
                self.op(Command::Bookmark);
            }
            _ => {}
        }
    }
    fn build_more(&self) {
        let footnotes = {
            let _lock = self.lock();
            self.op(Command::FootnotesPresent)
        };
        self.one(Command::BuildItems, footnotes as i32);
        let mut row = 0;
        while row < self.f(Field::MoreCount) {
            let action = self.one(Command::ActionAt, row as i32);
            if action == Action::Chapter as u32 || action == Action::Text as u32 {
                self.one(Command::EraseItem, row as i32);
            } else {
                row += 1;
            }
        }
    }
    fn activate(&self, row: i32) {
        if row < 0 || row as u32 >= self.f(Field::MoreCount) {
            return;
        }
        {
            let _lock = self.lock();
            self.op(Command::Settle);
        }
        let action = self.one(Command::ActionAt, row) as i32;
        match action {
            x if x == Action::Orientation as i32 => {
                let Some(index) = self
                    .f(Field::Orientation)
                    .checked_rem(self.f(Field::OrientationCount))
                else {
                    return;
                };
                self.one(Command::ShowOrientation, index as i32);
                self.op(Command::PaintPopup);
                return;
            }
            x if x == Action::Auto as i32 => {
                self.op(Command::PrepareLabels);
                let _labels = self.0.guard(Command::DropLabels as u8);
                let count = self.f(Field::RateCount);
                if count > i32::MAX as u32 {
                    return;
                }
                self.one(Command::ReserveLabels, count as i32);
                self.op(Command::AddOffLabel);
                for index in 1..count {
                    self.one(Command::AddRateLabel, index as i32);
                }
                self.one(Command::ShowAuto, self.f(Field::AutoOption) as i32);
                self.op(Command::PaintPopup);
                return;
            }
            x if x == Action::Bluetooth as i32 => {
                self.op(Command::ToggleBluetooth);
                let _lock = self.lock();
                self.op(Command::RenderOverlay);
                self.op(Command::PushOverlay);
                return;
            }
            x if x == Action::Night as i32 => {
                self.set(
                    Field::ScreenInverted,
                    u32::from(self.f(Field::ScreenInverted) == 0),
                );
                self.op(Command::SaveSettings);
                self.op(Command::Discard);
                self.op(Command::Update);
                return;
            }
            x if x == Action::Frontlight as i32 => {
                let on = u32::from(self.op(Command::LightGet) == 0);
                self.one(Command::LightSet, on as i32);
                self.set(Field::FrontlightOn, on);
                self.op(Command::SaveSettings);
                let _lock = self.lock();
                self.op(Command::RenderOverlay);
                self.op(Command::DisplayFast);
                return;
            }
            _ => {}
        }
        self.set(Field::Overlay, 0);
        if action == Action::Percent as i32 && self.f(Field::OverlayStored) != 0 {
            let _lock = self.lock();
            self.op(Command::Settle);
            self.op(Command::RestorePage);
            self.set(Field::OverlayStored, 0);
        } else {
            self.op(Command::Discard);
        }
        if action == Action::ToggleBookmark as i32 {
            self.op(Command::Bookmark);
            self.set(Field::ShowBookmark, 1);
            self.set(Field::BookmarkTime, self.op(Command::Clock));
            self.op(Command::Update);
            return;
        }
        self.confirm(action);
        if action != Action::Home as i32 && action != Action::DeleteCache as i32 {
            self.op(Command::Update);
        }
    }
    fn result(&self, operation: u8, argument: i32) {
        match operation {
            x if x == Operation::MenuResult as u8 => {
                if self.f(Field::Orientation) != self.f(Field::ResultOrientation) {
                    self.one(
                        Command::ApplyOrientation,
                        self.f(Field::ResultOrientation) as i32,
                    );
                }
                self.one(Command::AutoTurn, self.f(Field::ResultAuto) as i32);
                if self.f(Field::Cancelled) == 0 {
                    self.confirm(self.f(Field::ResultAction) as i32);
                }
            }
            x if x == Operation::ChapterResult as u8 => {
                if self.f(Field::Cancelled) != 0 {
                    self.op(Command::OpenMenu);
                    return;
                }
                let _lock = self.lock();
                self.op(Command::ClearDeferred);
                self.set(Field::Spine, self.f(Field::ResultSpine));
                self.op(Command::AssignAnchor);
                self.set(Field::NextPage, 0);
                self.op(Command::DropSection);
                self.op(Command::Update);
            }
            x if x == Operation::TextResult as u8 => {
                {
                    let _lock = self.lock();
                    self.cache();
                    self.op(Command::DropSection);
                }
                self.op(Command::OpenMenu);
            }
            x if x == Operation::PercentResult as u8 => {
                self.op(if self.f(Field::Cancelled) != 0 {
                    Command::OpenMenu
                } else {
                    Command::JumpPercent
                });
            }
            x if x == Operation::ProgressResult as u8 => {
                self.op(Command::LoadBookmarks);
                self.op(if self.f(Field::Cancelled) != 0 {
                    Command::OpenMenu
                } else {
                    Command::ApplyProgress
                });
            }
            x if x == Operation::ReopenResult as u8 => {
                self.op(Command::OpenMenu);
            }
            x if x == Operation::OrientationResult as u8 => {
                if argument == self.f(Field::Orientation) as i32 {
                    return;
                }
                self.one(Command::ApplyOrientation, argument);
                self.op(Command::Discard);
                self.op(Command::Update);
            }
            x if x == Operation::AutoResult as u8 => {
                self.set(Field::AutoOption, argument as u32);
                self.one(Command::AutoTurn, argument);
            }
            _ => {}
        }
    }
}
/// # Safety
/// Services satisfy the synchronous menu-parent resource contract.
pub unsafe fn run(services: *const Services, operation: u8, argument: i32) {
    let Some(s) = services.as_ref() else {
        return;
    };
    let (Some(read), Some(write), Some(command)) = (s.read, s.write, s.command) else {
        return;
    };
    if s.context.is_null() {
        return;
    }
    let e = Engine(Core {
        context: s.context,
        read,
        write,
        command,
    });
    match operation {
        0 => e.open(),
        1 => e.confirm(argument),
        2 => e.build_more(),
        3 => e.activate(argument),
        _ => e.result(operation, argument),
    }
}
const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<Services>() == 4 * size_of::<usize>());
    assert!(align_of::<Services>() == align_of::<usize>());
    assert!(offset_of!(Services, context) == 0);
    assert!(offset_of!(Services, read) == size_of::<usize>());
    assert!(offset_of!(Services, write) == 2 * size_of::<usize>());
    assert!(offset_of!(Services, command) == 3 * size_of::<usize>());
    assert!(Operation::Open as u8 == 0);
    assert!(Operation::Confirm as u8 == 1);
    assert!(Operation::BuildMore as u8 == 2);
    assert!(Operation::ActivateMore as u8 == 3);
    assert!(Operation::MenuResult as u8 == 4);
    assert!(Operation::ChapterResult as u8 == 5);
    assert!(Operation::TextResult as u8 == 6);
    assert!(Operation::PercentResult as u8 == 7);
    assert!(Operation::ProgressResult as u8 == 8);
    assert!(Operation::ReopenResult as u8 == 9);
    assert!(Operation::OrientationResult as u8 == 10);
    assert!(Operation::AutoResult as u8 == 11);
    assert!(Field::PendingTurn as u8 == 0);
    assert!(Field::Overlay as u8 == 1);
    assert!(Field::Focus as u8 == 2);
    assert!(Field::HoldJumped as u8 == 3);
    assert!(Field::Cursor as u8 == 4);
    assert!(Field::CachedSpine as u8 == 5);
    assert!(Field::CachedCount as u8 == 6);
    assert!(Field::NextPage as u8 == 7);
    assert!(Field::Spine as u8 == 8);
    assert!(Field::Screenshot as u8 == 9);
    assert!(Field::OverlayStored as u8 == 10);
    assert!(Field::ShowBookmark as u8 == 11);
    assert!(Field::BookmarkTime as u8 == 12);
    assert!(Field::AutoOption as u8 == 13);
    assert!(Field::UsesToolbar as u8 == 14);
    assert!(Field::HasToolbar as u8 == 15);
    assert!(Field::Touch as u8 == 16);
    assert!(Field::HasBook as u8 == 17);
    assert!(Field::HasSection as u8 == 18);
    assert!(Field::Page as u8 == 19);
    assert!(Field::PageCount as u8 == 20);
    assert!(Field::MoreCount as u8 == 21);
    assert!(Field::Orientation as u8 == 22);
    assert!(Field::ScreenInverted as u8 == 23);
    assert!(Field::FrontlightOn as u8 == 24);
    assert!(Field::OrientationCount as u8 == 25);
    assert!(Field::RateCount as u8 == 26);
    assert!(Field::Cancelled as u8 == 27);
    assert!(Field::ResultOrientation as u8 == 28);
    assert!(Field::ResultAuto as u8 == 29);
    assert!(Field::ResultAction as u8 == 30);
    assert!(Field::ResultSpine as u8 == 31);
    assert!(Field::TemporaryCount as u8 == 32);
    assert!(Field::StripsEnabled as u8 == 33);
    assert!(Command::Lock as u8 == 0);
    assert!(Command::Unlock as u8 == 1);
    assert!(Command::ReleaseStrips as u8 == 2);
    assert!(Command::NewToolbar as u8 == 3);
    assert!(Command::BeginToolbar as u8 == 4);
    assert!(Command::Discard as u8 == 5);
    assert!(Command::Update as u8 == 6);
    assert!(Command::PreparePosition as u8 == 7);
    assert!(Command::Percent as u8 == 8);
    assert!(Command::FootnotesPresent as u8 == 9);
    assert!(Command::ShowMenu as u8 == 10);
    assert!(Command::ApplyOrientation as u8 == 11);
    assert!(Command::AutoTurn as u8 == 12);
    assert!(Command::OpenMenu as u8 == 13);
    assert!(Command::CurrentToc as u8 == 14);
    assert!(Command::RememberOffset as u8 == 15);
    assert!(Command::DropSection as u8 == 16);
    assert!(Command::ShowChapter as u8 == 17);
    assert!(Command::ClearDeferred as u8 == 18);
    assert!(Command::AssignAnchor as u8 == 19);
    assert!(Command::ShowText as u8 == 20);
    assert!(Command::Footnotes as u8 == 21);
    assert!(Command::ShowPercent as u8 == 22);
    assert!(Command::Dictionary as u8 == 23);
    assert!(Command::PrepareQr as u8 == 24);
    assert!(Command::DropQr as u8 == 25);
    assert!(Command::ShowQr as u8 == 26);
    assert!(Command::Home as u8 == 27);
    assert!(Command::ClearCache as u8 == 28);
    assert!(Command::SetupCache as u8 == 29);
    assert!(Command::SaveProgress as u8 == 30);
    assert!(Command::LogSaveFailed as u8 == 31);
    assert!(Command::Sync as u8 == 32);
    assert!(Command::ShowBookmarks as u8 == 33);
    assert!(Command::StartClip as u8 == 34);
    assert!(Command::OpenClippings as u8 == 35);
    assert!(Command::Bookmark as u8 == 36);
    assert!(Command::LoadBookmarks as u8 == 37);
    assert!(Command::ApplyProgress as u8 == 38);
    assert!(Command::JumpPercent as u8 == 39);
    assert!(Command::BuildItems as u8 == 40);
    assert!(Command::ActionAt as u8 == 41);
    assert!(Command::EraseItem as u8 == 42);
    assert!(Command::Settle as u8 == 43);
    assert!(Command::RestorePage as u8 == 44);
    assert!(Command::ShowOrientation as u8 == 45);
    assert!(Command::PrepareLabels as u8 == 46);
    assert!(Command::DropLabels as u8 == 47);
    assert!(Command::ReserveLabels as u8 == 48);
    assert!(Command::AddOffLabel as u8 == 49);
    assert!(Command::AddRateLabel as u8 == 50);
    assert!(Command::ShowAuto as u8 == 51);
    assert!(Command::PaintPopup as u8 == 52);
    assert!(Command::ToggleBluetooth as u8 == 53);
    assert!(Command::RenderOverlay as u8 == 54);
    assert!(Command::PushOverlay as u8 == 55);
    assert!(Command::SaveSettings as u8 == 56);
    assert!(Command::LightGet as u8 == 57);
    assert!(Command::LightSet as u8 == 58);
    assert!(Command::DisplayFast as u8 == 59);
    assert!(Command::Clock as u8 == 60);
    assert!(Action::Chapter as u8 == 0);
    assert!(Action::Footnotes as u8 == 1);
    assert!(Action::Text as u8 == 2);
    assert!(Action::Night as u8 == 3);
    assert!(Action::Frontlight as u8 == 4);
    assert!(Action::Bluetooth as u8 == 5);
    assert!(Action::Percent as u8 == 6);
    assert!(Action::Auto as u8 == 7);
    assert!(Action::Orientation as u8 == 8);
    assert!(Action::Bookmarks as u8 == 9);
    assert!(Action::SaveClipping as u8 == 10);
    assert!(Action::ViewClippings as u8 == 11);
    assert!(Action::ToggleBookmark as u8 == 12);
    assert!(Action::Screenshot as u8 == 13);
    assert!(Action::Qr as u8 == 14);
    assert!(Action::Home as u8 == 15);
    assert!(Action::Sync as u8 == 16);
    assert!(Action::DeleteCache as u8 == 17);
    assert!(Action::Dictionary as u8 == 18);
};

#[cfg(test)]
mod tests {
    use super::*;
    struct Fake {
        fields: [u32; 34],
        locks: u32,
        text: bool,
        labels: bool,
        action: u32,
        calls: usize,
        qr_nonempty: bool,
    }
    impl Fake {
        fn services(&mut self) -> Services {
            Services {
                context: core::ptr::from_mut(self).cast(),
                read: Some(read),
                write: Some(write),
                command: Some(command),
            }
        }
    }
    unsafe extern "C" fn read(ctx: *mut c_void, field: u8) -> u32 {
        (&*ctx.cast::<Fake>()).fields[field as usize]
    }
    unsafe extern "C" fn write(ctx: *mut c_void, field: u8, value: u32) {
        (&mut *ctx.cast::<Fake>()).fields[field as usize] = value;
    }
    unsafe extern "C" fn command(ctx: *mut c_void, command: u8, _: i32, _: i32) -> u32 {
        let f = &mut *ctx.cast::<Fake>();
        f.calls += 1;
        match command {
            x if x == Command::Lock as u8 => {
                assert_eq!(f.locks, 0);
                f.locks += 1;
            }
            x if x == Command::Unlock as u8 => {
                assert_eq!(f.locks, 1);
                f.locks -= 1;
            }
            x if x == Command::PrepareQr as u8 => {
                assert!(!f.text);
                f.text = true;
                return u32::from(f.qr_nonempty);
            }
            x if x == Command::DropQr as u8 => {
                assert!(f.text);
                f.text = false;
            }
            x if x == Command::ShowQr as u8 => assert!(f.text),
            x if x == Command::PrepareLabels as u8 => {
                assert!(!f.labels);
                f.labels = true;
            }
            x if x == Command::DropLabels as u8 => {
                assert!(f.labels);
                f.labels = false;
            }
            x if x == Command::ShowAuto as u8
                || x == Command::AddOffLabel as u8
                || x == Command::AddRateLabel as u8 =>
            {
                assert!(f.labels)
            }
            x if x == Command::ActionAt as u8 => return f.action,
            x if x == Command::EraseItem as u8 => f.fields[Field::MoreCount as usize] -= 1,
            x if x == Command::DropSection as u8 => {
                assert_eq!(f.locks, 1);
                f.fields[Field::HasSection as usize] = 0;
            }
            x if x == Command::Update as u8 => assert!(!f.text),
            _ => {}
        }
        0
    }
    #[test]
    fn every_action_and_result_balances_native_resources() {
        for op in 0..13 {
            for action in -1..20 {
                for flags in 0..128 {
                    let mut f = Fake {
                        fields: [0; 34],
                        locks: 0,
                        text: false,
                        labels: false,
                        action: action as u32,
                        calls: 0,
                        qr_nonempty: flags & 64 != 0,
                    };
                    for (bit, field) in [
                        Field::HasSection,
                        Field::HasBook,
                        Field::UsesToolbar,
                        Field::HasToolbar,
                        Field::Cancelled,
                        Field::OverlayStored,
                        Field::StripsEnabled,
                    ]
                    .into_iter()
                    .enumerate()
                    {
                        f.fields[field as usize] = u32::from(flags & (1 << bit) != 0);
                    }
                    f.fields[Field::PageCount as usize] = 12;
                    f.fields[Field::MoreCount as usize] = 19;
                    f.fields[Field::RateCount as usize] = 5;
                    f.fields[Field::OrientationCount as usize] = 4;
                    f.fields[Field::ResultAction as usize] = action as u32;
                    let s = f.services();
                    unsafe {
                        run(&s, op, action);
                    }
                    assert_eq!(f.locks, 0);
                    assert!(!f.text && !f.labels);
                }
            }
        }
    }
    #[test]
    fn invalid_services_do_nothing() {
        unsafe {
            run(core::ptr::null(), 0, 0);
        }
        let mut f = Fake {
            fields: [0; 34],
            locks: 0,
            text: false,
            labels: false,
            action: 0,
            calls: 0,
            qr_nonempty: false,
        };
        let mut s = f.services();
        s.write = None;
        unsafe {
            run(&s, 0, 0);
        }
        assert_eq!(f.calls, 0);
    }
}
