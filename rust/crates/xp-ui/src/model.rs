//! Input is a snapshot, never a second hardware poll. Effects are applied by
//! the existing activity owner after releasing its model lock.
use crate::scene::{Arena, Command, Error, Kind, Rect};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Input {
    pub pressed: u32,
    pub released: u32,
    pub held: u32,
    pub now_ms: u32,
    pub held_ms: u32,
    pub touch_x: i16,
    pub touch_y: i16,
    pub touch_flags: u8,
    pub swipe: i8,
    pub reserved: u16,
    pub action: u32,
    pub value: i32,
}
// Wire bits deliberately keep the original list buttons in their first slots.
pub const BACK: u32 = 1;
pub const CONFIRM: u32 = 2;
pub const NAV_NEXT: u32 = 4;
pub const NAV_PREVIOUS: u32 = 8;
pub const LEFT: u32 = 1 << 4;
pub const RIGHT: u32 = 1 << 5;
pub const UP: u32 = 1 << 6;
pub const DOWN: u32 = 1 << 7;
pub const POWER: u32 = 1 << 8;
pub const PAGE_BACK: u32 = 1 << 9;
pub const PAGE_FORWARD: u32 = 1 << 10;
pub const SCREEN_LEFT: u32 = 1 << 11;
pub const SCREEN_RIGHT: u32 = 1 << 12;
pub const SCREEN_UP: u32 = 1 << 13;
pub const SCREEN_DOWN: u32 = 1 << 14;
pub const TOUCH_PRESSED: u8 = 1;
pub const TOUCH_RELEASED: u8 = 2;
pub const TOUCH_HELD: u8 = 4;
pub const SWIPE_UP: i8 = 1;
pub const SWIPE_DOWN: i8 = 2;
pub const SWIPE_LEFT: i8 = 3;
pub const SWIPE_RIGHT: i8 = 4;

