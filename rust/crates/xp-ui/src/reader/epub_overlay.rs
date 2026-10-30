//! Reader toolbar parent policy, backed by synchronous resource callbacks.
use super::services::{Core, Guard};
use crate::model::{BACK, CONFIRM, DOWN, LEFT, RIGHT, UP};
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Event {
    None = 0,
    Dismiss = 1,
    Tool = 2,
    PrevChapter = 3,
    NextChapter = 4,
    Scrub = 5,
    Row = 6,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Swipe {
    None = 0,
    Left = 1,
    Right = 2,
    Up = 3,
    Down = 4,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Mode {
    None = 0,
    Toolbar = 1,
    Contents = 2,
    Text = 3,
    More = 4,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    Overlay = 0,
    Spine = 1,
    NextPage = 2,
    Focus = 3,
    Index = 4,
    Cursor = 5,
    Hold = 6,
    Stored = 7,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    HasToolbar = 0,
    PopupActive = 1,
    SpineCount = 2,
    TocCount = 3,
    MoreCount = 4,
    Rows = 5,
    Routed = 6,
    Value = 7,
    Permille = 8,
    X = 9,
    Y = 10,
    Width = 11,
    Height = 12,
    FocusReading = 13,
    Pressed = 14,
    Released = 15,
    HasSection = 16,
    PageStale = 17,
    Touch = 18,
    CurrentToc = 19,
    IsA4 = 20,
    IsXteink = 21,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    PopupInput = 0,
    Lock = 1,
    Unlock = 2,
    Settle = 3,
    PaintPopup = 4,
    Restore = 5,
    Store = 6,
    Render = 7,
    Push = 8,
    Update = 9,
    Close = 10,
    Open = 11,
    ClearDeferred = 12,
    DropSection = 13,
    Route = 14,
    DismissPopup = 15,
    Discard = 16,
    StartPicker = 17,
    ApplyText = 18,
    ToolbarBegin = 19,
    SetFocusReading = 20,
    ApplyLive = 21,
    TextPopup = 22,
    More = 23,
    TocItem = 24,
    TocAnchor = 25,
    TocDrop = 26,
    Scroll = 27,
    TakeSwipe = 28,
    SampleButtons = 29,
    TakeHeldMs = 30,
    ResetHome = 31,
    CreateToolbar = 32,
    ResetNavigation = 33,
    BuildMore = 34,
    DropToolbar = 35,
    ForceCounter = 36,
    DisplayFast = 37,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Input = 0,
    PopupChanged = 1,
    PickerResult = 2,
    OpenNone = 3,
    OpenToolbar = 4,
    OpenContents = 5,
    OpenText = 6,
    OpenMore = 7,
    Close = 8,
    Cancel = 9,
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
pub const TEXT_ROWS: i32 = 5;
pub const HOLD_MS: u32 = 1500;
pub const HOLD_ROWS: i32 = 10;

const SWIPE_UP: u32 = Swipe::Up as u32;
const SWIPE_DOWN: u32 = Swipe::Down as u32;
const NONE: u32 = Mode::None as u32;
const TOOLBAR: u32 = Mode::Toolbar as u32;
const CONTENTS: u32 = Mode::Contents as u32;
const TEXT: u32 = Mode::Text as u32;
const MORE: u32 = Mode::More as u32;
impl Engine {
    fn open(&self, target: u32) {
        use Command as C;
        use Field as F;
        self.op(C::ResetHome);
        let previous = self.f(F::Overlay);
        self.set(F::Overlay, target);
        if self.q(Query::HasToolbar) == 0 {
            self.op(C::CreateToolbar);
        }
        if previous == NONE {
            self.op(C::ToolbarBegin);
        }
        self.set(F::Cursor, u32::from(self.q(Query::Touch) == 0));
        match target {
            TOOLBAR => self.set(F::Focus, 0),
            CONTENTS => {
                let _lock = self.lock();
                let index = self.q(Query::CurrentToc);
                self.set(F::Index, index);
                self.c(C::ResetNavigation, index as i32, index as i32);
            }
            TEXT | MORE => {
                self.set(F::Index, 0);
                if target == MORE {
                    self.op(C::BuildMore);
                }
                self.op(C::ResetNavigation);
            }
            _ => {}
        }
        self.set(F::Hold, 0);
        if self.q(Query::HasSection) != 0 && self.q(Query::PageStale) == 0 {
            let _lock = self.lock();
            self.op(C::Settle);
            if previous == NONE {
                self.set(F::Stored, self.op(C::Store));
            } else if self.f(F::Stored) != 0 {
                self.op(C::Restore);
                self.set(F::Stored, self.op(C::Store));
            }
            self.op(C::Render);
            self.op(C::Push);
        } else {
            self.op(C::Update);
        }
    }
    fn close(&self) {
        use Command as C;
        use Field as F;
        self.op(C::ResetHome);
        self.set(F::Overlay, NONE);
        self.op(C::DismissPopup);
        self.op(C::DropToolbar);
        if self.q(Query::IsA4) != 0 {
            self.c(C::ForceCounter, 1, 0);
        }
        if self.q(Query::IsXteink) == 0 && self.f(F::Stored) != 0 {
            let _lock = self.lock();
            self.op(C::Settle);
            self.op(C::Restore);
            self.set(F::Stored, 0);
            self.op(C::DisplayFast);
        } else {
            self.op(C::Discard);
            self.op(C::Update);
        }
    }
    fn new(s: Services) -> Option<Self> {
        Some(Self {
            core: Core {
                context: s.context,
                read: s.read?,
                write: s.write?,
                command: s.command?,
            },
            query: s.query?,
        })
    }
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
    fn lock(&self) -> Guard<'_> {
        self.op(Command::Lock);
        self.core.guard(Command::Unlock as u8)
    }
    fn toc(&self, index: i32) -> (Guard<'_>, i32) {
        let spine = self.c(Command::TocItem, index, 0) as i32;
        (self.core.guard(Command::TocDrop as u8), spine)
    }
    fn button(&self, bit: u32, released: bool) -> bool {
        if released {
            self.c(Command::SampleButtons, 0, bit as i32);
            self.q(Query::Released) & bit != 0
        } else {
            self.c(Command::SampleButtons, bit as i32, 0);
            self.q(Query::Pressed) & bit != 0
        }
    }
    fn tool(tool: i32) -> u32 {
        if tool == 0 {
            CONTENTS
        } else if tool == 1 {
            TEXT
        } else {
            MORE
        }
    }
    fn redraw(&self) {
        let lock = self.lock();
        self.op(Command::Settle);
        self.op(Command::Render);
        self.op(Command::Push);
        drop(lock);
    }
    fn goto_spine(&self, target: i32) {
        let count = self.q(Query::SpineCount) as i32;
        let target = target.clamp(0, count - 1);
        if target != self.f(Field::Spine) as i32 {
            let lock = self.lock();
            self.op(Command::ClearDeferred);
            self.set(Field::NextPage, 0);
            self.set(Field::Spine, target as u32);
            self.op(Command::DropSection);
            drop(lock);
        }
        self.op(Command::Update);
    }
    fn popup_changed(&self) {
        if self.q(Query::PopupActive) != 0 {
            self.op(Command::PaintPopup);
            return;
        }
        let lock = self.lock();
        self.op(Command::Settle);
        if self.f(Field::Stored) != 0 {
            self.op(Command::Restore);
            self.set(Field::Stored, self.op(Command::Store));
            self.op(Command::Render);
            self.op(Command::Push);
        } else {
            self.op(Command::Update);
        }
        drop(lock);
    }
    fn picker_result(&self) {
        self.op(Command::ApplyText);
        self.set(Field::Overlay, TEXT);
        self.set(Field::Index, 0);
        if self.q(Query::HasToolbar) != 0 {
            self.op(Command::ToolbarBegin);
        }
        self.op(Command::Update);
    }
    fn activate(&self, count: i32) {
        let index = self.f(Field::Index) as i32;
        if index < 0 || index >= count {
            return;
        }
        match self.f(Field::Overlay) {
            TEXT => {
                if index == 0 {
                    self.set(Field::Overlay, NONE);
                    self.op(Command::DismissPopup);
                    self.op(Command::Discard);
                    let lock = self.lock();
                    self.op(Command::Settle);
                    drop(lock);
                    self.op(Command::StartPicker);
                } else if index == 4 {
                    let enabled = self.q(Query::FocusReading) == 0;
                    self.c(Command::SetFocusReading, i32::from(enabled), 0);
                    self.op(Command::ApplyLive);
                } else {
                    self.c(Command::TextPopup, index, 0);
                }
            }
            CONTENTS => {
                let (toc, spine) = self.toc(index);
                if spine != -1 {
                    let lock = self.lock();
                    self.op(Command::ClearDeferred);
                    self.set(Field::Spine, spine as u32);
                    self.op(Command::TocAnchor);
                    self.set(Field::NextPage, 0);
                    self.op(Command::DropSection);
                    drop(lock);
                }
                self.set(Field::Overlay, NONE);
                self.op(Command::Discard);
                self.op(Command::Update);
                drop(toc);
            }
            MORE => {
                self.c(Command::More, index, 0);
            }
            _ => {}
        }
    }
    fn dismiss(&self) {
        self.set(Field::Overlay, TOOLBAR);
        if self.f(Field::Stored) != 0 {
            let lock = self.lock();
            self.op(Command::Settle);
            self.op(Command::Restore);
            self.set(Field::Stored, self.op(Command::Store));
            drop(lock);
            self.redraw();
            return;
        }
        self.op(Command::Update);
    }
    fn page(&self, direction: i32, count: i32, rows: i32) {
        if count <= 0 {
            return;
        }
        let moved = self.c(Command::Scroll, direction * rows, count) != 0;
        if self.f(Field::Cursor) != 0 {
            let index = (self.f(Field::Index) as i32 + direction * rows).clamp(0, count - 1);
            self.set(Field::Index, index as u32);
            self.redraw();
            return;
        }
        if moved {
            self.redraw();
        }
    }
    fn cancel(&self) {
        if self.f(Field::Overlay) == NONE || self.q(Query::HasToolbar) == 0 {
            return;
        }
        if self.q(Query::PopupActive) != 0 {
            self.op(Command::DismissPopup);
            self.popup_changed();
        } else if self.f(Field::Overlay) == TOOLBAR {
            self.op(Command::Close);
        } else {
            self.dismiss();
        }
    }
    fn input(&self) {
        use Command as C;
        use Field as F;
        use Query as Q;
        if self.q(Q::HasToolbar) == 0 {
            return;
        }
        if self.q(Q::PopupActive) != 0 {
            self.op(C::PopupInput);
            return;
        }
        let routed = self.op(C::Route);
        if self.f(F::Overlay) == TOOLBAR {
            match routed {
                x if x == Event::Dismiss as u32 => {
                    self.op(C::Close);
                    return;
                }
                x if x == Event::Tool as u32 => {
                    let value = self.q(Q::Value) as i32;
                    self.set(F::Focus, value as u32);
                    self.c(C::Open, Self::tool(value) as i32, 0);
                    return;
                }
                x if x == Event::PrevChapter as u32 => {
                    self.goto_spine(self.f(F::Spine) as i32 - 1);
                    return;
                }
                x if x == Event::NextChapter as u32 => {
                    self.goto_spine(self.f(F::Spine) as i32 + 1);
                    return;
                }
                x if x == Event::Scrub as u32 => {
                    let permille = self.q(Q::Permille) as i32;
                    let count = self.q(Q::SpineCount);
                    let target =
                        ((permille as f32 / 1000.0) * (count as i32 - 1) as f32 + 0.5) as i32;
                    self.goto_spine(target);
                    return;
                }
                _ => {}
            }
            if self.q(Q::Routed) != 0 {
                return;
            }
            if self.button(BACK, true) {
                self.op(C::Close);
                return;
            }
            if self.button(LEFT, true) {
                self.set(F::Focus, ((self.f(F::Focus) as i32 + 2) % 3) as u32);
                self.set(F::Cursor, 1);
                self.redraw();
                return;
            }
            if self.button(RIGHT, true) {
                self.set(F::Focus, ((self.f(F::Focus) as i32 + 1) % 3) as u32);
                self.set(F::Cursor, 1);
                self.redraw();
                return;
            }
            if self.button(CONFIRM, true) {
                self.c(C::Open, Self::tool(self.f(F::Focus) as i32) as i32, 0);
                return;
            }
            let prev = self.button(UP, true);
            let next = self.button(DOWN, true);
            if prev || next {
                self.goto_spine(self.f(F::Spine) as i32 + if next { 1 } else { -1 });
            }
            return;
        }
        let count = match self.f(F::Overlay) {
            CONTENTS => self.q(Q::TocCount) as i32,
            TEXT => TEXT_ROWS,
            _ => self.q(Q::MoreCount) as i32,
        };
        let rows = (self.q(Q::Rows) as i32).max(1);
        match routed {
            x if x == Event::Dismiss as u32 => {
                self.dismiss();
                return;
            }
            x if x == Event::Tool as u32 => {
                let value = self.q(Q::Value) as i32;
                let target = Self::tool(value);
                if target != self.f(F::Overlay) {
                    self.set(F::Focus, value as u32);
                    self.c(C::Open, target as i32, 0);
                }
                return;
            }
            x if x == Event::Row as u32 => {
                if self.q(Q::X) as i32 >= self.q(Q::Width) as i32 - 44 {
                    let next = self.q(Q::Y) as i32
                        >= self.q(Q::Height) as i32 - (self.q(Q::Height) as i32 * 62) / 200;
                    self.page(if next { 1 } else { -1 }, count, rows);
                    return;
                }
                self.set(F::Index, self.q(Q::Value));
                self.set(F::Cursor, 0);
                self.activate(count);
                return;
            }
            _ => {}
        }
        let swipe = self.op(C::TakeSwipe);
        if swipe == SWIPE_UP || swipe == SWIPE_DOWN {
            self.page(if swipe == SWIPE_UP { 1 } else { -1 }, count, rows);
            return;
        }
        if self.q(Q::Routed) != 0 {
            return;
        }
        if self.button(BACK, true) {
            self.dismiss();
            return;
        }
        if self.button(CONFIRM, true) {
            self.activate(count);
            return;
        }
        if count > 0 {
            let up = self.button(UP, false) || self.button(LEFT, false);
            let down = self.button(DOWN, false) || self.button(RIGHT, false);
            if self.f(F::Hold) == 0 && (up || down) && self.op(C::TakeHeldMs) >= HOLD_MS {
                self.set(
                    F::Index,
                    (self.f(F::Index) as i32 + if down { HOLD_ROWS } else { -HOLD_ROWS })
                        .clamp(0, count - 1) as u32,
                );
                self.set(F::Hold, 1);
                self.set(F::Cursor, 1);
                self.redraw();
                return;
            }
            let released_up = self.button(UP, true) || self.button(LEFT, true);
            let released_down = self.button(DOWN, true) || self.button(RIGHT, true);
            if released_up || released_down {
                if self.f(F::Hold) == 0 {
                    let index = self.f(F::Index) as i32;
                    self.set(
                        F::Index,
                        (if released_up {
                            (index + count - 1) % count
                        } else {
                            (index + 1) % count
                        }) as u32,
                    );
                    self.set(F::Cursor, 1);
                    self.redraw();
                }
                self.set(F::Hold, 0);
            }
        }
    }
}
/// # Safety
/// All callbacks obey synchronous exclusive scalar access and resource contracts.
pub unsafe fn run(s: *const Services, operation: u8) {
    if let Some(s) = s.as_ref() {
        if let Some(e) = Engine::new(*s) {
            match operation {
                x if x == Operation::Input as u8 => e.input(),
                x if x == Operation::PopupChanged as u8 => e.popup_changed(),
                x if x == Operation::PickerResult as u8 => e.picker_result(),
                x if (Operation::OpenNone as u8..=Operation::OpenMore as u8).contains(&x) => {
                    e.open(u32::from(x - Operation::OpenNone as u8))
                }
                x if x == Operation::Close as u8 => e.close(),
                x if x == Operation::Cancel as u8 => e.cancel(),
                _ => {}
            }
        }
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
    assert!(Field::Overlay as u8 == 0);
    assert!(Field::Spine as u8 == 1);
    assert!(Field::NextPage as u8 == 2);
    assert!(Field::Focus as u8 == 3);
    assert!(Field::Index as u8 == 4);
    assert!(Field::Cursor as u8 == 5);
    assert!(Field::Hold as u8 == 6);
    assert!(Field::Stored as u8 == 7);
    assert!(Query::HasToolbar as u8 == 0);
    assert!(Query::PopupActive as u8 == 1);
    assert!(Query::SpineCount as u8 == 2);
    assert!(Query::TocCount as u8 == 3);
    assert!(Query::MoreCount as u8 == 4);
    assert!(Query::Rows as u8 == 5);
    assert!(Query::Routed as u8 == 6);
    assert!(Query::Value as u8 == 7);
    assert!(Query::Permille as u8 == 8);
    assert!(Query::X as u8 == 9);
    assert!(Query::Y as u8 == 10);
    assert!(Query::Width as u8 == 11);
    assert!(Query::Height as u8 == 12);
    assert!(Query::FocusReading as u8 == 13);
    assert!(Query::Pressed as u8 == 14);
    assert!(Query::Released as u8 == 15);
    assert!(Command::PopupInput as u8 == 0);
    assert!(Command::Lock as u8 == 1);
    assert!(Command::Unlock as u8 == 2);
    assert!(Command::Settle as u8 == 3);
    assert!(Command::PaintPopup as u8 == 4);
    assert!(Command::Restore as u8 == 5);
    assert!(Command::Store as u8 == 6);
    assert!(Command::Render as u8 == 7);
    assert!(Command::Push as u8 == 8);
    assert!(Command::Update as u8 == 9);
    assert!(Command::Close as u8 == 10);
    assert!(Command::Open as u8 == 11);
    assert!(Command::ClearDeferred as u8 == 12);
    assert!(Command::DropSection as u8 == 13);
    assert!(Command::Route as u8 == 14);
    assert!(Command::DismissPopup as u8 == 15);
    assert!(Command::Discard as u8 == 16);
    assert!(Command::StartPicker as u8 == 17);
    assert!(Command::ApplyText as u8 == 18);
    assert!(Command::ToolbarBegin as u8 == 19);
    assert!(Command::SetFocusReading as u8 == 20);
    assert!(Command::ApplyLive as u8 == 21);
    assert!(Command::TextPopup as u8 == 22);
    assert!(Command::More as u8 == 23);
    assert!(Command::TocItem as u8 == 24);
    assert!(Command::TocAnchor as u8 == 25);
    assert!(Command::TocDrop as u8 == 26);
    assert!(Command::Scroll as u8 == 27);
    assert!(Command::TakeSwipe as u8 == 28);
    assert!(Command::SampleButtons as u8 == 29);
    assert!(Command::TakeHeldMs as u8 == 30);
    assert!(Operation::Cancel as u8 == 9);
    assert!(Operation::Input as u8 == 0);
    assert!(Operation::PopupChanged as u8 == 1);
    assert!(Operation::PickerResult as u8 == 2);
    assert!(Event::None as u8 == 0);
    assert!(Event::Dismiss as u8 == 1);
    assert!(Event::Tool as u8 == 2);
    assert!(Event::PrevChapter as u8 == 3);
    assert!(Event::NextChapter as u8 == 4);
    assert!(Event::Scrub as u8 == 5);
    assert!(Event::Row as u8 == 6);
    assert!(Swipe::None as u8 == 0);
    assert!(Swipe::Left as u8 == 1);
    assert!(Swipe::Right as u8 == 2);
    assert!(Swipe::Up as u8 == 3);
    assert!(Swipe::Down as u8 == 4);
    assert!(Mode::None as u8 == 0);
    assert!(Mode::Toolbar as u8 == 1);
    assert!(Mode::Contents as u8 == 2);
    assert!(Mode::Text as u8 == 3);
    assert!(Mode::More as u8 == 4);
    assert!(Query::HasSection as u8 == 16);
    assert!(Query::PageStale as u8 == 17);
    assert!(Query::Touch as u8 == 18);
    assert!(Query::CurrentToc as u8 == 19);
    assert!(Query::IsA4 as u8 == 20);
    assert!(Query::IsXteink as u8 == 21);
    assert!(Command::ResetHome as u8 == 31);
    assert!(Command::CreateToolbar as u8 == 32);
    assert!(Command::ResetNavigation as u8 == 33);
    assert!(Command::BuildMore as u8 == 34);
    assert!(Command::DropToolbar as u8 == 35);
    assert!(Command::ForceCounter as u8 == 36);
    assert!(Command::DisplayFast as u8 == 37);
    assert!(Operation::OpenNone as u8 == 3);
    assert!(Operation::OpenToolbar as u8 == 4);
    assert!(Operation::OpenContents as u8 == 5);
    assert!(Operation::OpenText as u8 == 6);
    assert!(Operation::OpenMore as u8 == 7);
    assert!(Operation::Close as u8 == 8);
    assert!(TEXT_ROWS == 5 && HOLD_MS == 1500 && HOLD_ROWS == 10);
    assert!(SWIPE_UP == 3);
    assert!(SWIPE_DOWN == 4);
};

#[cfg(test)]
mod tests {
    use super::*;
    struct Fake {
        fields: [u32; 8],
        queries: [u32; 22],
        event: u32,
        buttons: u32,
        locks: i32,
        toc: i32,
        invalid: bool,
        calls: usize,
        commands: u64,
    }
    impl Fake {
        fn new(mode: u32, event: u32, flags: u32) -> Self {
            let mut fields = [0; 8];
            fields[Field::Overlay as usize] = mode;
            fields[Field::Spine as usize] = 3;
            fields[Field::Index as usize] = flags % 8;
            fields[Field::Stored as usize] = flags & 1;
            fields[Field::Cursor as usize] = (flags >> 1) & 1;
            fields[Field::Hold as usize] = (flags >> 2) & 1;
            let mut queries = [0; 22];
            queries[Query::HasToolbar as usize] = u32::from(flags & 128 == 0);
            queries[Query::PopupActive as usize] = u32::from(flags & 64 != 0);
            queries[Query::SpineCount as usize] = 7;
            queries[Query::TocCount as usize] = flags % 12;
            queries[Query::MoreCount as usize] = flags % 6;
            queries[Query::Rows as usize] = flags % 4;
            queries[Query::Value as usize] = flags % 3;
            queries[Query::Permille as usize] = 500;
            queries[Query::Width as usize] = 600;
            queries[Query::Height as usize] = 800;
            Self {
                fields,
                queries,
                event,
                buttons: flags,
                locks: 0,
                toc: 0,
                invalid: false,
                calls: 0,
                commands: 0,
            }
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
        fn balanced(&self) {
            assert_eq!(self.locks, 0);
            assert_eq!(self.toc, 0);
            assert!(!self.invalid);
        }
    }
    unsafe extern "C" fn read(p: *mut c_void, field: u8) -> u32 {
        (*(p as *mut Fake)).fields[field as usize]
    }
    unsafe extern "C" fn write(p: *mut c_void, field: u8, value: u32) {
        (*(p as *mut Fake)).fields[field as usize] = value;
    }
    unsafe extern "C" fn query(p: *mut c_void, query: u8) -> u32 {
        (*(p as *mut Fake)).queries[query as usize]
    }
    unsafe extern "C" fn command(p: *mut c_void, command: u8, a: i32, b: i32) -> u32 {
        let f = &mut *(p as *mut Fake);
        f.calls += 1;
        f.commands |= 1 << command;
        match command {
            x if x == Command::Lock as u8 => {
                f.invalid |= f.locks != 0;
                f.locks += 1;
            }
            x if x == Command::Unlock as u8 => {
                f.invalid |= f.locks != 1;
                f.locks -= 1;
            }
            x if x == Command::TocItem as u8 => {
                f.invalid |= f.toc != 0;
                f.toc += 1;
                return if f.buttons & 1 != 0 { u32::MAX } else { 3 };
            }
            x if x == Command::TocDrop as u8 => {
                f.invalid |= f.toc != 1;
                f.toc -= 1;
            }
            x if x == Command::TocAnchor as u8 => {
                f.invalid |= f.toc != 1 || f.locks != 1;
            }
            x if x == Command::DismissPopup as u8 => f.queries[Query::PopupActive as usize] = 0,
            x if x == Command::Route as u8 => return f.event,
            x if x == Command::Store as u8 => return f.buttons & 1,
            x if x == Command::TakeSwipe as u8 => return f.buttons % 5,
            x if x == Command::TakeHeldMs as u8 => return 1500,
            x if x == Command::SampleButtons as u8 => {
                f.queries[Query::Pressed as usize] = f.buttons & a as u32;
                f.queries[Query::Released as usize] = f.buttons & b as u32;
            }
            _ => {}
        }
        0
    }
    #[test]
    fn every_operation_releases_scoped_resources() {
        for operation in [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 255] {
            for mode in 0..6 {
                for event in 0..8 {
                    for flags in 0..256 {
                        let mut fake = Fake::new(mode, event, flags);
                        unsafe {
                            run(&fake.services(), operation);
                        }
                        fake.balanced();
                        if operation > Operation::Cancel as u8 {
                            assert_eq!(fake.calls, 0);
                        }
                    }
                }
            }
        }
    }
    #[test]
    fn cancel_never_samples_input() {
        for mode in 0..=MORE {
            for flags in [0, 1, 64, 65, 128] {
                let mut fake = Fake::new(mode, Event::Tool as u32, flags);
                unsafe { run(&fake.services(), Operation::Cancel as u8) };
                fake.balanced();
                for command in [
                    Command::Route,
                    Command::PopupInput,
                    Command::SampleButtons,
                    Command::TakeSwipe,
                    Command::TakeHeldMs,
                    Command::Open,
                ] {
                    assert_eq!(fake.commands & (1 << command as u8), 0);
                }
                if mode == NONE || flags == 128 {
                    assert_eq!(fake.calls, 0);
                } else if flags & 64 != 0 {
                    assert_eq!(fake.fields[Field::Overlay as usize], mode);
                    assert_eq!(fake.queries[Query::PopupActive as usize], 0);
                    assert_ne!(fake.commands & (1 << Command::DismissPopup as u8), 0);
                } else if mode == TOOLBAR {
                    assert_eq!(fake.commands, 1 << Command::Close as u8);
                } else {
                    assert_eq!(fake.fields[Field::Overlay as usize], TOOLBAR);
                }
            }
        }
    }
    #[test]
    fn guards_release_on_early_return() {
        fn leave(engine: &Engine, early: bool) {
            let _lock = engine.lock();
            let (_toc, _) = engine.toc(0);
            if early {
                return;
            }
            engine.op(Command::TocAnchor);
        }
        for early in [false, true] {
            let mut fake = Fake::new(2, 6, 0);
            let engine = Engine::new(fake.services()).unwrap();
            leave(&engine, early);
            fake.balanced();
            assert_eq!(fake.calls, if early { 4 } else { 5 });
        }
    }
    #[test]
    fn missing_services_have_no_effect() {
        unsafe {
            run(core::ptr::null(), 0);
        }
        let mut fake = Fake::new(2, 6, 0);
        for missing in 0..4 {
            let mut services = fake.services();
            match missing {
                0 => services.read = None,
                1 => services.write = None,
                2 => services.query = None,
                _ => services.command = None,
            }
            unsafe {
                run(&services, 0);
            }
            fake.balanced();
            assert_eq!(fake.calls, 0);
        }
    }
}
