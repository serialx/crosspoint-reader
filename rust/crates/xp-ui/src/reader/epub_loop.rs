//! Reader loop decisions. Resource callbacks execute outside the model critical section.
use super::input::Turn;
use super::services::Core;
use super::values;
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    RadioReleased = 0,
    AppliedOrientation = 1,
    LastRendered = 2,
    IdleSpine = 3,
    IdlePage = 4,
    Spine = 5,
    BuildWidth = 6,
    BuildHeight = 7,
    PartialFailed = 8,
    RecentsRemoved = 9,
    PendingReadMove = 10,
    ShowBookmark = 11,
    BookmarkTime = 12,
    ShowDictionary = 13,
    DictionaryTime = 14,
    ShowFont = 15,
    FontTime = 16,
    Overlay = 17,
    LastTurn = 18,
    AutoActive = 19,
    PendingTurn = 20,
    PendingTouch = 21,
    TurnDuration = 22,
    ShowClip = 23,
    ClipTime = 24,
    FootnoteDepth = 25,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    LoadPopup = 0,
    HasEpub = 1,
    HasSection = 2,
    Building = 3,
    Framebuffer = 4,
    Now = 5,
    FreeHeap = 6,
    MaxBlock = 7,
    Page = 8,
    PageCount = 9,
    Partial = 10,
    RadioHeap = 11,
    SpineCount = 12,
    RemoveRecents = 13,
    MoveFinished = 14,
    InReadFolder = 15,
    Orientation = 16,
    ToolbarMenu = 17,
    TouchControls = 18,
    HomeAction = 19,
    TouchMenu = 20,
    ConfirmRelease = 21,
    BackRelease = 22,
    Busy = 23,
    EndMenuActive = 24,
    ConfirmThreshold = 25,
    LongMenuFunction = 26,
    HasTouch = 27,
    Held = 28,
    ShortPower = 29,
    PowerRelease = 30,
    DownRelease = 31,
    LongButtonBehavior = 32,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    PopupInput = 0,
    Finish = 1,
    Remember = 2,
    TryLock = 3,
    Unlock = 4,
    RadioAfter = 5,
    RepaintLinkNote = 6,
    Update = 7,
    Orientation = 8,
    LoadPage = 9,
    FontCache = 10,
    PrewarmBegin = 11,
    PrewarmPage = 12,
    PrewarmScanEnd = 13,
    PrewarmLog = 14,
    PrewarmDrop = 15,
    PageDrop = 16,
    StartBuild = 17,
    BuildFailedLog = 18,
    BuildStartedLog = 19,
    ClearEnd = 20,
    RemoveRecent = 21,
    AddRecent = 22,
    OverlayInput = 23,
    DiscardOverlay = 24,
    Clip = 25,
    Turn = 26,
    NoteTurn = 27,
    Haptic = 28,
    EndMenu = 29,
    ConfirmLong = 30,
    Bookmark = 31,
    Sync = 32,
    Dictionary = 33,
    RestorePosition = 34,
    Footnotes = 35,
    OpenOverlay = 36,
    OpenMenu = 37,
    MatchLink = 38,
    NavigateLink = 39,
    BackNavigation = 40,
    BuildTick = 41,
    EndTurn = 42,
    Skip = 43,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Point {
    Long = 0,
    Tap = 1,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub model_read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub model_write: Option<unsafe extern "C" fn(*mut c_void, u8, u32)>,
    pub query: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
    pub touch: Option<unsafe extern "C" fn(*mut c_void) -> Turn>,
    pub buttons: Option<unsafe extern "C" fn(*mut c_void) -> Turn>,
    pub point: Option<unsafe extern "C" fn(*mut c_void, u8, *mut i32, *mut i32) -> u8>,
}
struct Engine {
    core: Core,
    query: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    touch: unsafe extern "C" fn(*mut c_void) -> Turn,
    buttons: unsafe extern "C" fn(*mut c_void) -> Turn,
    point: unsafe extern "C" fn(*mut c_void, u8, *mut i32, *mut i32) -> u8,
}
impl Engine {
    fn new(s: Services) -> Option<Self> {
        Some(Self {
            core: Core {
                context: s.context,
                read: s.model_read?,
                write: s.model_write?,
                command: s.command?,
            },
            query: s.query?,
            touch: s.touch?,
            buttons: s.buttons?,
            point: s.point?,
        })
    }

