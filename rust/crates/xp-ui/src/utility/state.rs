use crate::{
    model::{self, Effect, EffectKind, Input, Interest},
    scene::{Interaction, Rect},
};

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Screen {
    Confirmation,
    Interval,
    Details,
    Frontlight,
    Qr,
}
impl Screen {
    pub fn from_byte(n: u8) -> Option<Self> {
        Some(match n {
            0 => Self::Confirmation,
            1 => Self::Interval,
            2 => Self::Details,
            3 => Self::Frontlight,
            4 => Self::Qr,
            _ => return None,
        })
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub value: i32,
    pub minimum: i32,
    pub maximum: i32,
    pub small_step: i32,
    pub large_step: i32,
    pub last_repeat: u32,
    pub active_action: u32,
    pub active_value: i32,
    pub clean_revision: u32,
    pub kind: u8,
    pub selected: u8,
    pub dragging: u8,
    pub dismissed: u8,
    pub brightness: u8,
    pub warmth: u8,
    pub light_on: u8,
    pub light_changed: u8,
    pub inverted: u8,
    pub orientation: u8,
    pub touch_mode: u8,
    pub touch_restore: u8,
    pub edge_buttons: u8,
    pub switch_owns_light: u8,
    pub contact_held: u8,
    pub flash_pending: u8,
    pub last_drag_x: i16,
    pub touch_revision: u16,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Update {
    pub navigation: Effect,
    pub writes: u32,
}
pub const WRITE_BRIGHTNESS: u32 = 1;
pub const WRITE_WARMTH: u32 = 2;
pub const WRITE_LIGHT: u32 = 4;
pub const WRITE_SETTINGS: u32 = 8;
pub const CLEAN_PARENT: u32 = 16;
pub const CHROME: u32 = 100;
pub const OPTION: u32 = 101;
pub const SLIDER: u32 = 102;
pub const STEP: u32 = 103;
pub const OK: u32 = 104;
pub const PRIMARY: u32 = 105;
pub const MORE: u32 = 106;
pub const BRIGHTNESS: u32 = 107;
pub const WARMTH: u32 = 108;
pub const TOGGLE: u32 = 109;
pub const BRIGHTNESS_STEP: u32 = 110;
pub const WARMTH_STEP: u32 = 111;
pub const TILE: u32 = 112;
pub const DRAG: u16 = 2;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Feedback {
    pub cover: Rect,
    pub description: Rect,
    pub panel_bottom: i16,
    pub reserved: u16,
    pub clean_revision: u32,
}
impl Update {
    fn finish(kind: EffectKind, value: i32) -> Self {
        Self {
            navigation: Effect {
                kind: kind as u8,
                value,
                ..Effect::default()
            },
            writes: 0,
        }
    }
    fn redraw() -> Self {
        Self {
            navigation: Effect {
                invalidate: 1,
                ..Effect::default()
            },
            writes: 0,
        }
    }
}
impl State {
    pub fn init(&mut self) -> bool {
        let Some(screen) = Screen::from_byte(self.kind) else {
            return false;
        };
        if screen == Screen::Interval {
            if self.minimum > self.maximum
                || i64::from(self.maximum) - i64::from(self.minimum) > i64::from(i32::MAX)
                || self.small_step < 0
                || self.large_step < 0
            {
                return false;
            }
            self.value = self.value.clamp(self.minimum, self.maximum);
        }
        self.brightness = self.brightness.max(1);
        self.touch_restore = if self.touch_mode != 0 {
            self.touch_mode
        } else {
            1
        };
        self.active_action = 0;
        self.last_drag_x = -1;
        true
    }
    pub fn interest(&self, now: u32) -> Interest {
        let Some(screen) = Screen::from_byte(self.kind) else {
            return Interest::default();
        };
        let nav = match screen {
            Screen::Interval => model::LEFT | model::RIGHT | model::UP | model::DOWN,
            Screen::Frontlight => model::LEFT | model::RIGHT,
            Screen::Confirmation => model::NAV_NEXT | model::NAV_PREVIOUS,
            _ => 0,
        };
        let repeat = matches!(screen, Screen::Interval | Screen::Frontlight);
        let timer = self.dismissed != 0 || (self.dragging != 0 && self.contact_held == 0);
        Interest {
            pressed: nav,
            released: model::BACK
                | model::CONFIRM
                | nav
                | if screen == Screen::Details {
                    model::RIGHT | model::DOWN
                } else {
                    0
                },
            held: if repeat && !timer { nav } else { 0 },
            held_after_ms: 501,
            deadline_ms: if timer || now.wrapping_sub(self.last_repeat) > 500 {
                now
            } else {
                self.last_repeat.wrapping_add(501)
            },
            deadline_enabled: u8::from(repeat || timer),
            touch_flags: 7,
            swipes: if screen == Screen::Frontlight {
                1 << model::SWIPE_UP
            } else {
                0
            },
            reserved: 0,
        }
    }
    fn brightness(&mut self, v: i32, always: bool) -> Update {
        let v = v.clamp(1, 100) as u8;
        if v == self.brightness && !always {
            return Update::default();
        }
        self.brightness = v;
        let mut e = Update::redraw();
        e.writes = WRITE_BRIGHTNESS;
        if self.light_on == 0 && self.switch_owns_light == 0 {
            self.light_on = 1;
            self.light_changed = 1;
            e.writes |= WRITE_LIGHT;
        }
        e
    }
    fn warmth(&mut self, v: i32, always: bool) -> Update {
        let v = v.clamp(0, 100) as u8;
        if v == self.warmth && !always {
            return Update::default();
        }
        self.warmth = v;
        let mut e = Update::redraw();
        e.writes = WRITE_WARMTH;
        e
    }
    fn toggle(&mut self) -> Update {
        self.light_on ^= 1;
        self.light_changed = 1;
        let mut e = Update::redraw();
        e.writes = WRITE_LIGHT;
        e
    }
    fn tile(&mut self, n: i32) -> Update {
        let mut e = Update::redraw();
        e.writes = WRITE_SETTINGS;
        match n {
            0 => {
                self.inverted = u8::from(self.inverted == 0);
                self.clean_revision = self.clean_revision.wrapping_add(1)
            }
            1 => {
                e = Update::finish(EffectKind::Cancel, 0);
                e.writes = CLEAN_PARENT;
            }
            2 => self.orientation = ((u16::from(self.orientation) + 1) % 4) as u8,
            3 => {
                if self.touch_mode != 0 {
                    self.touch_restore = self.touch_mode;
                    self.touch_mode = 0
                } else {
                    self.touch_mode = self.touch_restore
                }
            }
            _ => return Update::default(),
        }
        e
    }
    fn step(&mut self, delta: i32) -> Update {
        self.value = self
            .value
            .saturating_add(delta)
            .clamp(self.minimum, self.maximum);
        Update::redraw()
    }
    fn event(&mut self, action: u32, value: i32, permille: Option<i32>) -> Update {
        match action {
            OPTION => Update::finish(
                if value == 1 {
                    EffectKind::Activate
                } else {
                    EffectKind::Cancel
                },
                value,
            ),
            SLIDER => {
                let Some(p) = permille else {
                    return Update::default();
                };
                let range = (i64::from(self.maximum) - i64::from(self.minimum)).max(1);
                let v = (i64::from(self.minimum) + (i64::from(p) * range + 500) / 1000)
                    .clamp(i64::from(self.minimum), i64::from(self.maximum))
                    as i32;
                if v == self.value {
                    Update::default()
                } else {
                    self.value = v;
                    Update::redraw()
                }
            }
            STEP => self.step(value.saturating_mul(self.small_step)),
            OK => Update::finish(EffectKind::Activate, self.value),
            PRIMARY => Update::finish(EffectKind::Activate, 1),
            MORE => Update::finish(EffectKind::Activate, 2),
            BRIGHTNESS => permille.map_or_else(Update::default, |p| {
                self.brightness((p * 100 + 500) / 1000, true)
            }),
            WARMTH => permille.map_or_else(Update::default, |p| {
                self.warmth((p * 100 + 500) / 1000, true)
            }),
            BRIGHTNESS_STEP => self.brightness(i32::from(self.brightness) + value, false),
            WARMTH_STEP => self.warmth(i32::from(self.warmth) + value, false),
            TOGGLE => self.toggle(),
            TILE => self.tile(value),
            _ => Update::default(),
        }
    }
    pub fn flashing(&self, action: u32, value: i32) -> bool {
        self.flash_pending != 0 && self.flash_pending == flash_code(action, value)
    }
    pub fn publish(&mut self, rendered: &Self) {
        if self.touch_revision == rendered.touch_revision && rendered.flash_pending != 0 {
            self.flash_pending = 0;
        }
    }
    pub fn update(
        &mut self,
        input: &Input,
        hits: Option<&[Interaction]>,
        feedback: &Feedback,
    ) -> Update {
        let Some(screen) = Screen::from_byte(self.kind) else {
            return Update::default();
        };
        if self.dismissed != 0 {
            return Update::finish(EffectKind::Cancel, 0);
        }
        let pressed = input.touch_flags & model::TOUCH_PRESSED != 0;
        let released = input.touch_flags & model::TOUCH_RELEASED != 0;
        let held = input.touch_flags & model::TOUCH_HELD != 0;
        if screen == Screen::Qr {
            return if input.released & (model::BACK | model::CONFIRM) != 0
                || released && input.touch_x >= 0
            {
                Update::finish(EffectKind::Cancel, 0)
            } else {
                Update::default()
            };
        }
        let touch = pressed || released || held;
        let routed = hits.is_some() && touch;
        if let Some(hits) = hits.filter(|_| routed) {
            let hit_at = |mask| {
                hits.iter()
                    .rev()
                    .find(|h| h.inputs & mask != 0 && h.rect.contains(input.touch_x, input.touch_y))
            };
            if held && self.contact_held == 0 {
                self.active_action = 0;
                self.last_drag_x = -1;
                if let Some(h) = hit_at(DRAG) {
                    self.active_action = h.action;
                    self.active_value = h.value;
                }
            }
            if held {
                self.contact_held = 1
            }
            if released {
                self.contact_held = 0
            }
            if pressed {
                self.active_action = 0;
                if let Some(h) = hit_at(1) {
                    self.active_action = h.action;
                    self.active_value = h.value;
                }
            }
            let active = hits
                .iter()
                .find(|h| h.action == self.active_action && h.value == self.active_value);
            let drag = active
                .filter(|h| h.inputs & DRAG != 0 && (held || released && self.last_drag_x >= 0));
            let event = if let Some(h) = drag {
                let x = if held {
                    input.touch_x
                } else {
                    self.last_drag_x
                };
                if held {
                    self.last_drag_x = x
                } else {
                    self.active_action = 0;
                    self.last_drag_x = -1
                }
                Some((h, Some(permille(h.rect, x))))
            } else if released {
                self.last_drag_x = -1;
                self.active_action = 0;
                hit_at(1).map(|h| {
                    (
                        h,
                        if h.inputs & DRAG != 0 {
                            Some(permille(h.rect, input.touch_x))
                        } else {
                            None
                        },
                    )
                })
            } else {
                None
            };
            if let Some((h, p)) = event {
                if p.is_some() {
                    self.dragging = 1
                }
                let mut e = self.event(h.action, h.value, p);
                if screen != Screen::Confirmation {
                    e.navigation.invalidate = 1;
                    if p.is_none() && e.navigation.kind == 0 {
                        self.flash_pending = flash_code(h.action, h.value);
                        self.touch_revision = self.touch_revision.wrapping_add(1);
                    }
                }
                if released && (screen != Screen::Confirmation || h.action == OPTION) {
                    e.navigation.reserved[0] = 1
                }
                return e;
            }
            if screen == Screen::Confirmation {
                if released && input.touch_x >= 0 {
                    self.dismissed = 1;
                    let mut e = Update::default();
                    e.navigation.reserved[0] = 1;
                    return e;
                }
                if pressed
                    && self.active_action == OPTION
                    && i32::from(self.selected) != self.active_value
                {
                    self.selected = self.active_value as u8;
                    return Update::redraw();
                }
                return Update::default();
            }
            if screen == Screen::Frontlight
                && self.dragging == 0
                && (input.swipe == model::SWIPE_UP
                    || released
                        && feedback.panel_bottom > 0
                        && input.touch_y >= feedback.panel_bottom)
            {
                return Update::finish(EffectKind::Cancel, 0);
            }
        }
        if screen == Screen::Confirmation && touch {
            return Update::default();
        }
        if self.dragging != 0 && (screen == Screen::Frontlight || hits.is_some()) {
            if !held {
                self.dragging = 0;
                if screen == Screen::Frontlight {
                    self.clean_revision = self.clean_revision.wrapping_add(1);
                    return Update::redraw();
                }
            }
            return Update::default();
        }
        if screen == Screen::Interval && routed && released && input.touch_x >= 0 {
            return Update::finish(EffectKind::Cancel, 0);
        }
        if screen == Screen::Confirmation {
            if input.pressed & (model::NAV_PREVIOUS | model::NAV_NEXT) != 0 {
                self.selected ^= 1;
                return Update::redraw();
            }
            if input.released & model::CONFIRM != 0 {
                return self.event(OPTION, i32::from(self.selected), None);
            }
            if input.released & model::BACK != 0 {
                self.dismissed = 1
            }
            return Update::default();
        }
        if input.released & model::BACK != 0 {
            return Update::finish(EffectKind::Cancel, 0);
        }
        if input.released & model::CONFIRM != 0 {
            return match screen {
                Screen::Frontlight => {
                    if self.switch_owns_light == 0 {
                        self.toggle()
                    } else {
                        Update::default()
                    }
                }
                Screen::Interval => Update::finish(EffectKind::Activate, self.value),
                _ => Update::finish(EffectKind::Activate, 1),
            };
        }
        if screen == Screen::Details {
            return if input.released & (model::RIGHT | model::DOWN) != 0 {
                Update::finish(EffectKind::Activate, 2)
            } else {
                Update::default()
            };
        }
        let up = if self.edge_buttons != 0 {
            -self.large_step
        } else {
            self.large_step
        };
        let mut out = Update::default();
        for (button, delta) in [
            (model::LEFT, -self.small_step),
            (model::RIGHT, self.small_step),
            (model::UP, up),
            (model::DOWN, -up),
        ] {
            if screen == Screen::Frontlight && button & (model::LEFT | model::RIGHT) == 0 {
                continue;
            }
            let press = input.pressed & button != 0;
            let repeat = !press
                && input.released & button == 0
                && input.held & button != 0
                && input.held_ms > 500
                && input.now_ms.wrapping_sub(self.last_repeat) > 500;
            if press || repeat {
                let e = if screen == Screen::Interval {
                    self.step(delta)
                } else {
                    self.brightness(
                        i32::from(self.brightness) + if button == model::LEFT { -1 } else { 1 },
                        false,
                    )
                };
                out.navigation.invalidate |= e.navigation.invalidate;
                out.writes |= e.writes;
            }
            if (input.pressed | input.released) & button != 0 {
                self.last_repeat = 0
            } else if repeat {
                self.last_repeat = input.now_ms
            }
        }
        out
    }
}
fn permille(r: Rect, x: i16) -> i32 {
    if r.w <= 1 {
        0
    } else {
        ((i32::from(x) - i32::from(r.x)) * 1000 / (i32::from(r.w) - 1)).clamp(0, 1000)
    }
}

// Each utility control has a fixed action/value pair; zero means no visual flash.
fn flash_code(action: u32, value: i32) -> u8 {
    match (action, value) {
        (STEP, -1) => 1,
        (STEP, 1) => 2,
        (BRIGHTNESS_STEP, -1) => 3,
        (BRIGHTNESS_STEP, 1) => 4,
        (WARMTH_STEP, -1) => 5,
        (WARMTH_STEP, 1) => 6,
        (TOGGLE, _) => 7,
        (TILE, 0..=3) => 8 + value as u8,
        _ => 0,
    }
}
