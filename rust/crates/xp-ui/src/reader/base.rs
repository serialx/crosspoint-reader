//! Shared reader lifecycle with borrowed resource callbacks and POD publication.
use super::input::Turn;
use super::thresholds::SKIP_HOLD_MS;
use super::values;
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    Remembered = 0,
    Rendered = 1,
    EndReady = 2,
    Counter = 3,
    Forced = 4,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    AtEnd = 0,
    HasEnd = 1,
    EndMenu = 2,
    Exists = 3,
    OpenPathEmpty = 4,
    SubscriberExit = 5,
    SessionWorthy = 6,
    SubscriberSession = 7,
    Held = 8,
    LongButton = 9,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    Update = 0,
    Finish = 1,
    BaseEnter = 2,
    BaseExit = 3,
    LogEnter = 4,
    LogExit = 5,
    ClearPath = 6,
    SaveState = 7,
    EnsureFonts = 8,
    OrientationInitial = 9,
    LoadBook = 10,
    LoadFailure = 11,
    RememberPath = 12,
    RecentAdd = 13,
    EmitOpen = 14,
    ReleaseFonts = 15,
    SessionReset = 16,
    Document = 17,
    EmitSession = 18,
    EmitExit = 19,
    OrientationPortrait = 20,
    ResetLoadCount = 21,
    EndDrop = 22,
    EndAllocate = 23,
    EndOom = 24,
    EndLoad = 25,
    EndRender = 26,
    EndInput = 27,
    OpenNext = 28,
    Home = 29,
    ReturnEnd = 30,
    FormatInput = 31,
    Back = 32,
    Turn = 33,
    Skip = 34,
    NoteTurn = 35,
    Haptic = 36,
    ClearScreen = 37,
    Display = 38,
    EndRendered = 39,
    MarkRendered = 40,
    SessionRender = 41,
    RenderBook = 42,
    Lock = 43,
    Unlock = 44,
    PathDrop = 45,
    DocumentDrop = 46,
    DocumentByte = 47,
    ForceRefresh = 48,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Enter = 0,
    Exit = 1,
    Remember = 2,
    ClearEnd = 3,
    Menu = 4,
    EndTurn = 5,
    Loop = 6,
    Render = 7,
    ForceRefresh = 8,
    Flush = 9,
    MenuActive = 10,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub write: Option<unsafe extern "C" fn(*mut c_void, u8, u32)>,
    pub query: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
    pub touch: Option<unsafe extern "C" fn(*mut c_void) -> Turn>,
    pub buttons: Option<unsafe extern "C" fn(*mut c_void) -> Turn>,
}
struct Engine {
    context: *mut c_void,
    read: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    write: unsafe extern "C" fn(*mut c_void, u8, u32),
    query: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    command: unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32,
    touch: unsafe extern "C" fn(*mut c_void) -> Turn,
    buttons: unsafe extern "C" fn(*mut c_void) -> Turn,
}
impl Engine {
    fn new(s: Services) -> Option<Self> {
        Some(Self {
            context: s.context,
            read: s.read?,
            write: s.write?,
            query: s.query?,
            command: s.command?,
            touch: s.touch?,
            buttons: s.buttons?,
        })
    }
    fn f(&self, f: Field) -> u32 {
        unsafe { (self.read)(self.context, f as u8) }
    }
    fn set(&self, f: Field, v: u32) {
        unsafe { (self.write)(self.context, f as u8, v) }
    }
    fn q(&self, q: Query) -> u32 {
        unsafe { (self.query)(self.context, q as u8) }
    }
    fn c(&self, c: Command, a: i32, b: i32) -> u32 {
        unsafe { (self.command)(self.context, c as u8, a, b) }
    }
    fn op(&self, c: Command) -> u32 {
        self.c(c, 0, 0)
    }
    fn remember(&self) {
        if self.f(Field::Remembered) != 0 || self.f(Field::Rendered) == 0 {
            return;
        }
        self.set(Field::Remembered, 1);
        self.op(Command::RememberPath);
        self.op(Command::SaveState);
        self.op(Command::RecentAdd);
        self.op(Command::EmitOpen);
    }
    fn clear_end(&self) {
        if self.q(Query::AtEnd) != 0 || self.f(Field::EndReady) == 0 {
            return;
        }
        self.op(Command::Lock);
        self.set(Field::EndReady, 0);
        self.op(Command::EndDrop);
        self.op(Command::Unlock);
    }
    fn menu_active(&self) -> bool {
        self.q(Query::AtEnd) != 0 && self.f(Field::EndReady) != 0 && self.q(Query::EndMenu) != 0
    }
    fn menu(&self, suppress: bool) -> bool {
        if suppress || !self.menu_active() {
            return false;
        }
        let result = match self.op(Command::EndInput) {
            values::END_OPEN_BOOK => {
                self.op(Command::OpenNext);
                true
            }
            values::END_GO_HOME => {
                self.op(Command::Home);
                true
            }
            values::END_LAST_PAGE => {
                self.op(Command::ReturnEnd);
                self.op(Command::Update);
                true
            }
            values::END_REDRAW => {
                self.op(Command::Update);
                true
            }
            _ => false,
        };
        self.op(Command::PathDrop);
        result
    }
    fn end_turn(&self, prev: bool, next: bool) -> bool {
        if self.q(Query::AtEnd) == 0 {
            return false;
        }
        if self.f(Field::EndReady) != 0 && self.q(Query::EndMenu) != 0 {
            return true;
        }
        if next {
            self.op(Command::Home);
        } else if prev {
            self.op(Command::ReturnEnd);
            self.op(Command::Update);
        }
        true
    }
    #[inline(never)]
    fn input(&self) {
        self.remember();
        self.clear_end();
        if self.menu(false) || self.op(Command::FormatInput) != 0 || self.op(Command::Back) != 0 {
            return;
        }
        let touch = unsafe { (self.touch)(self.context) };
        let buttons = unsafe { (self.buttons)(self.context) };
        let prev = buttons.prev != 0 || touch.prev != 0;
        let next = buttons.next != 0 || touch.next != 0;
        if !prev && !next {
            return;
        }
        if self.end_turn(prev, next) {
            return;
        }
        let touched = touch.prev != 0 || touch.next != 0;
        let held = if touched {
            touch.held_ms
        } else {
            self.q(Query::Held)
        };
        let skip = buttons.from_tilt == 0
            && self.q(Query::LongButton) == values::LONG_BUTTON_SKIP
            && held >= SKIP_HOLD_MS;
        let changed = if skip {
            self.c(Command::Skip, if prev { -10 } else { 10 }, 0)
        } else {
            self.c(Command::Turn, i32::from(!prev), 0)
        };
        self.c(Command::NoteTurn, i32::from(!skip && !prev), changed as i32);
        if changed != 0 && touched {
            self.c(Command::Haptic, i32::from(skip), 0);
        }
        self.op(Command::Update);
    }
    fn render(&self) {
        if self.q(Query::AtEnd) != 0 {
            if self.q(Query::HasEnd) == 0 {
                self.op(Command::EndAllocate);
                if self.q(Query::HasEnd) == 0 {
                    self.op(Command::EndOom);
                }
            }
            self.op(Command::ClearScreen);
            if self.q(Query::HasEnd) != 0 {
                self.op(Command::EndLoad);
                self.set(Field::EndReady, 1);
                self.op(Command::EndRender);
            }
            self.op(Command::Display);
            self.op(Command::EndRendered);
            self.op(Command::MarkRendered);
            self.op(Command::SessionRender);
            return;
        }
        self.op(Command::RenderBook);
        self.op(Command::SessionRender);
    }
    fn enter(&self) {
        self.op(Command::BaseEnter);
        self.op(Command::LogEnter);
        if self.q(Query::Exists) == 0 {
            self.op(Command::Finish);
            return;
        }
        if self.q(Query::OpenPathEmpty) == 0 {
            self.op(Command::ClearPath);
            self.op(Command::SaveState);
        }
        self.op(Command::EnsureFonts);
        self.op(Command::OrientationInitial);
        if self.op(Command::LoadBook) == 0 {
            if self.op(Command::LoadFailure) == 0 {
                self.op(Command::Finish);
            }
            return;
        }
        self.op(Command::Update);
    }
    fn flush(&self) {
        if self.q(Query::SessionWorthy) == 0 || self.q(Query::SubscriberSession) == 0 {
            self.op(Command::SessionReset);
            return;
        }
        if self.op(Command::Document) == 32
            && (0..32)
                .all(|index| matches!(self.c(Command::DocumentByte, index, 0), 48..=57 | 97..=102))
        {
            self.op(Command::EmitSession);
        }
        self.op(Command::SessionReset);
        self.op(Command::DocumentDrop);
    }
    fn exit(&self) {
        self.op(Command::BaseExit);
        self.op(Command::ReleaseFonts);
        self.op(Command::LogExit);
        self.flush();
        if self.q(Query::SubscriberExit) != 0 {
            self.op(Command::EmitExit);
        }
        self.op(Command::OrientationPortrait);
        self.op(Command::ResetLoadCount);
        self.op(Command::SaveState);
        self.op(Command::EndDrop);
        self.set(Field::EndReady, 0);
    }
    fn run(&self, operation: Operation, a: bool, b: bool) -> u8 {
        match operation {
            Operation::Enter => self.enter(),
            Operation::Exit => self.exit(),
            Operation::Remember => self.remember(),
            Operation::ClearEnd => self.clear_end(),
            Operation::Menu => return u8::from(self.menu(a)),
            Operation::EndTurn => return u8::from(self.end_turn(a, b)),
            Operation::Loop => self.input(),
            Operation::Render => self.render(),
            Operation::ForceRefresh => {
                self.op(Command::ForceRefresh);
                self.op(Command::Update);
                return 1;
            }
            Operation::Flush => self.flush(),
            Operation::MenuActive => return u8::from(self.menu_active()),
        }
        0
    }
}
/// # Safety
/// The borrowed callbacks are synchronous and valid for this call.
pub unsafe fn run(services: Services, operation: u8, a: bool, b: bool) -> u8 {
    const OPERATIONS: [Operation; 11] = [
        Operation::Enter,
        Operation::Exit,
        Operation::Remember,
        Operation::ClearEnd,
        Operation::Menu,
        Operation::EndTurn,
        Operation::Loop,
        Operation::Render,
        Operation::ForceRefresh,
        Operation::Flush,
        Operation::MenuActive,
    ];
    let Some(operation) = OPERATIONS.get(operation as usize).copied() else {
        return 0;
    };
    Engine::new(services).map_or(0, |e| e.run(operation, a, b))
}

