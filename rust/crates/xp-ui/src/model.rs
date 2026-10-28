//! Input is a snapshot, never a second hardware poll. Effects are applied by
//! the existing activity owner after releasing its render lock.
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
    pub fn update(&mut self, input: Input, count: i32, has_book: bool) -> Effect {
        if input.released & 1 != 0 {
            return Effect {
                kind: EffectKind::Cancel as u8,
                ..Effect::default()
            };
        }
        if !has_book {
            return Effect::default();
        }
        if input.released & 2 != 0 {
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
        if input.touch_flags & 1 != 0 {
            self.active = if input.action == 1 { input.value } else { -1 };
        }
        if input.touch_flags & 2 != 0 {
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
        if input.swipe != 0 {
            let delta = if input.swipe > 0 {
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
        for (mask, direction) in [(4, 1), (8, -1)] {
            if input.pressed & mask != 0 {
                effect = self.step(count, direction, false);
            }
        }
        for (mask, direction) in [(4, 1), (8, -1)] {
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