    fn f(&self, field: Field) -> u32 {
        self.core.read(field as u8)
    }
    fn set(&self, field: Field, value: u32) {
        self.core.write(field as u8, value);
    }
    fn q(&self, query: Query) -> u32 {
        unsafe { (self.query)(self.core.context, query as u8) }
    }
    fn c(&self, command: Command, a: i32, b: i32) -> u32 {
        self.core.command(command as u8, a, b)
    }
    fn op(&self, command: Command) -> u32 {
        self.core.op(command as u8)
    }
    fn update(&self) {
        self.op(Command::Update);
    }
    fn point(&self, point: Point) -> Option<(i32, i32)> {
        let (mut x, mut y) = (0, 0);
        (unsafe { (self.point)(self.core.context, point as u8, &mut x, &mut y) } != 0)
            .then_some((x, y))
    }
    fn expired(&self, shown: Field, started: Field) {
        if self.f(shown) != 0 && self.q(Query::Now).wrapping_sub(self.f(started)) >= 2500 {
            self.set(shown, 0);
            self.update();
        }
    }
    fn run(&self) {
        use Command as C;
        use Field as F;
        use Query as Q;
        if self.q(Q::LoadPopup) != 0 {
            self.op(C::PopupInput);
            return;
        }
        if self.q(Q::HasEpub) == 0 {
            self.op(C::Finish);
            return;
        }
        self.op(C::Remember);
        if self.f(F::RadioReleased) != 0 {
            if self.op(C::TryLock) != 0 && !(self.q(Q::HasSection) != 0 && self.q(Q::Building) != 0)
            {
                self.set(F::RadioReleased, 0);
                self.op(C::RadioAfter);
            }
            self.op(C::Unlock);
        }
        if self.op(C::TryLock) != 0 && self.op(C::RepaintLinkNote) != 0 {
            self.update();
        }
        self.op(C::Unlock);
        if self.f(F::AppliedOrientation) != self.q(Q::Orientation) {
            self.c(C::Orientation, self.q(Q::Orientation) as i32, 0);
            self.update();
            return;
        }
        if self.op(C::TryLock) != 0
            && self.q(Q::HasSection) != 0
            && self.q(Q::Building) == 0
            && self.q(Q::Framebuffer) != 0
            && self.f(F::LastRendered) != 0
            && self.q(Q::Now).wrapping_sub(self.f(F::LastRendered)) > 400
            && self.q(Q::FreeHeap) > 24 * 1024
            && self.q(Q::MaxBlock) > 16 * 1024
            && (self.f(F::IdleSpine) != self.f(F::Spine) || self.f(F::IdlePage) != self.q(Q::Page))
        {
            self.set(F::IdleSpine, self.f(F::Spine));
            self.set(F::IdlePage, self.q(Q::Page));
            let next = (self.q(Q::Page) as i32).wrapping_add(1);
            if next < (self.q(Q::PageCount) as i32) && self.c(C::LoadPage, next, 0) != 0 {
                if self.op(C::FontCache) != 0 {
                    let started = self.q(Q::Now);
                    self.op(C::PrewarmBegin);
                    self.op(C::PrewarmPage);
                    self.op(C::PrewarmScanEnd);
                    self.c(C::PrewarmLog, next, started as i32);
                    self.op(C::PrewarmDrop);
                }
                self.op(C::PageDrop);
            }
        }
        self.op(C::Unlock);
        if self.op(C::TryLock) != 0
            && self.q(Q::HasSection) != 0
            && self.q(Q::Building) == 0
            && self.q(Q::Partial) != 0
            && (self.f(F::BuildWidth) as i32) > 0
            && self.f(F::PartialFailed) == 0
            && self.q(Q::RadioHeap) == 0
            && (self.q(Q::Page) as i32).wrapping_add(15) >= (self.q(Q::PageCount) as i32)
        {
            if self.op(C::StartBuild) == 0 {
                self.set(F::PartialFailed, 1);
                self.op(C::BuildFailedLog);
            } else {
                self.op(C::BuildStartedLog);
            }
        }
        self.op(C::Unlock);
        let at_end = (self.f(F::Spine) as i32) > 0
            && (self.f(F::Spine) as i32) >= (self.q(Q::SpineCount) as i32);
        self.op(C::ClearEnd);
        if self.q(Q::RemoveRecents) != 0 {
            if at_end && self.f(F::RecentsRemoved) == 0 {
                self.set(F::RecentsRemoved, self.op(C::RemoveRecent));
            } else if !at_end && self.f(F::RecentsRemoved) != 0 {
                self.op(C::AddRecent);
                self.set(F::RecentsRemoved, 0);
            }
        }
        self.set(
            F::PendingReadMove,
            u32::from(at_end && self.q(Q::MoveFinished) != 0 && self.q(Q::InReadFolder) == 0),
        );
        let touch = unsafe { (self.touch)(self.core.context) };
        self.expired(F::ShowBookmark, F::BookmarkTime);
        self.expired(F::ShowDictionary, F::DictionaryTime);
        self.expired(F::ShowFont, F::FontTime);
        if self.f(F::Overlay) != values::OVERLAY_NONE {
            if self.q(Q::ToolbarMenu) != 0 {
                self.set(F::LastTurn, self.q(Q::Now));
                self.op(C::OverlayInput);
                return;
            }
            self.set(F::Overlay, values::OVERLAY_NONE);
            self.op(C::DiscardOverlay);
            self.update();
            return;
        }
        if !at_end && self.q(Q::TouchControls) != 0 {
            if let Some((x, y)) = self.point(Point::Long) {
                self.set(F::AutoActive, 0);
                self.set(F::PendingTurn, 0);
                self.c(C::Clip, x, y);
                return;
            }
        }
        if matches!(
            self.q(Q::HomeAction),
            values::HOME_FOOTNOTES
                | values::HOME_SYNC
                | values::HOME_BOOKMARK
                | values::HOME_DICTIONARY
                | values::HOME_READER_MENU
                | values::HOME_CREATE_CLIPPING
        ) {
            self.set(F::AutoActive, 0);
        }
        if self.f(F::AutoActive) != 0 {
            let touch_stops = self.q(Q::TouchMenu) != 0;
            if self.q(Q::ConfirmRelease) != 0 || self.q(Q::BackRelease) != 0 || touch_stops {
                self.set(F::AutoActive, 0);
                if touch_stops {
                    self.op(C::Haptic);
                }
                self.update();
                return;
            }
            if self.q(Q::HasSection) == 0 {
                self.update();
                return;
            }
            if self.q(Q::Busy) != 0 {
                self.set(F::LastTurn, self.q(Q::Now));
                return;
            }
            if self.q(Q::Now).wrapping_sub(self.f(F::LastTurn)) >= self.f(F::TurnDuration) {
                let succeeded = self.c(C::Turn, 1, 0);
                self.c(C::NoteTurn, 1, succeeded as i32);
                self.update();
                return;
            }
        }
        self.expired(F::ShowClip, F::ClipTime);
        if self.op(C::EndMenu) != 0 {
            return;
        }
        let end_menu = self.q(Q::EndMenuActive) != 0;
        let threshold = self.q(Q::ConfirmThreshold);
        let long = !end_menu && threshold != 0 && self.c(C::ConfirmLong, threshold as i32, 0) != 0;
        let released = self.q(Q::ConfirmRelease) != 0;
        if long {
            match self.q(Q::LongMenuFunction) {
                values::LONG_MENU_BOOKMARK => {
                    self.op(C::Bookmark);
                    self.set(F::ShowBookmark, 1);
                    self.set(F::BookmarkTime, self.q(Q::Now));
                    self.update();
                }
                values::LONG_MENU_SYNC => {
                    if self.op(C::Sync) != 0 {
                        return;
                    }
                }
                values::LONG_MENU_DICTIONARY => {
                    self.op(C::Dictionary);
                    return;
                }
                values::LONG_MENU_CREATE_CLIPPING => {
                    self.c(C::Clip, -1, -1);
                    return;
                }
                values::LONG_MENU_FOOTNOTES => {
                    if self.f(F::FootnoteDepth) > 0 {
                        self.op(C::RestorePosition);
                    } else {
                        self.op(C::Footnotes);
                    }
                    return;
                }
                _ => {}
            }
        }
        if !end_menu {
            match self.q(Q::HomeAction) {
                values::HOME_BOOKMARK => {
                    if self.f(F::ShowBookmark) == 0 {
                        self.op(C::Bookmark);
                        self.set(F::ShowBookmark, 1);
                        self.set(F::BookmarkTime, self.q(Q::Now));
                        self.update();
                    }
                    return;
                }
                values::HOME_SYNC => {
                    self.op(C::Sync);
                    return;
                }
                values::HOME_DICTIONARY => {
                    if self.f(F::ShowDictionary) == 0 {
                        self.op(C::Dictionary);
                    }
                    return;
                }
                values::HOME_CREATE_CLIPPING => {
                    self.c(C::Clip, -1, -1);
                    return;
                }
                values::HOME_READER_MENU => {
                    if self.q(Q::ToolbarMenu) != 0 && self.q(Q::HasSection) != 0 {
                        self.c(C::OpenOverlay, values::OVERLAY_TOOLBAR as i32, 0);
                    } else {
                        self.op(C::OpenMenu);
                    }
                    return;
                }
                _ => {}
            }
        }
        if !at_end && self.q(Q::TouchControls) != 0 && self.q(Q::HasTouch) != 0 {
            if let Some((x, y)) = self.point(Point::Tap) {
                if self.c(C::MatchLink, x, y) != 0 {
                    self.op(C::NavigateLink);
                    self.op(C::Haptic);
                    return;
                }
            }
        }
        let touch_menu = self.q(Q::TouchMenu) != 0;
        if released || touch_menu {
            if touch_menu {
                self.op(C::Haptic);
            }
            if self.q(Q::ToolbarMenu) != 0 && self.q(Q::HasSection) != 0 {
                self.set(F::PendingTurn, 0);
                self.c(C::OpenOverlay, values::OVERLAY_TOOLBAR as i32, 0);
            } else {
                self.op(C::OpenMenu);
            }
            return;
        }
        if self.f(F::FootnoteDepth) > 0 && self.q(Q::BackRelease) != 0 && self.q(Q::Held) < 1000 {
            self.op(C::RestorePosition);
            return;
        }
        if self.op(C::BackNavigation) != 0 {
            return;
        }
        if self.q(Q::ShortPower) == values::POWER_CREATE_CLIPPING
            && self.q(Q::PowerRelease) != 0
            && self.q(Q::DownRelease) == 0
        {
            self.c(C::Clip, -1, -1);
            return;
        }
        if (!end_menu && self.q(Q::HomeAction) == values::HOME_FOOTNOTES)
            || (self.q(Q::ShortPower) == values::POWER_FOOTNOTES
                && self.q(Q::PowerRelease) != 0
                && self.q(Q::DownRelease) == 0)
        {
            if self.f(F::FootnoteDepth) > 0 {
                self.op(C::RestorePosition);
            } else {
                self.op(C::Footnotes);
            }
            return;
        }
        let guard = self.q(Q::Busy) != 0 || self.q(Q::Now).wrapping_sub(self.f(F::LastTurn)) < 200;
        if self.f(F::PendingTurn) != 0 && !guard {
            if self.q(Q::HasSection) == 0 {
                self.set(F::PendingTurn, 0);
                return;
            }
            let forward = (self.f(F::PendingTurn) as i32) > 0;
            self.set(F::PendingTurn, 0);
            let succeeded = self.c(C::Turn, i32::from(forward), 0);
            self.c(C::NoteTurn, i32::from(forward), succeeded as i32);
            if succeeded != 0 && self.f(F::PendingTouch) != 0 {
                self.op(C::Haptic);
            }
            self.set(F::PendingTouch, 0);
            self.update();
            return;
        }
        let buttons = unsafe { (self.buttons)(self.core.context) };
        let prev = buttons.prev != 0 || touch.prev != 0;
        let next = buttons.next != 0 || touch.next != 0;
        let touched = touch.prev != 0 || touch.next != 0;
        if !prev && !next {
            self.op(C::BuildTick);
            return;
        }
        if self.c(C::EndTurn, i32::from(prev), i32::from(next)) != 0 {
            return;
        }
        if self.q(Q::PowerRelease) != 0 && self.q(Q::DownRelease) != 0 {
            return;
        }
        let held = if touched {
            touch.held_ms
        } else {
            self.q(Q::Held)
        };
        let long = buttons.from_tilt == 0 && held >= 700;
        if long && self.q(Q::LongButtonBehavior) == values::LONG_BUTTON_SKIP {
            let succeeded = self.c(C::Skip, if next { 1 } else { -1 }, 0);
            self.c(C::NoteTurn, 0, succeeded as i32);
            if succeeded != 0 && touched {
                self.c(C::Haptic, 1, 0);
            }
            self.update();
            return;
        }
        if long && self.q(Q::LongButtonBehavior) == values::LONG_BUTTON_ORIENTATION {
            let old = self.q(Q::Orientation);
            let new = if next { (old + 3) % 4 } else { (old + 1) % 4 };
            self.c(C::Orientation, new as i32, 0);
            if touched {
                self.c(C::Haptic, 1, 0);
            }
            self.update();
            return;
        }
        if self.q(Q::HasSection) == 0 {
            self.update();
            return;
        }
        if guard {
            self.set(F::PendingTurn, if prev { u32::MAX } else { 1 });
            self.set(F::PendingTouch, u32::from(touched));
            return;
        }
        let succeeded = self.c(C::Turn, i32::from(!prev), 0);
        self.c(C::NoteTurn, i32::from(!prev), succeeded as i32);
        if succeeded != 0 && touched {
            self.op(C::Haptic);
        }
        self.update();
    }
}
/// # Safety
/// The context and synchronous callbacks remain valid and do not retain pointer arguments.
pub unsafe fn run(services: Services) {
    if let Some(engine) = Engine::new(services) {
        engine.run();
    }
}