const _: () = {
    assert!(Command::ForceRefresh as u8 == 48);
    assert!(Operation::MenuActive as u8 == 10);
    assert!(Field::Remembered as u8 == 0);
    assert!(Field::Rendered as u8 == 1);
    assert!(Field::EndReady as u8 == 2);
    assert!(Field::Counter as u8 == 3);
    assert!(Field::Forced as u8 == 4);
    assert!(Query::AtEnd as u8 == 0);
    assert!(Query::HasEnd as u8 == 1);
    assert!(Query::EndMenu as u8 == 2);
    assert!(Query::Exists as u8 == 3);
    assert!(Query::OpenPathEmpty as u8 == 4);
    assert!(Query::SubscriberExit as u8 == 5);
    assert!(Query::SessionWorthy as u8 == 6);
    assert!(Query::SubscriberSession as u8 == 7);
    assert!(Query::Held as u8 == 8);
    assert!(Query::LongButton as u8 == 9);
    assert!(Command::Update as u8 == 0);
    assert!(Command::Finish as u8 == 1);
    assert!(Command::BaseEnter as u8 == 2);
    assert!(Command::BaseExit as u8 == 3);
    assert!(Command::LogEnter as u8 == 4);
    assert!(Command::LogExit as u8 == 5);
    assert!(Command::ClearPath as u8 == 6);
    assert!(Command::SaveState as u8 == 7);
    assert!(Command::EnsureFonts as u8 == 8);
    assert!(Command::OrientationInitial as u8 == 9);
    assert!(Command::LoadBook as u8 == 10);
    assert!(Command::LoadFailure as u8 == 11);
    assert!(Command::RememberPath as u8 == 12);
    assert!(Command::RecentAdd as u8 == 13);
    assert!(Command::EmitOpen as u8 == 14);
    assert!(Command::ReleaseFonts as u8 == 15);
    assert!(Command::SessionReset as u8 == 16);
    assert!(Command::Document as u8 == 17);
    assert!(Command::EmitSession as u8 == 18);
    assert!(Command::EmitExit as u8 == 19);
    assert!(Command::OrientationPortrait as u8 == 20);
    assert!(Command::ResetLoadCount as u8 == 21);
    assert!(Command::EndDrop as u8 == 22);
    assert!(Command::EndAllocate as u8 == 23);
    assert!(Command::EndOom as u8 == 24);
    assert!(Command::EndLoad as u8 == 25);
    assert!(Command::EndRender as u8 == 26);
    assert!(Command::EndInput as u8 == 27);
    assert!(Command::OpenNext as u8 == 28);
    assert!(Command::Home as u8 == 29);
    assert!(Command::ReturnEnd as u8 == 30);
    assert!(Command::FormatInput as u8 == 31);
    assert!(Command::Back as u8 == 32);
    assert!(Command::Turn as u8 == 33);
    assert!(Command::Skip as u8 == 34);
    assert!(Command::NoteTurn as u8 == 35);
    assert!(Command::Haptic as u8 == 36);
    assert!(Command::ClearScreen as u8 == 37);
    assert!(Command::Display as u8 == 38);
    assert!(Command::EndRendered as u8 == 39);
    assert!(Command::MarkRendered as u8 == 40);
    assert!(Command::SessionRender as u8 == 41);
    assert!(Command::RenderBook as u8 == 42);
    assert!(Command::Lock as u8 == 43);
    assert!(Command::Unlock as u8 == 44);
    assert!(Command::PathDrop as u8 == 45);
    assert!(Command::DocumentDrop as u8 == 46);
    assert!(Command::DocumentByte as u8 == 47);
    assert!(Command::ForceRefresh as u8 == 48);
    assert!(Operation::Enter as u8 == 0);
    assert!(Operation::Exit as u8 == 1);
    assert!(Operation::Remember as u8 == 2);
    assert!(Operation::ClearEnd as u8 == 3);
    assert!(Operation::Menu as u8 == 4);
    assert!(Operation::EndTurn as u8 == 5);
    assert!(Operation::Loop as u8 == 6);
    assert!(Operation::Render as u8 == 7);
    assert!(Operation::ForceRefresh as u8 == 8);
    assert!(Operation::Flush as u8 == 9);
    assert!(Operation::MenuActive as u8 == 10);
};

