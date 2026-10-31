//! Reader screen policy and placement. Resource callbacks borrow existing book
//! windows and font services; no screen data is allocated by this module.
use crate::{
    model::{self, Effect, EffectKind, Input, Interest},
    scene::{Arena, Command, Error, Kind, Rect},
};

pub mod hits;
pub mod list;
pub mod menu;
#[cfg(test)]
mod tests;
pub mod toolbar;

pub const ACTIONS: u8 = 4;
pub const DELETE: u8 = 5;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ListModel {
    pub list: model::ListState,
    pub revealed: i32,
    pub long_press_consumed: u8,
    pub reserved: [u8; 3],
}

impl ListModel {
    pub fn new(selected: i32) -> Self {
        Self {
            list: model::ListState::new(selected),
            revealed: -1,
            long_press_consumed: 0,
            reserved: [0; 3],
        }
    }
    pub fn interest(&self, kind: list::Screen, now: u32) -> Interest {
        let mut interest = self.list.interest(now);
        if matches!(kind, list::Screen::Bookmarks | list::Screen::Clippings) {
            interest.pressed |= model::CONFIRM;
            interest.held |= model::CONFIRM;
            interest.held_after_ms = 500;
            interest.deadline_enabled = 0;
            interest.touch_flags |= model::TOUCH_HELD;
        }
        if kind == list::Screen::Bookmarks {
            interest.swipes |= (1 << model::SWIPE_LEFT) | (1 << model::SWIPE_RIGHT);
        }
        interest
    }
    pub fn update(
        &mut self,
        input: Input,
        count: u16,
        present: bool,
        kind: list::Screen,
        long_press: bool,
        swipe_row: i32,
    ) -> Effect {
        let actions = matches!(kind, list::Screen::Bookmarks | list::Screen::Clippings);
        if actions && long_press {
            if self.revealed >= 0 && input.action == 1 {
                self.revealed = -1;
                return Effect {
                    invalidate: 1,
                    ..Effect::default()
                };
            }
            if input.action == 1 && input.value >= 0 && input.value < i32::from(count) {
                self.list.selected = input.value;
            }
            self.long_press_consumed = 1;
            if self.list.selected >= 0 && self.list.selected < i32::from(count) {
                return Effect {
                    kind: ACTIONS,
                    value: self.list.selected,
                    ..Effect::default()
                };
            }
            return Effect::default();
        }
        if self.revealed >= 0 {
            let button_close = input.released & (model::BACK | model::CONFIRM) != 0;
            // A swipe also releases the contact, but has no tap coordinates.
            let tap = input.touch_flags & model::TOUCH_RELEASED != 0
                && input.touch_x >= 0
                && input.touch_y >= 0;
            let close = button_close || tap || input.swipe == model::SWIPE_RIGHT;
            if close {
                let row = self.revealed;
                self.revealed = -1;
                return Effect {
                    kind: if !button_close && tap && input.action == 2 && input.value == row {
                        DELETE
                    } else {
                        0
                    },
                    value: row,
                    invalidate: 1,
                    ..Effect::default()
                };
            }
        }
        if kind == list::Screen::Bookmarks
            && input.swipe == model::SWIPE_LEFT
            && swipe_row >= 0
            && swipe_row < i32::from(count)
        {
            self.revealed = swipe_row;
            return Effect {
                invalidate: 1,
                ..Effect::default()
            };
        }
        if input.swipe == model::SWIPE_UP
            || input.swipe == model::SWIPE_DOWN
            || input.pressed & (model::NAV_NEXT | model::NAV_PREVIOUS) != 0
        {
            self.revealed = -1;
        }
        if input.pressed & model::CONFIRM != 0 {
            self.long_press_consumed = 0;
        }
        if input.released & model::CONFIRM != 0 && self.long_press_consumed != 0 {
            self.long_press_consumed = 0;
            return Effect::default();
        }
        self.list.update(input, i32::from(count), present)
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct FootnoteState {
    pub selected: i32,
    pub last_move_ms: u32,
}

impl FootnoteState {
    pub fn interest(&self, now: u32) -> Interest {
        Interest {
            pressed: model::SCREEN_LEFT
                | model::SCREEN_RIGHT
                | model::SCREEN_UP
                | model::SCREEN_DOWN,
            released: model::BACK | model::CONFIRM | model::POWER,
            held: model::SCREEN_LEFT | model::SCREEN_RIGHT,
            held_after_ms: 500,
            deadline_ms: self.last_move_ms.wrapping_add(500),
            deadline_enabled: u8::from(now.wrapping_sub(self.last_move_ms) < 500),
            ..Interest::default()
        }
    }

    pub fn update(&mut self, input: Input, count: u16) -> Effect {
        if input.released & model::BACK != 0 {
            return Effect {
                kind: EffectKind::Cancel as u8,
                ..Effect::default()
            };
        }
        if count == 0 {
            return Effect::default();
        }
        if input.released & (model::CONFIRM | model::POWER) != 0 {
            return Effect {
                kind: EffectKind::Activate as u8,
                value: self.selected,
                ..Effect::default()
            };
        }
        let repeat = input.held_ms >= 500 && input.now_ms.wrapping_sub(self.last_move_ms) >= 500;
        let left = input.pressed & model::SCREEN_LEFT != 0
            || repeat && input.held & model::SCREEN_LEFT != 0;
        let right = input.pressed & model::SCREEN_RIGHT != 0
            || repeat && input.held & model::SCREEN_RIGHT != 0;
        let moved = if (left || input.pressed & model::SCREEN_UP != 0) && self.selected > 0 {
            self.selected -= 1;
            if left {
                self.last_move_ms = input.now_ms;
            }
            true
        } else if (right || input.pressed & model::SCREEN_DOWN != 0)
            && self.selected < i32::from(count) - 1
        {
            self.selected += 1;
            if right {
                self.last_move_ms = input.now_ms;
            }
            true
        } else {
            false
        };
        Effect {
            invalidate: u8::from(moved),
            ..Effect::default()
        }
    }
}

pub fn footnote_view(
    arena: &mut Arena<'_>,
    screen: Rect,
    link: Rect,
    margin_left: i16,
    margin_top: i16,
    font: i32,
    number_handle: u32,
) -> Result<Rect, Error> {
    if screen.x != 0
        || screen.y != 0
        || !(1..=8192).contains(&screen.w)
        || !(1..=8192).contains(&screen.h)
    {
        return Err(Error::Invalid);
    }
    let x = i32::from(link.x) + i32::from(margin_left);
    let y = i32::from(link.y) + i32::from(margin_top);
    let hx = (x - 2).max(0);
    let hy = (y - 2).max(0);
    let rect = Rect {
        x: i16::try_from(hx).map_err(|_| Error::Invalid)?,
        y: i16::try_from(hy).map_err(|_| Error::Invalid)?,
        w: i16::try_from(i32::from(link.w) + 4 + (x - 2).min(0)).map_err(|_| Error::Invalid)?,
        h: i16::try_from(i32::from(link.h) + 4 + (y - 2).min(0)).map_err(|_| Error::Invalid)?,
    };
    if rect.w > 0 && rect.h > 0 {
        arena.push(Command {
            rect,
            ..Command::default()
        })?;
    }
    if number_handle != 0 {
        arena.push(Command {
            kind: Kind::TextHandle as u8,
            resource: number_handle,
            generation: 1,
            rect: Rect {
                x: i16::try_from(x).map_err(|_| Error::Invalid)?,
                y: i16::try_from(y - 1).map_err(|_| Error::Invalid)?,
                w: 0,
                h: 0,
            },
            paint: 1,
            font,
            ..Command::default()
        })?;
    }
    Ok(rect)
}

/// Existing EndOfBookOptions Action values stay local to this facade.
pub fn end_update(
    state: &mut model::ListState,
    input: Input,
    count: u16,
    use_press: bool,
) -> Effect {
    if count == 0 {
        return Effect::default();
    }
    state.selected = state.selected.clamp(0, i32::from(count) - 1);
    let tap = input.touch_flags & model::TOUCH_RELEASED != 0
        && input.action == 1
        && input.value >= 0
        && input.value < i32::from(count);
    if tap {
        state.selected = input.value;
    }
    if tap || input.released & model::CONFIRM != 0 {
        return Effect {
            value: state.selected,
            kind: if state.selected < i32::from(count) - 1 {
                2
            } else {
                3
            },
            ..Effect::default()
        };
    }
    if input.released & model::BACK != 0 && input.held_ms < 1000 {
        return Effect {
            kind: 4,
            ..Effect::default()
        };
    }
    let edges = if use_press {
        input.pressed
    } else {
        input.released
    };
    let step = if edges & model::NAV_PREVIOUS != 0 {
        -1
    } else if edges & model::NAV_NEXT != 0 {
        1
    } else {
        0
    };
    if step != 0 {
        state.select((state.selected + step).rem_euclid(i32::from(count)));
        return Effect {
            kind: 1,
            invalidate: 1,
            ..Effect::default()
        };
    }
    Effect::default()
}