/// Screen-owned interest. Deadlines use wrapping milliseconds, within half a cycle.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Interest {
    pub pressed: u32,
    pub released: u32,
    pub held: u32,
    pub held_after_ms: u32,
    pub deadline_ms: u32,
    pub touch_flags: u8,
    pub swipes: u8,
    pub deadline_enabled: u8,
    pub reserved: u8,
}
impl Interest {
    pub fn due(&self, input: &Input) -> bool {
        input.pressed & self.pressed != 0
            || input.released & self.released != 0
            || input.touch_flags & self.touch_flags != 0
            || (1..=4).contains(&input.swipe) && self.swipes & (1 << input.swipe) != 0
            || input.held & self.held != 0
                && input.held_ms >= self.held_after_ms
                && (self.deadline_enabled == 0
                    || input.now_ms.wrapping_sub(self.deadline_ms) < 0x8000_0000)
            || self.held == 0
                && self.deadline_enabled != 0
                && input.now_ms.wrapping_sub(self.deadline_ms) < 0x8000_0000
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub enum EffectKind {
    #[default]
    None,
    Cancel,
    Activate,
    Scroll,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Effect {
    pub value: i32,
    pub kind: u8,
    pub invalidate: u8,
    pub reserved: [u8; 2],
}
#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct ListState {
    pub selected: i32,
    pub top: i32,
    pub visible: i32,
    pub drawn: i32,
    pub drawn_count: i32,
    pub pending_scroll: i32,
    pub active: i32,
    pub last_repeat: u32,
    pub follow: u8,
    pub reserved: [u8; 3],
    pub navigation_revision: u32,
}
impl ListState {
    pub fn new(selected: i32) -> Self {
        Self {
            selected,
            visible: 1,
            follow: 1,
            active: -1,
            ..Self::default()
        }
    }
    /// Commit viewport feedback without overwriting input received during layout.
    pub fn publish(&mut self, next: &Self, consumed_scroll: i32) {
        self.top = next.top;
        self.visible = next.visible;
        self.drawn = next.drawn;
        self.drawn_count = next.drawn_count;
        if self.navigation_revision == next.navigation_revision {
            self.pending_scroll = self.pending_scroll.saturating_sub(consumed_scroll);
            self.follow = 0;
        }
    }
    pub fn interest(&self, now_ms: u32) -> Interest {
        Interest {
            pressed: NAV_NEXT | NAV_PREVIOUS,
            released: BACK | CONFIRM | NAV_NEXT | NAV_PREVIOUS,
            held: NAV_NEXT | NAV_PREVIOUS,
            held_after_ms: 501,
            deadline_ms: self.last_repeat.wrapping_add(501),
            deadline_enabled: u8::from(now_ms.wrapping_sub(self.last_repeat) <= 500),
            touch_flags: TOUCH_PRESSED | TOUCH_RELEASED,
            swipes: (1 << SWIPE_UP) | (1 << SWIPE_DOWN),
            ..Interest::default()
        }
    }
    pub fn update(&mut self, input: Input, count: i32, has_book: bool) -> Effect {
        if input.released & BACK != 0 {
            return Effect {
                kind: EffectKind::Cancel as u8,
                ..Effect::default()
            };
        }
        if !has_book {
            return Effect::default();
        }
        if input.released & CONFIRM != 0 {
            return if self.selected >= 0 && self.selected < count {
                Effect {
                    kind: EffectKind::Activate as u8,
                    value: self.selected,
                    ..Effect::default()
                }
            } else {
                Effect::default()
            };
        }
        if input.touch_flags & TOUCH_PRESSED != 0 {
            self.active = if input.action == 1 { input.value } else { -1 };
        }
        if input.touch_flags & TOUCH_RELEASED != 0 {
            self.active = -1;
            if input.action == 1 && input.value >= 0 && input.value < count {
                self.selected = input.value;
                return Effect {
                    kind: EffectKind::Activate as u8,
                    value: input.value,
                    invalidate: 1,
                    ..Effect::default()
                };
            }
        }
        if matches!(input.swipe, SWIPE_UP | SWIPE_DOWN) {
            let delta = if input.swipe == SWIPE_UP {
                self.page_rows()
            } else {
                -self.page_rows()
            };
            self.scroll(delta);
            return Effect {
                kind: EffectKind::Scroll as u8,
                value: delta,
                invalidate: 1,
                ..Effect::default()
            };
        }
        let mut effect = Effect::default();
        for (mask, direction) in [(NAV_NEXT, 1), (NAV_PREVIOUS, -1)] {
            if input.pressed & mask != 0 {
                effect = self.step(count, direction, false);
            }
        }
        for (mask, direction) in [(NAV_NEXT, 1), (NAV_PREVIOUS, -1)] {
            if (input.pressed | input.released) & mask != 0 {
                self.last_repeat = 0;
            } else if input.held & mask != 0
                && input.held_ms > 500
                && input.now_ms.wrapping_sub(self.last_repeat) > 500
            {
                effect = self.step(count, direction, true);
                self.last_repeat = input.now_ms;
            }
        }
        effect
    }
    pub fn page_rows(&self) -> i32 {
        self.drawn.max(1)
    }
    pub fn select(&mut self, selected: i32) -> Effect {
        self.navigation_revision = self.navigation_revision.wrapping_add(1);
        self.selected = selected;
        self.follow = 1;
        self.pending_scroll = 0;
        Effect {
            invalidate: 1,
            ..Effect::default()
        }
    }
    pub fn scroll(&mut self, delta: i32) -> Effect {
        self.follow = 0;
        self.pending_scroll = self
            .pending_scroll
            .saturating_add(delta)
            .clamp(-65535, 65535);
        Effect {
            invalidate: 1,
            ..Effect::default()
        }
    }
    pub fn step(&mut self, count: i32, direction: i32, page: bool) -> Effect {
        if count <= 0 {
            return self.select(0);
        }
        let rows = self.page_rows();
        let next = if page && count > rows {
            let last = (count - 1) / rows;
            let current = self.selected.max(0) / rows;
            if direction > 0 {
                if current < last {
                    (current + 1) * rows
                } else {
                    0
                }
            } else if current > 0 {
                (current - 1) * rows
            } else {
                last * rows
            }
        } else if direction > 0 {
            (self.selected.saturating_add(1)).rem_euclid(count)
        } else {
            (self.selected.saturating_sub(1)).rem_euclid(count)
        };
        self.select(next)
    }
}

pub trait Metrics {
    fn width(&mut self, font: i32, text: &[u8], style: u8) -> i32;
    fn line_height(&mut self, font: i32) -> i32;
}

/// The synchronous full-screen message uses the fixed UI font, as before.
pub fn message_view(
    arena: &mut Arena<'_>,
    metrics: &mut dyn Metrics,
    screen: Rect,
    font: i32,
    style: u8,
    text: &[u8],
) -> Result<(), Error> {
    let height = metrics.line_height(font);
    let width = metrics.width(font, text, style);
    let x =
        i16::try_from((i64::from(screen.w) - i64::from(width)) / 2).map_err(|_| Error::Invalid)?;
    let y =
        i16::try_from((i64::from(screen.h) - i64::from(height)) / 2).map_err(|_| Error::Invalid)?;
    arena.push(Command {
        kind: Kind::Clear as u8,
        rect: screen,
        paint: 1,
        ..Command::default()
    })?;
    arena.text(
        Command {
            rect: Rect { x, y, w: 0, h: 0 },
            font,
            style,
            ..Command::default()
        },
        text,
    )
}