use super::Session;

pub const REFRESH_MANUAL: u8 = 1;
pub const REFRESH_COUNTER_ONLY: u8 = 2;
pub const REFRESH_REQUEST_MASK: u8 = REFRESH_MANUAL | REFRESH_COUNTER_ONLY;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Model {
    pub session: Session,
    pub pages_until_full_refresh: i32,
    pub refresh_requests: u8,
    pub end_ready: u8,
    pub page_rendered: u8,
    pub book_remembered: u8,
}
impl Model {
    pub fn new(fast: bool, frequency: i32) -> Self {
        Self {
            pages_until_full_refresh: if fast { frequency.max(2) } else { 0 },
            ..Self::default()
        }
    }
    fn valid_requests(&self) {
        debug_assert!(self.refresh_requests & !REFRESH_REQUEST_MASK == 0);
    }
    pub fn begin_render(&mut self) -> Self {
        self.valid_requests();
        let frame = *self;
        self.session.pending_forward = 0;
        self.refresh_requests = 0;
        frame
    }
    pub fn publish(&mut self, frame: &Self) {
        self.valid_requests();
        frame.valid_requests();
        let next_turn = self.session.pending_forward;
        self.session = frame.session;
        self.session.pending_forward = next_turn;
        if self.refresh_requests == 0 {
            self.pages_until_full_refresh = frame.pages_until_full_refresh;
        }
        self.end_ready = frame.end_ready;
        self.page_rendered = frame.page_rendered;
    }
    pub fn force_refresh(&mut self) {
        self.valid_requests();
        self.pages_until_full_refresh = 1;
        self.refresh_requests |= REFRESH_MANUAL;
    }
    pub fn request_counter(&mut self, value: i32) {
        self.valid_requests();
        self.pages_until_full_refresh = value;
        self.refresh_requests |= REFRESH_COUNTER_ONLY;
    }
}
const _: () = {
    assert!(core::mem::size_of::<Model>() == 48);
    assert!(core::mem::align_of::<Model>() == 8);
    assert!(core::mem::offset_of!(Model, session) == 0);
    assert!(core::mem::offset_of!(Model, pages_until_full_refresh) == 40);
    assert!(core::mem::offset_of!(Model, refresh_requests) == 44);
    assert!(core::mem::offset_of!(Model, end_ready) == 45);
    assert!(core::mem::offset_of!(Model, page_rendered) == 46);
    assert!(core::mem::offset_of!(Model, book_remembered) == 47);
};

