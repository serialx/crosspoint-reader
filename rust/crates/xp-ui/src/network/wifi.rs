//! Wi-Fi selection state. Scan/credential strings remain borrowed facade resources.
use crate::model::{self, Input, ListState};
#[repr(u8)]
#[derive(Clone, Copy, PartialEq)]
pub enum Phase {
    AutoConnecting = 0,
    Scanning = 1,
    List = 2,
    HiddenSsid = 3,
    Password = 4,
    Connecting = 5,
    Connected = 6,
    Save = 7,
    Failed = 8,
    Forget = 9,
}
impl Phase {
    pub fn read(n: u8) -> Option<Self> {
        Some(match n {
            0 => Self::AutoConnecting,
            1 => Self::Scanning,
            2 => Self::List,
            3 => Self::HiddenSsid,
            4 => Self::Password,
            5 => Self::Connecting,
            6 => Self::Connected,
            7 => Self::Save,
            8 => Self::Failed,
            9 => Self::Forget,
            _ => return None,
        })
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Default)]
pub enum Operation {
    #[default]
    None = 0,
    Scan = 1,
    CheckScan = 2,
    CheckConnection = 3,
    Cancel = 4,
    CancelScan = 5,
    CancelConnection = 6,
    Complete = 7,
    Select = 8,
    Hidden = 9,
    Password = 10,
    Connect = 11,
    ConnectSaved = 12,
    HiddenLookup = 13,
    TrySaved = 14,
    AutoFailure = 15,
    ManualList = 16,
    SaveComplete = 17,
    RemoveScan = 18,
    ForgetSelected = 19,
    Connected = 20,
    ErrorGeneral = 21,
    ErrorMissing = 22,
    ErrorTimeout = 23,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Update {
    pub invalidate: u8,
    pub operation: Operation,
    pub flags: u8,
}
impl Update {
    fn op(operation: Operation) -> Self {
        Self {
            operation,
            ..Self::default()
        }
    }
    fn paint(mut self) -> Self {
        self.invalidate = 1;
        self
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub list: ListState,
    pub ui_revision: u32,
    pub flash_index: i32,
    pub flash_ticks: u8,
    pub ui_invalidated: u8,
    pub reserved: [u8; 2],
    pub connection_at: u32,
    pub generation: u32,
    pub count: u16,
    pub real_count: u16,
    pub phase: u8,
    pub automatic: u8,
    pub manual_list: u8,
    pub used_saved: u8,
    pub requires_password: u8,
    pub save_selection: u8,
    pub forget_selection: u8,
    pub allow_auto: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Resources {
    pub selected_saved: u8,
    pub selected_hidden: u8,
    pub selected_encrypted: u8,
    pub password_nonempty: u8,
    pub saved_password_nonempty: u8,
    pub long_press: u8,
    pub routed: u8,
    pub reserved: u8,
}
impl State {
    pub fn init(&mut self, allow_auto: bool) {
        let last_repeat = self.list.last_repeat;
        *self = Self {
            list: ListState {
                last_repeat,
                ..ListState::new(0)
            },
            phase: Phase::Scanning as u8,
            allow_auto: u8::from(allow_auto),
            flash_index: -1,
            connection_at: self.connection_at,
            requires_password: self.requires_password,
            ..Self::default()
        };
    }
    pub fn begin_scan(&mut self, automatic: bool) {
        self.automatic = u8::from(automatic);
        self.manual_list = 0;
        self.list = ListState {
            last_repeat: self.list.last_repeat,
            navigation_revision: self.list.navigation_revision.wrapping_add(1),
            ..ListState::new(0)
        };
        self.phase = Phase::Scanning as u8;
        self.count = 0;
        self.generation = self.generation.wrapping_add(1);
    }
    pub fn scanned(&mut self, count: u16, real: u16, failed: bool) -> Update {
        self.count = count;
        self.real_count = real;
        if !failed && self.automatic != 0 && self.manual_list == 0 {
            return Update::op(Operation::TrySaved);
        }
        self.show_list();
        Update::default().paint()
    }
    pub fn show_list(&mut self) {
        self.automatic = 0;
        self.phase = Phase::List as u8;
        self.list.selected = 0;
    }
    pub fn select(&mut self, index: i32, r: Resources) -> Update {
        if index < 0 || index >= i32::from(self.count) {
            return Update::default();
        }
        if r.selected_hidden != 0 {
            return Update::op(Operation::Hidden);
        }
        self.requires_password = r.selected_encrypted;
        self.used_saved = 0;
        self.automatic = 0;
        if r.saved_password_nonempty != 0 {
            self.used_saved = 1;
            return Update::op(Operation::ConnectSaved);
        }
        Update::op(if self.requires_password != 0 {
            Operation::Password
        } else {
            Operation::Connect
        })
    }
    pub fn hidden(&mut self) {
        self.requires_password = 1;
        self.used_saved = 0;
        self.automatic = 0;
        self.phase = Phase::HiddenSsid as u8;
    }
    pub fn password(&mut self) {
        self.phase = Phase::Password as u8;
    }
    pub fn keyboard_result(&mut self, cancelled: bool, empty: bool) {
        if cancelled || self.phase == Phase::HiddenSsid as u8 && empty {
            self.phase = Phase::List as u8;
        }
    }
    pub fn credential(&mut self, attempted: bool, has_password: bool) -> bool {
        if attempted {
            return false;
        }
        self.requires_password = u8::from(has_password);
        self.used_saved = 1;
        self.automatic = 1;
        self.manual_list = 0;
        true
    }
    pub fn connecting(&mut self, now: u32) {
        self.phase = if self.automatic != 0 {
            Phase::AutoConnecting
        } else {
            Phase::Connecting
        } as u8;
        self.connection_at = now;
    }
    pub fn connection(&mut self, status: i32, now: u32) -> Update {
        if !matches!(
            Phase::read(self.phase),
            Some(Phase::Connecting | Phase::AutoConnecting)
        ) {
            return Update::default();
        }
        if status == 3 {
            self.automatic = 0;
            return Update::op(Operation::Connected);
        }
        let error = match status {
            1 => Operation::ErrorMissing,
            4 => Operation::ErrorGeneral,
            _ => {
                let timeout = if self.automatic != 0 { 7000 } else { 15000 };
                if now.wrapping_sub(self.connection_at) <= timeout {
                    return Update::default();
                }
                Operation::ErrorTimeout
            }
        };
        Update::op(error)
    }
    pub fn failed(&mut self) -> Update {
        if self.automatic != 0 {
            return Update::op(Operation::AutoFailure);
        }
        self.phase = Phase::Failed as u8;
        Update::default().paint()
    }
    pub fn connected(&mut self, password_nonempty: bool) -> Update {
        if self.used_saved == 0 && password_nonempty {
            self.phase = Phase::Save as u8;
            self.save_selection = 0;
            Update::default().paint()
        } else {
            Update::op(Operation::Complete)
        }
    }
    pub fn manual(&mut self) -> Update {
        self.automatic = 0;
        self.manual_list = 1;
        if self.count == 0 {
            return Update::op(Operation::Scan);
        }
        self.phase = Phase::List as u8;
        self.list.selected = 0;
        Update::default().paint()
    }
    pub fn failed_auto(&mut self) -> Update {
        if self.count > 0 {
            return Update::op(Operation::TrySaved);
        }
        let mut u = Update::op(Operation::Scan);
        u.flags = 1;
        u
    }
    fn prompt(&mut self, input: Input, forget: bool) -> Update {
        let selection = if forget {
            &mut self.forget_selection
        } else {
            &mut self.save_selection
        };
        if input.pressed & (model::UP | model::LEFT) != 0 {
            if *selection > 0 {
                *selection -= 1;
                return Update::default().paint();
            }
        } else if input.pressed & (model::DOWN | model::RIGHT) != 0 {
            if *selection < 1 {
                *selection += 1;
                return Update::default().paint();
            }
        } else if input.pressed & model::CONFIRM != 0 {
            return Update::op(if forget {
                if *selection == 1 {
                    Operation::RemoveScan
                } else {
                    Operation::Scan
                }
            } else if *selection == 0 {
                Operation::SaveComplete
            } else {
                Operation::Complete
            });
        } else if input.pressed & model::BACK != 0 {
            return Update::op(if forget {
                Operation::Scan
            } else {
                Operation::Complete
            });
        }
        Update::default()
    }
    pub fn update(&mut self, input: Input, r: Resources) -> Update {
        let Some(phase) = Phase::read(self.phase) else {
            return Update::default();
        };
        match phase {
            Phase::Scanning => {
                if input.pressed & model::BACK != 0 {
                    return Update::op(Operation::CancelScan);
                }
                let mut u = Update::op(Operation::CheckScan);
                if self.automatic != 0 && input.pressed & model::CONFIRM != 0 {
                    self.automatic = 0;
                    self.manual_list = 1;
                    u.invalidate = 1;
                }
                u
            }
            Phase::AutoConnecting | Phase::Connecting => {
                if phase == Phase::AutoConnecting {
                    if input.pressed & model::BACK != 0 {
                        return Update::op(Operation::CancelConnection);
                    }
                    if input.pressed & model::CONFIRM != 0 {
                        return Update::op(Operation::ManualList);
                    }
                }
                Update::op(Operation::CheckConnection)
            }
            Phase::HiddenSsid => Update::op(Operation::HiddenLookup),
            Phase::Password => Update::op(Operation::Connect),
            Phase::Save | Phase::Forget => {
                self.touch_edges(input, r.routed != 0);
                if r.routed != 0
                    && input.touch_flags & model::TOUCH_RELEASED != 0
                    && input.action == 3
                    && (0..=1).contains(&input.value)
                {
                    self.dispatched(false, -1);
                    let selected = input.value as u8;
                    let mut update = if phase == Phase::Save {
                        self.save_selection = selected;
                        Update::op(if selected == 0 {
                            Operation::SaveComplete
                        } else {
                            Operation::Complete
                        })
                    } else {
                        self.forget_selection = selected;
                        Update::op(if selected == 1 {
                            Operation::RemoveScan
                        } else {
                            Operation::Scan
                        })
                    };
                    update.flags |= 2;
                    update
                } else {
                    let mut update = self.prompt(input, phase == Phase::Forget);
                    if r.routed != 0 && self.ui_invalidated != 0 {
                        update.invalidate = 1;
                    }
                    update
                }
            }
            Phase::Connected => Update::op(Operation::Complete),
            Phase::Failed => {
                if input.pressed & (model::BACK | model::CONFIRM) != 0 {
                    if self.automatic != 0 || self.used_saved != 0 {
                        self.automatic = 0;
                        self.phase = Phase::Forget as u8;
                        self.forget_selection = 0;
                    } else {
                        self.phase = Phase::List as u8;
                    }
                    Update::default().paint()
                } else {
                    Update::default()
                }
            }
            Phase::List => self.list_input(input, r),
        }
    }
    fn list_input(&mut self, input: Input, r: Resources) -> Update {
        if self.manual_list != 0 {
            if input.held & model::CONFIRM == 0 {
                self.manual_list = 0;
            }
            return Update::default();
        }
        if input.pressed & model::BACK != 0 {
            return Update::op(Operation::Cancel);
        }
        if input.released & model::CONFIRM != 0 {
            return Update::op(if self.count != 0 {
                Operation::Select
            } else {
                Operation::Scan
            });
        }
        if input.pressed & model::RIGHT != 0 {
            return Update::op(Operation::Scan);
        }
        if input.pressed & model::LEFT != 0 && r.selected_saved != 0 {
            return self.forget_selected();
        }
        self.touch_edges(input, r.routed != 0);
        if r.routed != 0 && input.touch_flags & model::TOUCH_RELEASED != 0 {
            if input.action == 2 && r.long_press == 0 {
                self.dispatched(false, -1);
                let mut u = Update::op(Operation::Scan);
                u.flags = 2;
                return u;
            }
            if input.action == 1 && input.value >= 0 && input.value < i32::from(self.count) {
                self.list.selected = input.value;
                let flash = r.long_press != 0 && r.selected_saved == 0;
                self.dispatched(flash, input.value);
                let mut u = if r.long_press != 0 {
                    if r.selected_saved != 0 {
                        self.forget_selected()
                    } else {
                        Update::default()
                    }
                } else {
                    Update::op(Operation::Select)
                };
                u.flags |= 2;
                return u;
            }
        }
        let touch_repaint = r.routed != 0 && self.ui_invalidated != 0;
        if self.count != 0 && matches!(input.swipe, model::SWIPE_UP | model::SWIPE_DOWN) {
            let delta = if input.swipe == model::SWIPE_UP {
                self.list.visible
            } else {
                -self.list.visible
            };
            self.list.navigation_revision = self.list.navigation_revision.wrapping_add(1);
            return Update {
                invalidate: self.list.scroll(delta).invalidate,
                ..Update::default()
            };
        }
        let mut u = Update {
            invalidate: u8::from(touch_repaint),
            ..Update::default()
        };
        for (mask, delta) in [(model::NAV_NEXT, 1), (model::NAV_PREVIOUS, -1)] {
            if input.pressed & mask != 0 {
                u.invalidate = self
                    .list
                    .step(i32::from(self.count), delta, false)
                    .invalidate;
            }
            if (input.pressed | input.released) & mask != 0 {
                self.list.last_repeat = 0;
            } else if input.held & mask != 0
                && input.held_ms > 500
                && input.now_ms.wrapping_sub(self.list.last_repeat) > 500
            {
                u.invalidate = self
                    .list
                    .step(i32::from(self.count), delta, false)
                    .invalidate;
                self.list.last_repeat = input.now_ms;
            }
        }
        u
    }
    fn forget_selected(&mut self) -> Update {
        self.phase = Phase::Forget as u8;
        self.forget_selection = 0;
        Update::op(Operation::ForgetSelected).paint()
    }
}
/// The facade keeps std::sort's original permutation for equivalent keys.
pub fn row_before(saved_a: bool, rssi_a: i32, saved_b: bool, rssi_b: i32) -> bool {
    if saved_a != saved_b {
        saved_a
    } else {
        rssi_a > rssi_b
    }
}

use core::ffi::c_void;
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Call {
    Now = 0,
    Paint = 1,
    EntryReset = 2,
    ReadMac = 3,
    Exit = 4,
    LastCredential = 5,
    CredentialFlags = 6,
    CopyAutoCredential = 7,
    DropCredential = 8,
    ClearScan = 9,
    WifiSta = 10,
    Disconnect = 11,
    Delay = 12,
    ScanBegin = 13,
    ScanComplete = 14,
    ScanReserve = 15,
    ScanRead = 16,
    ScanFind = 17,
    ScanAppend = 18,
    ScanRssi = 19,
    ScanReplace = 20,
    ScanSort = 21,
    RowCount = 22,
    HiddenAppend = 23,
    RowsRebuild = 24,
    ScanDelete = 25,
    RowFlags = 26,
    RowAttempted = 27,
    RowCredential = 28,
    SelectRow = 29,
    SelectedCredential = 30,
    CopyPassword = 31,
    PasswordPresent = 32,
    LogSaved = 33,
    HiddenReset = 34,
    Keyboard = 35,
    ConnectionReset = 36,
    ConnectionStart = 37,
    ConnectionStatus = 38,
    ConnectedDetails = 39,
    SetError = 40,
    LogComplete = 41,
    Complete = 42,
    LogAutoFailure = 43,
    LogManual = 44,
    Save = 45,
    Forget = 46,
    CopySelected = 47,
    Haptic = 48,
}
#[repr(C)]
pub struct Services {
    pub context: *mut c_void,
    pub call: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    pub publish: Option<unsafe extern "C" fn(*mut c_void, *mut State)>,
}
impl Services {
    #[inline(never)]
    fn call(&mut self, operation: Call, value: u32) -> u32 {
        self.call
            .map_or(0, |f| unsafe { f(self.context, operation as u32, value) })
    }
    #[inline(never)]
    fn publish(&mut self, state: &mut State) {
        if let Some(f) = self.publish {
            unsafe { f(self.context, state) }
        }
    }
}
impl State {
    fn paint(&mut self, c: &mut Services) {
        c.publish(self);
        c.call(Call::Paint, 0);
    }
    pub fn run(&mut self, c: &mut Services, event: u8, value: u32, input: Input) {
        match event {
            0 => {
                let auto = self.allow_auto != 0;
                self.init(auto);
                let count = c.call(Call::EntryReset, 0);
                c.call(Call::ReadMac, 0);
                self.paint(c);
                if auto && count != 0 {
                    let found = c.call(Call::LastCredential, 0) != 0;
                    let started = found && self.try_credential(c);
                    c.call(Call::DropCredential, 0);
                    if !started {
                        self.scan(c, true);
                    }
                } else {
                    self.scan(c, false);
                }
            }
            1 => {
                c.call(Call::Exit, 0);
            }
            2 => {
                let index = if input.action == 1 {
                    input.value
                } else {
                    self.list.selected
                };
                let flags = if index >= 0 && index < i32::from(self.count) {
                    c.call(Call::RowFlags, index as u32)
                } else {
                    0
                };
                let r = Resources {
                    selected_saved: (flags & 1) as u8,
                    long_press: (value & 1) as u8,
                    routed: u8::from(value & 2 != 0),
                    ..Resources::default()
                };
                let update = self.update(input, r);
                self.apply(c, update);
                if update.flags & 2 != 0 {
                    c.call(Call::Haptic, u32::from(r.long_press));
                    self.paint(c);
                }
            }
            3 => {
                self.keyboard_result(value & 1 != 0, value & 2 != 0);
            }
            _ => {}
        }
        c.publish(self);
    }
    fn apply(&mut self, c: &mut Services, u: Update) {
        if u.invalidate != 0 && !matches!(u.operation, Operation::ForgetSelected) {
            self.paint(c);
        }
        match u.operation {
            Operation::Scan => self.scan(c, u.flags & 1 != 0),
            Operation::CheckScan => self.check_scan(c),
            Operation::CheckConnection => self.check_connection(c),
            Operation::Cancel => {
                c.call(Call::Complete, 0);
            }
            Operation::CancelScan => {
                c.call(Call::ScanDelete, 0);
                c.call(Call::Complete, 0);
            }
            Operation::CancelConnection => {
                c.call(Call::Disconnect, 0);
                c.call(Call::Complete, 0);
            }
            Operation::Complete => {
                c.call(Call::Complete, 1);
            }
            Operation::Select => self.select_row(c, self.list.selected),
            Operation::Hidden => {
                c.call(Call::HiddenReset, 0);
                self.hidden();
                c.publish(self);
                c.call(Call::Keyboard, 1);
            }
            Operation::Password => {
                self.password();
                c.publish(self);
                c.call(Call::Keyboard, 0);
            }
            Operation::Connect => self.connect(c),
            Operation::ConnectSaved => {
                c.call(Call::CopyPassword, 0);
                c.call(Call::LogSaved, 0);
                self.connect(c);
            }
            Operation::HiddenLookup => {
                let found = c.call(Call::SelectedCredential, 0) != 0;
                if found && c.call(Call::CredentialFlags, 0) & 2 != 0 {
                    c.call(Call::CopyPassword, 0);
                    self.used_saved = 1;
                    c.call(Call::LogSaved, 1);
                    self.connect(c);
                } else {
                    self.apply(c, Update::op(Operation::Password));
                }
                c.call(Call::DropCredential, 0);
            }
            Operation::TrySaved => {
                if !self.try_next(c) {
                    self.show_list();
                    self.paint(c);
                }
            }
            Operation::AutoFailure => {
                c.call(Call::LogAutoFailure, 0);
                c.call(Call::Disconnect, 0);
                let update = self.failed_auto();
                self.apply(c, update);
            }
            Operation::ManualList => {
                c.call(Call::LogManual, 0);
                c.call(Call::Disconnect, 0);
                let update = self.manual();
                self.apply(c, update);
                self.manual_list = 1;
            }
            Operation::SaveComplete => {
                c.call(Call::Save, 0);
                c.call(Call::Complete, 1);
            }
            Operation::RemoveScan => {
                c.call(Call::Forget, 0);
                self.scan(c, false);
            }
            Operation::ForgetSelected => {
                c.call(Call::CopySelected, self.list.selected as u32);
                self.paint(c);
            }
            Operation::Connected => {
                c.publish(self);
                c.call(Call::ConnectedDetails, 0);
                let update = self.connected(c.call(Call::PasswordPresent, 0) != 0);
                if matches!(update.operation, Operation::Complete) {
                    c.call(Call::LogComplete, 0);
                }
                self.apply(c, update);
            }
            Operation::ErrorGeneral | Operation::ErrorMissing | Operation::ErrorTimeout => {
                let error = if matches!(u.operation, Operation::ErrorTimeout) {
                    c.call(Call::Disconnect, 0);
                    2
                } else if matches!(u.operation, Operation::ErrorMissing) {
                    1
                } else {
                    0
                };
                c.call(Call::SetError, error);
                let update = self.failed();
                self.apply(c, update);
            }
            Operation::None => {}
        }
    }
    fn scan(&mut self, c: &mut Services, automatic: bool) {
        self.begin_scan(automatic);
        // Leave List before releasing its rows to concurrent rendering.
        self.paint(c);
        c.call(Call::ClearScan, 0);
        c.call(Call::WifiSta, 0);
        c.call(Call::Disconnect, 0);
        c.call(Call::Delay, 100);
        c.call(Call::ScanBegin, 0);
    }
    fn check_scan(&mut self, c: &mut Services) {
        let result = c.call(Call::ScanComplete, 0) as i32;
        if result == -1 {
            return;
        }
        if result == -2 {
            c.call(Call::ScanReserve, 0);
            self.real_count = 0;
            c.call(Call::HiddenAppend, 0);
            c.call(Call::RowsRebuild, 0);
            self.count = c.call(Call::RowCount, 0) as u16;
            self.show_list();
            self.paint(c);
            return;
        }
        c.call(Call::ScanReserve, result as u32);
        for index in 0..result {
            if c.call(Call::ScanRead, index as u32) == 0 {
                continue;
            }
            let found = c.call(Call::ScanFind, 0);
            if found == u32::MAX {
                c.call(Call::ScanAppend, index as u32);
            } else if (c.call(Call::ScanRssi, u32::MAX) as i32)
                > (c.call(Call::ScanRssi, found) as i32)
            {
                c.call(Call::ScanReplace, found);
            }
        }
        c.call(Call::ScanSort, 0);
        self.real_count = c.call(Call::RowCount, 0) as u16;
        c.call(Call::HiddenAppend, 0);
        c.call(Call::RowsRebuild, 0);
        c.call(Call::ScanDelete, 0);
        self.count = c.call(Call::RowCount, 0) as u16;
        if self.automatic != 0 && self.manual_list == 0 && self.try_next(c) {
            return;
        }
        self.show_list();
        self.paint(c);
    }
    fn select_row(&mut self, c: &mut Services, index: i32) {
        if index < 0 || index >= i32::from(self.count) {
            return;
        }
        let flags = c.call(Call::RowFlags, index as u32);
        if flags & 2 != 0 {
            self.apply(c, Update::op(Operation::Hidden));
            return;
        }
        c.call(Call::SelectRow, index as u32);
        let found = c.call(Call::SelectedCredential, 0) != 0;
        let saved = found && c.call(Call::CredentialFlags, 0) & 2 != 0;
        let update = self.select(
            index,
            Resources {
                selected_encrypted: u8::from(flags & 4 != 0),
                saved_password_nonempty: u8::from(saved),
                ..Resources::default()
            },
        );
        self.apply(c, update);
        c.call(Call::DropCredential, 0);
    }
    fn connect(&mut self, c: &mut Services) {
        self.connecting(c.call(Call::Now, 0));
        c.call(Call::ConnectionReset, 0);
        self.paint(c);
        let password = self.requires_password != 0 && c.call(Call::PasswordPresent, 0) != 0;
        c.call(Call::ConnectionStart, u32::from(password));
    }
    fn try_credential(&mut self, c: &mut Services) -> bool {
        let flags = c.call(Call::CredentialFlags, 0);
        if flags & 1 != 0 {
            return false;
        }
        c.call(Call::CopyAutoCredential, 0);
        self.credential(false, flags & 2 != 0);
        self.connect(c);
        self.paint(c);
        true
    }
    fn try_next(&mut self, c: &mut Services) -> bool {
        for index in 0..self.count {
            if c.call(Call::RowFlags, u32::from(index)) & 1 == 0
                || c.call(Call::RowAttempted, u32::from(index)) != 0
            {
                continue;
            }
            let found = c.call(Call::RowCredential, u32::from(index)) != 0;
            let started = found && self.try_credential(c);
            c.call(Call::DropCredential, 0);
            if started {
                return true;
            }
        }
        false
    }
    fn check_connection(&mut self, c: &mut Services) {
        let status = c.call(Call::ConnectionStatus, 0) as i32;
        let now = if matches!(status, 1 | 3 | 4) {
            0
        } else {
            c.call(Call::Now, 0)
        };
        let update = self.connection(status, now);
        self.apply(c, update);
    }
}

impl State {
    /// Viewport feedback may arrive while a transport call is in progress.
    pub fn publish_model(&mut self, next: &mut Self) {
        if self.generation == next.generation {
            next.list.top = self.list.top;
            next.list.visible = self.list.visible;
            next.list.drawn = self.list.drawn;
            next.list.drawn_count = self.list.drawn_count;
            if self.ui_revision == next.ui_revision {
                next.ui_invalidated = self.ui_invalidated;
                next.flash_ticks = self.flash_ticks;
                next.flash_index = self.flash_index;
            }
            if self.list.navigation_revision == next.list.navigation_revision {
                next.list.pending_scroll = self.list.pending_scroll;
                next.list.follow = self.list.follow;
            }
        }
        *self = *next;
    }
    pub fn publish_view(&mut self, source: &Self, next: &ListState) {
        if self.generation == source.generation && self.phase == source.phase {
            self.list.publish(next, source.list.pending_scroll);
            if self.list.navigation_revision == source.list.navigation_revision {
                self.list.follow = next.follow;
            }
        }
    }
}

impl State {
    fn touch_edges(&mut self, input: Input, routed: bool) {
        if !routed {
            return;
        }
        if input.touch_flags & model::TOUCH_PRESSED != 0 {
            self.list.active = if (1..=3).contains(&input.action) {
                input.value
            } else {
                -1
            };
        }
        if input.touch_flags & model::TOUCH_RELEASED != 0 {
            self.list.active = -1;
        }
    }
    fn dispatched(&mut self, flash: bool, index: i32) {
        self.ui_revision = self.ui_revision.wrapping_add(1);
        self.ui_invalidated = 1;
        self.flash_ticks = if flash { 2 } else { 0 };
        self.flash_index = if flash { index } else { -1 };
    }
    pub fn prepare_view(&mut self) {
        if !matches!(
            Phase::read(self.phase),
            Some(Phase::List | Phase::Save | Phase::Forget)
        ) {
            return;
        }
        self.ui_invalidated = 0;
        self.flash_ticks = self.flash_ticks.saturating_sub(1);
        if self.flash_ticks == 0 {
            self.flash_index = -1;
        }
    }
}

pub fn signal_strength(rssi: i32) -> u8 {
    match rssi {
        -50.. => 0,
        -60..=-51 => 1,
        -70..=-61 => 2,
        _ => 3,
    }
}

const _: () = {
    assert!(core::mem::size_of::<State>() == 72);
    assert!(core::mem::offset_of!(State, phase) == 64);
    assert!(core::mem::offset_of!(State, generation) == 56);
    assert!(core::mem::size_of::<Services>() == 3 * core::mem::size_of::<usize>());
    assert!(Phase::AutoConnecting as u32 == 0);
    assert!(Phase::Scanning as u32 == 1);
    assert!(Phase::List as u32 == 2);
    assert!(Phase::HiddenSsid as u32 == 3);
    assert!(Phase::Password as u32 == 4);
    assert!(Phase::Connecting as u32 == 5);
    assert!(Phase::Connected as u32 == 6);
    assert!(Phase::Save as u32 == 7);
    assert!(Phase::Failed as u32 == 8);
    assert!(Phase::Forget as u32 == 9);
    assert!(Call::Now as u32 == 0);
    assert!(Call::Paint as u32 == 1);
    assert!(Call::EntryReset as u32 == 2);
    assert!(Call::ReadMac as u32 == 3);
    assert!(Call::Exit as u32 == 4);
    assert!(Call::LastCredential as u32 == 5);
    assert!(Call::CredentialFlags as u32 == 6);
    assert!(Call::CopyAutoCredential as u32 == 7);
    assert!(Call::DropCredential as u32 == 8);
    assert!(Call::ClearScan as u32 == 9);
    assert!(Call::WifiSta as u32 == 10);
    assert!(Call::Disconnect as u32 == 11);
    assert!(Call::Delay as u32 == 12);
    assert!(Call::ScanBegin as u32 == 13);
    assert!(Call::ScanComplete as u32 == 14);
    assert!(Call::ScanReserve as u32 == 15);
    assert!(Call::ScanRead as u32 == 16);
    assert!(Call::ScanFind as u32 == 17);
    assert!(Call::ScanAppend as u32 == 18);
    assert!(Call::ScanRssi as u32 == 19);
    assert!(Call::ScanReplace as u32 == 20);
    assert!(Call::ScanSort as u32 == 21);
    assert!(Call::RowCount as u32 == 22);
    assert!(Call::HiddenAppend as u32 == 23);
    assert!(Call::RowsRebuild as u32 == 24);
    assert!(Call::ScanDelete as u32 == 25);
    assert!(Call::RowFlags as u32 == 26);
    assert!(Call::RowAttempted as u32 == 27);
    assert!(Call::RowCredential as u32 == 28);
    assert!(Call::SelectRow as u32 == 29);
    assert!(Call::SelectedCredential as u32 == 30);
    assert!(Call::CopyPassword as u32 == 31);
    assert!(Call::PasswordPresent as u32 == 32);
    assert!(Call::LogSaved as u32 == 33);
    assert!(Call::HiddenReset as u32 == 34);
    assert!(Call::Keyboard as u32 == 35);
    assert!(Call::ConnectionReset as u32 == 36);
    assert!(Call::ConnectionStart as u32 == 37);
    assert!(Call::ConnectionStatus as u32 == 38);
    assert!(Call::ConnectedDetails as u32 == 39);
    assert!(Call::SetError as u32 == 40);
    assert!(Call::LogComplete as u32 == 41);
    assert!(Call::Complete as u32 == 42);
    assert!(Call::LogAutoFailure as u32 == 43);
    assert!(Call::LogManual as u32 == 44);
    assert!(Call::Save as u32 == 45);
    assert!(Call::Forget as u32 == 46);
    assert!(Call::CopySelected as u32 == 47);
    assert!(Call::Haptic as u32 == 48);
    assert!(Operation::None as u32 == 0);
    assert!(Operation::Scan as u32 == 1);
    assert!(Operation::CheckScan as u32 == 2);
    assert!(Operation::CheckConnection as u32 == 3);
    assert!(Operation::Cancel as u32 == 4);
    assert!(Operation::CancelScan as u32 == 5);
    assert!(Operation::CancelConnection as u32 == 6);
    assert!(Operation::Complete as u32 == 7);
    assert!(Operation::Select as u32 == 8);
    assert!(Operation::Hidden as u32 == 9);
    assert!(Operation::Password as u32 == 10);
    assert!(Operation::Connect as u32 == 11);
    assert!(Operation::ConnectSaved as u32 == 12);
    assert!(Operation::HiddenLookup as u32 == 13);
    assert!(Operation::TrySaved as u32 == 14);
    assert!(Operation::AutoFailure as u32 == 15);
    assert!(Operation::ManualList as u32 == 16);
    assert!(Operation::SaveComplete as u32 == 17);
    assert!(Operation::RemoveScan as u32 == 18);
    assert!(Operation::ForgetSelected as u32 == 19);
    assert!(Operation::Connected as u32 == 20);
    assert!(Operation::ErrorGeneral as u32 == 21);
    assert!(Operation::ErrorMissing as u32 == 22);
    assert!(Operation::ErrorTimeout as u32 == 23);
};
