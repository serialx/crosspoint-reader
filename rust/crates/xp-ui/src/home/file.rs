use crate::model::{self, Effect, Input, ListState};

pub const ROOT: u8 = 4;
pub const PARENT: u8 = 5;
pub const HOME: u8 = 6;
pub const OPTIONS: u8 = 7;
pub const DIRECTORY: u8 = 8;
pub const PICK: u8 = 9;
pub const OPEN: u8 = 10;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub list: ListState,
    pub revealed: i32,
    pub picker: u8,
    pub reserved: [u8; 3],
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Facts {
    pub count: u16,
    pub root: u8,
    pub directory: u8,
    pub long_confirm: u8,
    pub long_touch: u8,
    pub touch: u8,
    pub touch_directory: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Update {
    pub effect: Effect,
    pub consumed: u8,
    pub haptic: u8,
    pub reserved: [u8; 2],
}

impl State {
    pub fn new(selected: i32, picker: bool) -> Self {
        Self {
            list: ListState::new(selected),
            revealed: -1,
            picker: u8::from(picker),
            reserved: [0; 3],
        }
    }
    pub fn activate(&mut self, f: Facts) -> Effect {
        if !(0..i32::from(f.count)).contains(&self.list.selected) {
            return Effect::default();
        }
        let effect = Effect {
            kind: if f.directory != 0 {
                DIRECTORY
            } else if self.picker != 0 {
                PICK
            } else {
                OPEN
            },
            value: self.list.selected,
            invalidate: u8::from(f.directory != 0),
            reserved: [0; 2],
        };
        if f.directory != 0 {
            self.list.selected = 0;
            self.list.top = 0;
        }
        effect
    }
    pub fn activate_bound(&self, facts: Facts) -> Effect {
        // Directory selection is published only after the facade replaces its rows.
        let mut snapshot = *self;
        let mut effect = snapshot.activate(facts);
        effect.invalidate = 0;
        effect
    }
    pub fn update(&mut self, i: Input, f: Facts) -> Update {
        let mut result = Update::default();
        if self.picker == 0 && i.released & model::BACK != 0 && i.held_ms >= 1000 && f.root == 0 {
            self.list.selected = 0;
            self.list.top = 0;
            result.effect = Effect {
                kind: ROOT,
                invalidate: 1,
                ..Effect::default()
            };
            result.consumed = 1;
            return result;
        }
        if self.picker == 0 && f.long_confirm != 0 {
            result.effect.kind = OPTIONS;
            result.effect.value = self.list.selected;
            result.consumed = 1;
            return result;
        }
        if i.released & model::CONFIRM != 0 {
            result.effect = self.activate(f);
            result.consumed = 1;
            return result;
        }
        if i.released & model::BACK != 0 {
            result.consumed = 1;
            if i.held_ms < 1000 {
                result.effect.kind = if f.root == 0 {
                    PARENT
                } else if self.picker != 0 {
                    1
                } else {
                    HOME
                };
                result.effect.invalidate = u8::from(f.root == 0);
            }
            return result;
        }
        for (mask, direction, page) in [
            (model::NAV_NEXT, 1, false),
            (model::NAV_PREVIOUS, -1, false),
            (model::NAV_NEXT, 1, true),
            (model::NAV_PREVIOUS, -1, true),
        ] {
            if !page {
                if i.pressed & mask != 0 {
                    self.step(i32::from(f.count), direction, false);
                    result.effect.invalidate += 1;
                }
            } else if (i.pressed | i.released) & mask != 0 {
                self.list.last_repeat = 0;
            } else if i.held & mask != 0
                && i.held_ms > 500
                && i.now_ms.wrapping_sub(self.list.last_repeat) > 500
            {
                self.step(i32::from(f.count), direction, true);
                self.list.last_repeat = i.now_ms;
                result.effect.invalidate += 1;
            }
        }
        result
    }
    #[inline(never)]
    pub fn update_bound(&mut self, input: Input, facts: Facts) -> Update {
        let before = self.list;
        let mut result = self.update_full(input, facts);
        if matches!(result.effect.kind, ROOT | PARENT | DIRECTORY) {
            self.list.selected = before.selected;
            self.list.top = before.top;
            result.effect.invalidate = 0;
        }
        result
    }
    pub fn loaded(&mut self, reason: u8, selected: i32, count: u16) {
        match reason {
            ROOT | DIRECTORY => {
                self.list.selected = 0;
                self.list.top = 0;
            }
            PARENT => self.parent_loaded(selected, count),
            11 => {
                self.list.selected = self.list.selected.clamp(0, (i32::from(count) - 1).max(0));
                self.parent_loaded(self.list.selected, count);
            }
            12 => {
                self.list.selected = selected;
                self.parent_loaded(selected, count);
            }
            _ => self.list.selected = selected,
        }
    }
    pub fn interest(&self, now: u32) -> model::Interest {
        let mut interest = self.list.interest(now);
        interest.held |= model::CONFIRM;
        interest.held_after_ms = 501;
        interest.deadline_enabled = 0;
        interest.touch_flags |= model::TOUCH_HELD;
        interest.swipes |= (1 << model::SWIPE_LEFT) | (1 << model::SWIPE_RIGHT);
        interest
    }
    pub fn update_full(&mut self, mut i: Input, f: Facts) -> Update {
        if self.picker == 0 && i.released & model::BACK != 0 && i.held_ms >= 1000 && f.root == 0 {
            return self.update(i, f);
        }
        let tap = i.touch_flags & model::TOUCH_RELEASED != 0 && i.touch_x >= 0 && i.touch_y >= 0;
        let closed_reveal = f.touch != 0 && self.revealed >= 0;
        if f.touch != 0 {
            if self.revealed >= 0 {
                let row = self.revealed;
                if i.released & (model::BACK | model::CONFIRM) != 0
                    || (tap && !(i.action == 2 && i.value == row))
                    || i.swipe == model::SWIPE_RIGHT
                {
                    self.revealed = -1;
                    return Update {
                        effect: Effect {
                            invalidate: 1,
                            ..Effect::default()
                        },
                        consumed: 1,
                        ..Update::default()
                    };
                }
            }
            if i.swipe == model::SWIPE_UP || i.swipe == model::SWIPE_DOWN {
                self.revealed = -1;
            }
            if self.picker == 0
                && i.swipe == model::SWIPE_LEFT
                && i.action == 1
                && (0..i32::from(f.count)).contains(&i.value)
            {
                self.revealed = i.value;
                return Update {
                    effect: Effect {
                        invalidate: 1,
                        ..Effect::default()
                    },
                    consumed: 1,
                    ..Update::default()
                };
            }
        }
        if f.long_confirm != 0 || i.released & (model::CONFIRM | model::BACK) != 0 {
            return self.update(i, f);
        }
        if (tap || f.long_touch != 0) && i.action != 0 && (0..i32::from(f.count)).contains(&i.value)
        {
            let mut result = Update {
                consumed: 1,
                haptic: 1,
                ..Update::default()
            };
            if i.action == 2 && i.value == self.revealed && self.picker == 0 {
                self.revealed = -1;
                self.list.selected = i.value;
                result.effect = Effect {
                    kind: 11,
                    value: i.value,
                    invalidate: 1,
                    ..Effect::default()
                };
                return result;
            }
            if i.action == 1 {
                if self.revealed >= 0 {
                    self.revealed = -1;
                    result.effect.invalidate = 1;
                    return result;
                }
                self.list.selected = i.value;
                result.effect = if f.long_touch != 0 && self.picker == 0 {
                    Effect {
                        kind: OPTIONS,
                        value: i.value,
                        ..Effect::default()
                    }
                } else {
                    self.activate(Facts {
                        directory: f.touch_directory,
                        ..f
                    })
                };
                return result;
            }
        }
        if i.swipe == model::SWIPE_UP || i.swipe == model::SWIPE_DOWN {
            let delta = self.list.page_rows() * if i.swipe == model::SWIPE_UP { 1 } else { -1 };
            self.list.scroll(delta);
            return Update {
                effect: Effect {
                    kind: 3,
                    value: delta,
                    invalidate: 1 + u8::from(closed_reveal),
                    ..Effect::default()
                },
                consumed: 1,
                ..Update::default()
            };
        }
        if i.pressed & (model::NAV_NEXT | model::NAV_PREVIOUS) != 0 {
            self.revealed = -1;
        }
        i.action = 0;
        self.update(i, f)
    }
    fn step(&mut self, count: i32, direction: i32, page: bool) {
        // Keep native signed remainder and page division for stale selections.
        let current = self.list.selected;
        let rows = self.list.page_rows();
        let next = if count <= 0 {
            0
        } else if page && count > rows {
            let last = (count - 1) / rows;
            let current = current / rows;
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
            (current + 1) % count
        } else {
            (current + count - 1) % count
        };
        self.list.select(next);
    }
    pub fn parent_loaded(&mut self, selected: i32, count: u16) {
        self.list.selected = selected;
        let rows = self.list.visible.max(1);
        self.list.top = (selected - rows + 1)
            .max(0)
            .min((i32::from(count) - rows).max(0));
    }
}