impl Model {
    pub fn read(&self, field: u8) -> u32 {
        self.valid_requests();
        match field {
            x if x == Field::Remembered as u8 => u32::from(self.book_remembered),
            x if x == Field::Rendered as u8 => u32::from(self.page_rendered),
            x if x == Field::EndReady as u8 => u32::from(self.end_ready),
            x if x == Field::Counter as u8 => self.pages_until_full_refresh as u32,
            x if x == Field::Forced as u8 => u32::from(self.refresh_requests & REFRESH_MANUAL != 0),
            _ => 0,
        }
    }
    pub fn write(&mut self, field: u8, value: u32) {
        self.valid_requests();
        match field {
            x if x == Field::Remembered as u8 => self.book_remembered = u8::from(value != 0),
            x if x == Field::Rendered as u8 => self.page_rendered = u8::from(value != 0),
            x if x == Field::EndReady as u8 => self.end_ready = u8::from(value != 0),
            x if x == Field::Counter as u8 => self.request_counter(value as i32),
            x if x == Field::Forced as u8 => {
                if value != 0 {
                    self.refresh_requests |= REFRESH_MANUAL;
                } else {
                    self.refresh_requests &= !REFRESH_MANUAL;
                }
            }
            _ => {}
        }
    }
}

const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<Services>() == 7 * size_of::<usize>());
    assert!(align_of::<Services>() == align_of::<usize>());
    assert!(offset_of!(Services, context) == 0);
    assert!(offset_of!(Services, read) == size_of::<usize>());
    assert!(offset_of!(Services, write) == 2 * size_of::<usize>());
    assert!(offset_of!(Services, query) == 3 * size_of::<usize>());
    assert!(offset_of!(Services, command) == 4 * size_of::<usize>());
    assert!(offset_of!(Services, touch) == 5 * size_of::<usize>());
    assert!(offset_of!(Services, buttons) == 6 * size_of::<usize>());
};

