use super::Feedback;
use crate::model::{
    EffectKind, Input, Interest, ListState, BACK, CONFIRM, NAV_NEXT, NAV_PREVIOUS, TOUCH_HELD,
    TOUCH_PRESSED, TOUCH_RELEASED,
};

pub const OTA: u8 = Kind::Ota as u8;
pub const SD: u8 = Kind::Sd as u8;
pub const FONTS: u8 = Kind::Fonts as u8;
pub const CACHE: u8 = Kind::Cache as u8;
pub mod ota {
    pub const WIFI: u8 = 0;
    pub const CHECKING: u8 = 1;
    pub const CONFIRM: u8 = 2;
    pub const PROGRESS: u8 = 3;
    pub const NO_UPDATE: u8 = 4;
    pub const FAILED: u8 = 5;
    pub const COMPLETE: u8 = 6;
    pub const SHUTDOWN: u8 = 7;
}
pub mod sd {
    pub const PICKER: u8 = 0;
    pub const VALIDATING: u8 = 1;
    pub const CONFIRM: u8 = 2;
    pub const PROGRESS: u8 = 3;
    pub const COMPLETE: u8 = 4;
    pub const FAILED: u8 = 5;
}
pub mod font {
    pub const WIFI: u8 = 0;
    pub const MANIFEST: u8 = 1;
    pub const GROUPS: u8 = 2;
    pub const FAMILIES: u8 = 3;
    pub const PROGRESS: u8 = 4;
    pub const COMPLETE: u8 = 5;
    pub const FAILED: u8 = 6;
}
pub mod cache {
    pub const WARNING: u8 = 0;
    pub const CLEARING: u8 = 1;
    pub const COMPLETE: u8 = 2;
    pub const FAILED: u8 = 3;
}
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Ota = 0,
    Sd = 1,
    Fonts = 2,
    Cache = 3,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Event {
    WifiResult = 0,
    OtaChecked = 1,
    InstallBegin = 2,
    Installed = 3,
    Shutdown = 4,
    Picked = 5,
    Validated = 6,
    Confirmed = 7,
    Progress = 8,
    Manifest = 9,
    Group = 10,
    FontBegin = 11,
    FontFile = 12,
    FontFileDone = 13,
    FontDone = 14,
    FontError = 15,
    FontAborted = 16,
    TransferInput = 17,
    CacheBegin = 18,
    CacheResult = 19,
    FontBatch = 20,
    SceneFailed = 21,
}
impl Event {
    pub fn from_byte(value: u8) -> Option<Self> {
        Some(match value {
            0 => Self::WifiResult,
            1 => Self::OtaChecked,
            2 => Self::InstallBegin,
            3 => Self::Installed,
            4 => Self::Shutdown,
            5 => Self::Picked,
            6 => Self::Validated,
            7 => Self::Confirmed,
            8 => Self::Progress,
            9 => Self::Manifest,
            10 => Self::Group,
            11 => Self::FontBegin,
            12 => Self::FontFile,
            13 => Self::FontFileDone,
            14 => Self::FontDone,
            15 => Self::FontError,
            16 => Self::FontAborted,
            17 => Self::TransferInput,
            18 => Self::CacheBegin,
            19 => Self::CacheResult,
            20 => Self::FontBatch,
            21 => Self::SceneFailed,
            _ => return None,
        })
    }
}
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Action {
    None = 0,
    Finish = 1,
    Picker = 2,
    Install = 5,
    FontRetry = 9,
    FontActivate = 10,
    EnterGroup = 11,
    Clear = 12,
    Restart = 13,
    Home = 14,
}
#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Effect {
    pub action: u8,
    pub repaint: u8,
    pub immediate: u8,
    pub reserved: u8,
    pub value: i32,
}
impl Effect {
    fn action(action: Action) -> Self {
        Self {
            action: action as u8,
            ..Self::default()
        }
    }
    fn repaint() -> Self {
        Self {
            repaint: 1,
            ..Self::default()
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub list: ListState,
    pub groups: ListState,
    pub processed: u32,
    pub total: u32,
    pub file_index: u32,
    pub file_count: u32,
    pub last_percent: u32,
    pub family_index: i32,
    pub kind: u8,
    pub phase: u8,
    pub recovery: u8,
    pub cancel_requested: u8,
    pub home_requested: u8,
    pub has_groups: u8,
    pub popup_selected: u8,
    pub popup_active: u8,
    pub touch_active: u8,
    pub reserved: [u8; 3],
}
impl State {
    pub fn valid(&self) -> bool {
        self.kind <= 3
            && self.phase
                <= match self.kind {
                    0 => 7,
                    1 => 5,
                    2 => 6,
                    _ => 3,
                }
            && self.popup_selected <= 1
            && self.touch_active <= 2
            && [
                self.recovery,
                self.cancel_requested,
                self.home_requested,
                self.has_groups,
                self.popup_active,
            ]
            .iter()
            .all(|v| *v <= 1)
    }
    fn scene_failed(&mut self) {
        self.touch_active = 0;
        self.list.active = -1;
        self.groups.active = -1;
        self.list.pending_scroll = 0;
        self.groups.pending_scroll = 0;
        self.list.last_repeat = 0;
        self.groups.last_repeat = 0;
    }

    pub fn new(kind: u8, recovery: u8) -> Self {
        Self {
            kind,
            recovery: u8::from(recovery != 0),
            list: ListState::new(0),
            groups: ListState::new(0),
            last_percent: if kind == Kind::Ota as u8 { 111 } else { 101 },
            popup_active: u8::from(kind == Kind::Cache as u8),
            ..Self::default()
        }
    }
    pub fn is_list(&self) -> bool {
        self.kind == FONTS && matches!(self.phase, font::GROUPS | font::FAMILIES)
    }
    pub fn nav(&self) -> &ListState {
        if self.phase == font::GROUPS {
            &self.groups
        } else {
            &self.list
        }
    }
    pub fn nav_mut(&mut self) -> &mut ListState {
        if self.phase == font::GROUPS {
            &mut self.groups
        } else {
            &mut self.list
        }
    }
    pub fn popup(&self) -> bool {
        self.kind == OTA && self.phase == ota::CONFIRM
            || self.kind == CACHE && self.phase == cache::WARNING
    }
    pub fn sleep(&self) -> bool {
        match self.kind {
            OTA => matches!(self.phase, ota::CHECKING | ota::PROGRESS),
            SD => matches!(self.phase, sd::VALIDATING | sd::PROGRESS),
            FONTS => matches!(
                self.phase,
                font::MANIFEST | font::PROGRESS | font::COMPLETE | font::FAILED
            ),
            _ => false,
        }
    }
    pub fn delay(&self) -> bool {
        self.kind != SD || self.phase == sd::PROGRESS
    }
    pub fn percent(&self) -> u32 {
        if self.kind == Kind::Sd as u8 {
            self.processed
                .wrapping_mul(100)
                .checked_div(self.total)
                .unwrap_or(0)
        } else if self.total != 0 {
            (self.processed as f32 / self.total as f32 * 100.0) as i32 as u32
        } else {
            0
        }
    }
    pub fn service(&mut self, event: Event, a: u32, b: u32) -> u8 {
        match event {
            Event::SceneFailed => {
                self.scene_failed();
                Action::None as u8
            }
            Event::WifiResult => {
                if a == 0 {
                    return Action::Finish as u8;
                }
                self.phase = if self.kind == OTA {
                    ota::CHECKING
                } else {
                    font::MANIFEST
                };
                Action::None as u8
            }
            Event::OtaChecked => {
                if a == 1 || a == 0 && b == 0 {
                    self.phase = ota::NO_UPDATE;
                    return Action::None as u8;
                }
                if a != 0 {
                    self.phase = ota::FAILED;
                    return Action::None as u8;
                }
                self.phase = ota::CONFIRM;
                self.popup_active = 1;
                self.popup_selected = 1;
                self.touch_active = 0;
                Action::None as u8
            }
            Event::InstallBegin => {
                self.phase = ota::PROGRESS;
                Action::None as u8
            }
            Event::Installed => {
                self.phase = if a == 0 {
                    if self.kind == SD {
                        sd::COMPLETE
                    } else {
                        ota::COMPLETE
                    }
                } else if self.kind == SD {
                    sd::FAILED
                } else {
                    ota::FAILED
                };
                Action::None as u8
            }
            Event::Shutdown => {
                self.phase = ota::SHUTDOWN;
                Action::None as u8
            }
            Event::Picked => {
                if a != 0 {
                    return if self.recovery != 0 {
                        Action::Picker as u8
                    } else {
                        Action::Finish as u8
                    };
                }
                if b == 0 {
                    return Action::Finish as u8;
                }
                self.phase = sd::VALIDATING;
                Action::None as u8
            }
            Event::Validated => {
                if a == 0 {
                    self.phase = sd::FAILED;
                    Action::None as u8
                } else {
                    self.phase = sd::CONFIRM;
                    Action::None as u8
                }
            }
            Event::Confirmed => {
                if a != 0 {
                    return if self.recovery != 0 {
                        Action::Picker as u8
                    } else {
                        Action::Finish as u8
                    };
                }
                self.phase = sd::PROGRESS;
                self.processed = 0;
                self.last_percent = 101;
                Action::None as u8
            }
            Event::Progress => {
                self.processed = a;
                self.total = b;
                Action::None as u8
            }
            Event::Manifest => {
                self.has_groups = u8::from(b != 0);
                self.phase = if a == 0 {
                    font::FAILED
                } else if b != 0 {
                    self.groups = ListState::new(0);
                    font::GROUPS
                } else {
                    self.list = ListState::new(0);
                    font::FAMILIES
                };
                Action::None as u8
            }
            Event::Group => {
                self.list = ListState::new(0);
                self.phase = font::FAMILIES;
                Action::None as u8
            }
            Event::FontBegin => {
                self.phase = font::PROGRESS;
                self.family_index = a as i32;
                self.processed = 0;
                self.total = 0;
                self.cancel_requested = 0;
                self.home_requested = 0;
                Action::None as u8
            }
            Event::FontFile => {
                self.processed = 0;
                self.total = a;
                Action::None as u8
            }
            Event::FontFileDone => {
                self.file_index = self.file_index.wrapping_add(1);
                Action::None as u8
            }
            Event::FontDone => {
                self.phase = font::COMPLETE;
                Action::None as u8
            }
            Event::FontError => {
                self.phase = font::FAILED;
                Action::None as u8
            }
            Event::FontAborted => {
                if self.home_requested != 0 {
                    return Action::Home as u8;
                }
                self.phase = font::FAMILIES;
                Action::None as u8
            }
            Event::TransferInput => {
                if a != 0 || b != 0 {
                    self.cancel_requested = 1;
                }
                if b != 0 {
                    self.home_requested = 1;
                }
                Action::None as u8
            }
            Event::FontBatch => {
                self.file_index = 0;
                self.file_count = a;
                self.cancel_requested = 0;
                Action::None as u8
            }
            Event::CacheBegin => {
                self.phase = cache::CLEARING;
                Action::None as u8
            }
            Event::CacheResult => {
                self.phase = if a == 0 {
                    cache::FAILED
                } else {
                    cache::COMPLETE
                };
                Action::None as u8
            }
        }
    }
    pub fn interest(&self, now: u32) -> Interest {
        if self.is_list() {
            return self.nav().interest(now);
        }
        if self.popup() {
            return Interest {
                pressed: NAV_PREVIOUS | NAV_NEXT,
                released: CONFIRM | BACK,
                touch_flags: TOUCH_PRESSED | TOUCH_RELEASED | TOUCH_HELD,
                deadline_enabled: u8::from(self.popup_active == 0),
                deadline_ms: now,
                ..Interest::default()
            };
        }
        Interest {
            pressed: BACK | CONFIRM,
            touch_flags: TOUCH_RELEASED,
            deadline_enabled: u8::from(self.kind == OTA && self.phase == ota::SHUTDOWN),
            deadline_ms: now,
            ..Interest::default()
        }
    }
    pub fn input(
        &mut self,
        mut input: Input,
        feedback: &Feedback,
        published: bool,
        row_count: i32,
        family_count: i32,
    ) -> Effect {
        if self.popup() {
            return self.popup_input(input, feedback, published);
        }
        if self.is_list() {
            if input.touch_flags != 0 {
                input.action = 0;
                if published {
                    if let Some(hit) = feedback.rows.hit(input.touch_x, input.touch_y) {
                        input.action = hit.action;
                        input.value = hit.value;
                    }
                }
            }
            let list_effect = self.nav_mut().update(input, row_count, true);
            match list_effect.kind {
                k if k == EffectKind::Cancel as u8 => {
                    if self.phase == font::FAMILIES && self.has_groups != 0 {
                        self.phase = font::GROUPS;
                        Effect::repaint()
                    } else {
                        Effect::action(Action::Finish)
                    }
                }
                k if k == EffectKind::Activate as u8 => Effect {
                    value: list_effect.value,
                    ..Effect::action(if self.phase == font::GROUPS {
                        Action::EnterGroup
                    } else {
                        Action::FontActivate
                    })
                },
                _ => Effect {
                    repaint: list_effect.invalidate,
                    ..Effect::default()
                },
            }
        } else {
            let tap =
                input.touch_flags & TOUCH_RELEASED != 0 && input.touch_x >= 0 && input.touch_y >= 0;
            let back = input.pressed & BACK != 0;
            let confirm = input.pressed & CONFIRM != 0;
            match (self.kind, self.phase) {
                (OTA, ota::NO_UPDATE | ota::FAILED) | (CACHE, cache::COMPLETE | cache::FAILED)
                    if back || tap =>
                {
                    Effect::action(Action::Finish)
                }
                (OTA, ota::SHUTDOWN) => Effect::action(Action::Restart),
                (SD, sd::FAILED) if back || confirm || tap => {
                    if self.recovery != 0 {
                        self.phase = sd::PICKER;
                        Effect::action(Action::Picker)
                    } else {
                        Effect::action(Action::Finish)
                    }
                }
                (FONTS, font::COMPLETE) if back || confirm || tap => {
                    self.phase = font::FAMILIES;
                    Effect::repaint()
                }
                (FONTS, font::FAILED) if back || confirm || tap => {
                    if !back && self.family_index >= 0 && self.family_index < family_count {
                        return Effect {
                            value: self.family_index,
                            ..Effect::action(Action::FontRetry)
                        };
                    }
                    self.phase = font::FAMILIES;
                    Effect::repaint()
                }
                _ => Effect::default(),
            }
        }
    }
    fn popup_input(&mut self, input: Input, f: &Feedback, published: bool) -> Effect {
        if self.popup_active == 0 {
            return Effect::action(Action::Finish);
        }
        if input.touch_flags != 0 {
            if !published {
                return Effect::default();
            }
            let hit = f
                .options
                .iter()
                .position(|r| r.contains(input.touch_x, input.touch_y));
            let mut repaint = false;
            if input.touch_flags & TOUCH_PRESSED != 0 {
                self.touch_active = hit.map_or(0, |v| v as u8 + 1);
                if let Some(n) = hit {
                    if self.popup_selected != n as u8 {
                        self.popup_selected = n as u8;
                        repaint = true;
                    }
                }
            }
            if input.touch_flags & TOUCH_RELEASED != 0 {
                self.touch_active = 0;
                if let Some(n) = hit {
                    self.popup_selected = n as u8;
                    return self.choose();
                }
                if !f.popup.contains(input.touch_x, input.touch_y) && input.touch_x >= 0 {
                    self.popup_active = 0;
                    return if self.kind == Kind::Cache as u8 {
                        Effect {
                            repaint: 2,
                            ..Effect::action(Action::Finish)
                        }
                    } else {
                        Effect::repaint()
                    };
                }
            }
            return if repaint {
                Effect::repaint()
            } else {
                Effect::default()
            };
        }
        if input.pressed & (NAV_PREVIOUS | NAV_NEXT) != 0 {
            self.popup_selected = 1 - self.popup_selected;
            return Effect::repaint();
        }
        if input.released & CONFIRM != 0 {
            return self.choose();
        }
        if input.released & BACK != 0 {
            self.popup_active = 0;
            return if self.kind == Kind::Cache as u8 {
                Effect {
                    repaint: 2,
                    ..Effect::action(Action::Finish)
                }
            } else {
                Effect::repaint()
            };
        }
        Effect::default()
    }
    fn choose(&mut self) -> Effect {
        self.popup_active = 0;
        if self.popup_selected == 0 {
            return Effect {
                repaint: if self.kind == Kind::Cache as u8 { 2 } else { 1 },
                ..Effect::action(Action::Finish)
            };
        }
        Effect {
            repaint: 1,
            ..Effect::action(if self.kind == Kind::Ota as u8 {
                Action::Install
            } else {
                Action::Clear
            })
        }
    }
}