const _: () = {
    assert!(Field::RadioReleased as u8 == 0);
    assert!(Field::AppliedOrientation as u8 == 1);
    assert!(Field::LastRendered as u8 == 2);
    assert!(Field::IdleSpine as u8 == 3);
    assert!(Field::IdlePage as u8 == 4);
    assert!(Field::Spine as u8 == 5);
    assert!(Field::BuildWidth as u8 == 6);
    assert!(Field::BuildHeight as u8 == 7);
    assert!(Field::PartialFailed as u8 == 8);
    assert!(Field::RecentsRemoved as u8 == 9);
    assert!(Field::PendingReadMove as u8 == 10);
    assert!(Field::ShowBookmark as u8 == 11);
    assert!(Field::BookmarkTime as u8 == 12);
    assert!(Field::ShowDictionary as u8 == 13);
    assert!(Field::DictionaryTime as u8 == 14);
    assert!(Field::ShowFont as u8 == 15);
    assert!(Field::FontTime as u8 == 16);
    assert!(Field::Overlay as u8 == 17);
    assert!(Field::LastTurn as u8 == 18);
    assert!(Field::AutoActive as u8 == 19);
    assert!(Field::PendingTurn as u8 == 20);
    assert!(Field::PendingTouch as u8 == 21);
    assert!(Field::TurnDuration as u8 == 22);
    assert!(Field::ShowClip as u8 == 23);
    assert!(Field::ClipTime as u8 == 24);
    assert!(Field::FootnoteDepth as u8 == 25);
    assert!(Query::LoadPopup as u8 == 0);
    assert!(Query::HasEpub as u8 == 1);
    assert!(Query::HasSection as u8 == 2);
    assert!(Query::Building as u8 == 3);
    assert!(Query::Framebuffer as u8 == 4);
    assert!(Query::Now as u8 == 5);
    assert!(Query::FreeHeap as u8 == 6);
    assert!(Query::MaxBlock as u8 == 7);
    assert!(Query::Page as u8 == 8);
    assert!(Query::PageCount as u8 == 9);
    assert!(Query::Partial as u8 == 10);
    assert!(Query::RadioHeap as u8 == 11);
    assert!(Query::SpineCount as u8 == 12);
    assert!(Query::RemoveRecents as u8 == 13);
    assert!(Query::MoveFinished as u8 == 14);
    assert!(Query::InReadFolder as u8 == 15);
    assert!(Query::Orientation as u8 == 16);
    assert!(Query::ToolbarMenu as u8 == 17);
    assert!(Query::TouchControls as u8 == 18);
    assert!(Query::HomeAction as u8 == 19);
    assert!(Query::TouchMenu as u8 == 20);
    assert!(Query::ConfirmRelease as u8 == 21);
    assert!(Query::BackRelease as u8 == 22);
    assert!(Query::Busy as u8 == 23);
    assert!(Query::EndMenuActive as u8 == 24);
    assert!(Query::ConfirmThreshold as u8 == 25);
    assert!(Query::LongMenuFunction as u8 == 26);
    assert!(Query::HasTouch as u8 == 27);
    assert!(Query::Held as u8 == 28);
    assert!(Query::ShortPower as u8 == 29);
    assert!(Query::PowerRelease as u8 == 30);
    assert!(Query::DownRelease as u8 == 31);
    assert!(Query::LongButtonBehavior as u8 == 32);
    assert!(Command::PopupInput as u8 == 0);
    assert!(Command::Finish as u8 == 1);
    assert!(Command::Remember as u8 == 2);
    assert!(Command::TryLock as u8 == 3);
    assert!(Command::Unlock as u8 == 4);
    assert!(Command::RadioAfter as u8 == 5);
    assert!(Command::RepaintLinkNote as u8 == 6);
    assert!(Command::Update as u8 == 7);
    assert!(Command::Orientation as u8 == 8);
    assert!(Command::LoadPage as u8 == 9);
    assert!(Command::FontCache as u8 == 10);
    assert!(Command::PrewarmBegin as u8 == 11);
    assert!(Command::PrewarmPage as u8 == 12);
    assert!(Command::PrewarmScanEnd as u8 == 13);
    assert!(Command::PrewarmLog as u8 == 14);
    assert!(Command::PrewarmDrop as u8 == 15);
    assert!(Command::PageDrop as u8 == 16);
    assert!(Command::StartBuild as u8 == 17);
    assert!(Command::BuildFailedLog as u8 == 18);
    assert!(Command::BuildStartedLog as u8 == 19);
    assert!(Command::ClearEnd as u8 == 20);
    assert!(Command::RemoveRecent as u8 == 21);
    assert!(Command::AddRecent as u8 == 22);
    assert!(Command::OverlayInput as u8 == 23);
    assert!(Command::DiscardOverlay as u8 == 24);
    assert!(Command::Clip as u8 == 25);
    assert!(Command::Turn as u8 == 26);
    assert!(Command::NoteTurn as u8 == 27);
    assert!(Command::Haptic as u8 == 28);
    assert!(Command::EndMenu as u8 == 29);
    assert!(Command::ConfirmLong as u8 == 30);
    assert!(Command::Bookmark as u8 == 31);
    assert!(Command::Sync as u8 == 32);
    assert!(Command::Dictionary as u8 == 33);
    assert!(Command::RestorePosition as u8 == 34);
    assert!(Command::Footnotes as u8 == 35);
    assert!(Command::OpenOverlay as u8 == 36);
    assert!(Command::OpenMenu as u8 == 37);
    assert!(Command::MatchLink as u8 == 38);
    assert!(Command::NavigateLink as u8 == 39);
    assert!(Command::BackNavigation as u8 == 40);
    assert!(Command::BuildTick as u8 == 41);
    assert!(Command::EndTurn as u8 == 42);
    assert!(Command::Skip as u8 == 43);
    assert!(Point::Long as u8 == 0);
    assert!(Point::Tap as u8 == 1);
};

const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<Services>() == size_of::<[usize; 8]>());
    assert!(align_of::<Services>() == align_of::<usize>());
    assert!(offset_of!(Services, context) == 0);
    assert!(offset_of!(Services, model_read) == size_of::<usize>());
    assert!(offset_of!(Services, model_write) == 2 * size_of::<usize>());
    assert!(offset_of!(Services, query) == 3 * size_of::<usize>());
    assert!(offset_of!(Services, command) == 4 * size_of::<usize>());
    assert!(offset_of!(Services, touch) == 5 * size_of::<usize>());
    assert!(offset_of!(Services, buttons) == 6 * size_of::<usize>());
    assert!(offset_of!(Services, point) == 7 * size_of::<usize>());
};