const _: () = {
    assert!(REFRESH_MANUAL == 1);
    assert!(REFRESH_COUNTER_ONLY == 2);
    assert!(REFRESH_REQUEST_MASK == 3);
};

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn counter_only_and_manual_requests_preserve_their_distinct_meaning() {
        for manual in [false, true] {
            for counter in [0, 1] {
                let mut live = Model::new(true, 15);
                let mut frame = live.begin_render();
                if manual {
                    live.force_refresh();
                }
                live.write(Field::Counter as u8, counter);
                frame.pages_until_full_refresh = 14;
                live.publish(&frame);
                let next = live.begin_render();
                assert_eq!(next.pages_until_full_refresh, counter as i32);
                assert_eq!(next.read(Field::Forced as u8), u32::from(manual));
                assert_eq!(
                    next.refresh_requests,
                    REFRESH_COUNTER_ONLY | u8::from(manual)
                );
                assert_eq!(live.refresh_requests, 0);
            }
        }
        let mut live = Model::default();
        live.request_counter(0);
        live.force_refresh();
        assert_eq!(live.refresh_requests, REFRESH_REQUEST_MASK);
        assert_eq!(live.pages_until_full_refresh, 1);
        live.write(Field::Forced as u8, 0);
        assert_eq!(live.refresh_requests, REFRESH_COUNTER_ONLY);
        live.write(Field::Forced as u8, 1);
        assert_eq!(live.refresh_requests, REFRESH_REQUEST_MASK);
    }
    #[test]
    #[should_panic]
    fn reserved_request_bits_are_rejected() {
        let mut live = Model {
            refresh_requests: 4,
            ..Model::default()
        };
        live.begin_render();
    }
    #[test]
    fn pending_input_belongs_to_next_render_and_cannot_be_lost() {
        for force in [false, true] {
            for forward in [false, true] {
                for succeeded in [false, true] {
                    let mut live = Model::new(true, 15);
                    live.session.render(0, 100, 10);
                    live.session.note(true, true);
                    let mut frame = live.begin_render();
                    assert_eq!(frame.session.pending_forward, 1);
                    assert_eq!(live.session.pending_forward, 0);
                    live.session.note(forward, succeeded);
                    if force {
                        live.force_refresh();
                    }
                    // Only the snapshot is modified by painting.
                    frame.session.render(10_000, 110, 20);
                    frame.pages_until_full_refresh = 14;
                    frame.page_rendered = 1;
                    live.book_remembered = 1;
                    live.publish(&frame);
                    assert_eq!(live.session.duration, 10);
                    assert_eq!(live.session.pending_forward, u8::from(forward && succeeded));
                    assert_eq!(live.pages_until_full_refresh, if force { 1 } else { 14 });
                    assert_eq!(live.refresh_requests, u8::from(force));
                    assert_eq!(live.page_rendered, 1);
                    assert_eq!(live.book_remembered, 1);
                    let next = live.begin_render();
                    assert_eq!(next.refresh_requests, u8::from(force));
                    assert_eq!(live.refresh_requests, 0);
                }
            }
        }
    }
}
