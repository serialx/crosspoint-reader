use crate::model::{self, Effect, EffectKind, Input, Interest};
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Phase {
    Wifi,
    Connecting,
    Syncing,
    Result,
    Uploading,
    Uploaded,
    Synchronized,
    Missing,
    Failed,
    NoCredentials,
    Authenticating,
    Authenticated,
}
impl Phase {
    pub fn from_wire(v: u8) -> Option<Self> {
        Some(match v {
            0 => Self::Wifi,
            1 => Self::Connecting,
            2 => Self::Syncing,
            3 => Self::Result,
            4 => Self::Uploading,
            5 => Self::Uploaded,
            6 => Self::Synchronized,
            7 => Self::Missing,
            8 => Self::Failed,
            9 => Self::NoCredentials,
            10 => Self::Authenticating,
            11 => Self::Authenticated,
            _ => return None,
        })
    }
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Message {
    None,
    Hash,
    HashFailed,
    Clippings,
    ClippingsFailed,
    Fetch,
    Upload,
    SaveFailed,
    WifiFailed,
    AuthBusy,
    AuthOk,
    UserExists,
    ServiceError,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub auto_return_at: u32,
    pub kind: u8,
    pub phase: u8,
    pub selected: u8,
    pub smart: u8,
    pub plugin: u8,
    pub wifi_activated: u8,
    pub touch_armed: u8,
    pub pending_back: u8,
}
impl State {
    pub fn busy(&self) -> bool {
        matches!(
            Phase::from_wire(self.phase),
            Some(Phase::Connecting | Phase::Syncing | Phase::Uploading | Phase::Authenticating)
        )
    }
    pub fn publish(&mut self, phase: u8, selected: u8) {
        if Phase::from_wire(phase).is_some() {
            self.phase = phase;
            self.selected = selected.min(1);
            self.touch_armed = 0;
        }
    }
    pub fn mark_return(&mut self, now: u32) {
        if self.smart != 0 || self.plugin != 0 {
            self.auto_return_at = now.wrapping_add(1200);
        }
    }
    pub fn interest(&self, now: u32) -> Interest {
        if self.kind != 0 {
            return Interest {
                pressed: model::BACK | model::CONFIRM,
                touch_flags: model::TOUCH_RELEASED,
                ..Interest::default()
            };
        }
        // The original compares unsigned timestamps directly, including wrap.
        // Polling the timer preserves that behavior instead of normalizing it.
        Interest {
            released: model::BACK
                | model::CONFIRM
                | model::UP
                | model::DOWN
                | model::LEFT
                | model::RIGHT,
            touch_flags: model::TOUCH_PRESSED | model::TOUCH_RELEASED,
            deadline_ms: now,
            deadline_enabled: u8::from(self.auto_return_at != 0),
            ..Interest::default()
        }
    }
    pub fn update(&mut self, input: &Input) -> Effect {
        let mut out = Effect::default();
        if self.pending_back != 0 {
            self.pending_back = 0;
            out.kind = EffectKind::Cancel as u8;
            return out;
        }
        if self.kind != 0 {
            if matches!(
                Phase::from_wire(self.phase),
                Some(Phase::Authenticated | Phase::Failed)
            ) && (input.pressed & (model::BACK | model::CONFIRM) != 0
                || input.touch_flags & model::TOUCH_RELEASED != 0
                    && input.touch_x >= 0
                    && input.touch_y >= 0)
            {
                out.kind = EffectKind::Activate as u8;
                out.value = 3;
            }
            return out;
        }
        match Phase::from_wire(self.phase) {
            Some(Phase::NoCredentials | Phase::Failed | Phase::Uploaded | Phase::Synchronized) => {
                if self.auto_return_at != 0 && input.now_ms >= self.auto_return_at
                    || input.released & (model::BACK | model::CONFIRM) != 0
                {
                    out.kind = EffectKind::Cancel as u8;
                }
            }
            Some(Phase::Result | Phase::Missing) => {
                let count = if self.phase == Phase::Result as u8 {
                    2
                } else {
                    1
                };
                let hit = input.action == 1 && (0..count).contains(&input.value);
                if input.touch_flags & model::TOUCH_PRESSED != 0 {
                    let next = if hit { input.value as u8 + 1 } else { 0 };
                    self.touch_armed = next;
                }
                if input.touch_flags & model::TOUCH_RELEASED != 0 {
                    self.touch_armed = 0;
                    if hit {
                        self.selected = input.value as u8;
                        out.kind = EffectKind::Activate as u8;
                        out.value = if count == 1 { 2 } else { input.value };
                        return out;
                    }
                }
                if count == 2
                    && input.released & (model::UP | model::DOWN | model::LEFT | model::RIGHT) != 0
                {
                    self.selected ^= 1;
                    out.invalidate = 1;
                }
                if input.released & model::CONFIRM != 0 {
                    out.kind = EffectKind::Activate as u8;
                    out.value = if count == 1 {
                        2
                    } else {
                        i32::from(self.selected)
                    };
                    self.pending_back = u8::from(input.released & model::BACK != 0);
                } else if input.released & model::BACK != 0 {
                    out.kind = EffectKind::Cancel as u8;
                }
            }
            _ => {}
        }
        out
    }
}
pub fn edit(row: u8, value: u8, credentials: u8) -> Effect {
    let (kind, next) = match row {
        0..=2 | 4 | 5 => (4, i32::from(row)),
        3 => (5, 0),
        6 | 8 => (6, i32::from(u8::from(value == 0))),
        7 | 9 => (6, i32::from(u8::from(value == 0))),
        10 | 11 if credentials != 0 => (7, i32::from(row == 10)),
        _ => (0, 0),
    };
    Effect {
        kind,
        value: next,
        ..Effect::default()
    }
}
