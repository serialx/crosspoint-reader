use crate::model::{self, Effect, Input, ListState};
pub const HOME: u8 = 4;
pub const SEARCH: u8 = 5;
pub const OPTIONS: u8 = 6;
pub const DELETE: u8 = 7;
pub const COLLAPSE: u8 = 8;
pub const EXPAND: u8 = 9;
pub const CLEAR_QUERY: u8 = 10;
pub const SORT: u8 = 11;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct State {
    pub lists: [ListState; 3],
    pub expanded: ListState,
    pub revealed: i32,
    pub last_repeat: u32,
    pub active_tab: u8,
    pub descending_tabs: u8,
    pub sort: u8,
    pub degraded: u8,
    pub collapsed: u8,
    pub queried: u8,
    pub suppress_confirm: u8,
    pub suppress_back: u8,
    pub started_on_tabs: u8,
    pub reserved: [u8; 3],
}
impl Default for State {
    fn default() -> Self {
        Self {
            lists: [ListState::new(0); 3],
            expanded: ListState::new(0),
            revealed: -1,
            last_repeat: 0,
            active_tab: 0,
            descending_tabs: 1,
            sort: 1,
            degraded: 0,
            collapsed: 0,
            queried: 0,
            suppress_confirm: 0,
            suppress_back: 0,
            started_on_tabs: 0,
            reserved: [0; 3],
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Facts {
    pub count: u16,
    pub books: u16,
    pub long_confirm: u8,
    pub long_touch: u8,
    pub touch: u8,
    pub filter_failed: u8,
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
    pub fn nav(&self) -> &ListState {
        &self.lists[usize::from(self.active_tab.min(2))]
    }
    pub fn nav_mut(&mut self) -> &mut ListState {
        &mut self.lists[usize::from(self.active_tab.min(2))]
    }
    pub fn entry(&self) -> i32 {
        (self.nav().selected - 1).max(0)
    }
    pub fn tabs_focused(&self) -> bool {
        self.nav().selected == 0
    }
    pub fn groupable(&self, books: u16) -> bool {
        self.degraded == 0 && self.sort >= 2 && books > 0
    }
    pub fn delete_eligible(&self, books: u16) -> bool {
        self.collapsed == 0 && (self.queried != 0 || !self.groupable(books))
    }
    pub fn select_tab(&mut self, index: u8, toggle: bool) -> Effect {
        if index >= 3 {
            return Effect::default();
        }
        if toggle && index == self.active_tab {
            self.descending_tabs ^= 1 << index;
        }
        self.sort = 2 * index + u8::from(self.descending_tabs & (1 << index) != 0);
        self.collapsed = 0;
        self.active_tab = index;
        self.nav_mut().selected = 0;
        self.nav_mut().top = 0;
        Effect {
            kind: SORT,
            value: i32::from(index),
            invalidate: 1,
            reserved: [0; 2],
        }
    }
    fn step_tab(&mut self, direction: i32) -> Effect {
        self.select_tab(
            (i32::from(self.active_tab) + if direction > 0 { 1 } else { 2 }) as u8 % 3,
            false,
        )
    }
    fn move_ring(&mut self, index: i32, out: &mut Update) {
        self.nav_mut().select(index);
        out.effect.invalidate += 1;
    }
    pub fn back(&mut self) -> Effect {
        if self.queried != 0 {
            self.queried = 0;
            self.collapsed = 0;
            self.nav_mut().selected = 0;
            self.nav_mut().top = 0;
            Effect {
                kind: CLEAR_QUERY,
                invalidate: 1,
                ..Effect::default()
            }
        } else if self.collapsed != 0 {
            Effect {
                kind: EXPAND,
                ..Effect::default()
            }
        } else if !self.tabs_focused() && self.degraded == 0 {
            self.nav_mut().selected = 0;
            Effect {
                invalidate: 1,
                ..Effect::default()
            }
        } else {
            Effect {
                kind: HOME,
                ..Effect::default()
            }
        }
    }
    pub fn interest(&self, now: u32) -> model::Interest {
        let mut i = self.nav().interest(now);
        i.held |= model::CONFIRM;
        i.held_after_ms = 501;
        i.deadline_enabled = 0;
        i.touch_flags |= model::TOUCH_HELD;
        i.swipes |= (1 << model::SWIPE_LEFT) | (1 << model::SWIPE_RIGHT);
        i
    }
    pub fn filter_started(&mut self, queried: bool) {
        self.queried = u8::from(queried);
        self.collapsed = 0;
    }
    pub fn query_loaded(&mut self, matches: u16) {
        let selected = if self.queried != 0 && matches == 0 && self.degraded == 0 {
            0
        } else {
            1
        };
        self.nav_mut().selected = selected;
        self.nav_mut().top = 0;
    }
    pub fn sort_begin(&mut self, value: i32) {
        let index = (value & 255) as u8;
        if index >= 3 {
            return;
        }
        self.descending_tabs = ((value >> 8) & 7) as u8;
        self.sort = 2 * index + u8::from(self.descending_tabs & (1 << index) != 0);
        self.revealed = -1;
    }
    pub fn sort_finish(&mut self, index: u8) {
        if index >= 3 {
            return;
        }
        self.active_tab = index;
        self.nav_mut().selected = 0;
        self.nav_mut().top = 0;
    }
    pub fn rebuilt(&mut self, count: u16) {
        let n = self.nav_mut();
        n.selected = n.selected.min(i32::from(count)).max(0);
        n.follow = 1;
    }
    pub fn collapse(&mut self, group: i32) {
        self.expanded = *self.nav();
        self.collapsed = 1;
        *self.nav_mut() = ListState::new(group + 1);
    }
    pub fn expand(&mut self, book: i32) {
        if self.collapsed == 0 {
            return;
        }
        self.collapsed = 0;
        *self.nav_mut() = self.expanded;
        if book >= 0 {
            let n = self.nav_mut();
            n.selected = book + 1;
            n.top = book;
            n.follow = 1;
        }
    }
    pub fn update_full(&mut self, i: Input, f: Facts) -> Update {
        if (self.suppress_confirm != 0 && i.released & model::CONFIRM != 0)
            || (self.suppress_back != 0 && i.released & model::BACK != 0)
        {
            return self.update(i, f);
        }
        let tap = i.touch_flags & model::TOUCH_RELEASED != 0 && i.touch_x >= 0 && i.touch_y >= 0;
        let closed_reveal = f.touch != 0 && self.revealed >= 0;
        if f.touch != 0 {
            if self.revealed >= 0
                && (i.released & (model::BACK | model::CONFIRM) != 0
                    || (tap && !(i.action == 2 && i.value == self.revealed))
                    || i.swipe == model::SWIPE_RIGHT)
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
            if i.swipe == model::SWIPE_UP || i.swipe == model::SWIPE_DOWN {
                self.revealed = -1;
            }
            if i.swipe == model::SWIPE_LEFT
                && self.collapsed == 0
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
        if tap || f.long_touch != 0 {
            let mut out = Update {
                consumed: 1,
                haptic: 1,
                ..Update::default()
            };
            match i.action {
                1 if (0..i32::from(f.count)).contains(&i.value) => {
                    if self.revealed >= 0 {
                        self.revealed = -1;
                        out.effect.invalidate = 1;
                        return out;
                    }
                    self.nav_mut().selected = i.value + 1;
                    out.effect = Effect {
                        value: i.value,
                        kind: if f.long_touch != 0 {
                            if self.sort < 2 {
                                OPTIONS
                            } else if self.delete_eligible(f.books) {
                                DELETE
                            } else if self.collapsed == 0 && self.groupable(f.books) {
                                COLLAPSE
                            } else {
                                2
                            }
                        } else {
                            2
                        },
                        ..Effect::default()
                    };
                    return out;
                }
                2 if i.value == self.revealed
                    && self.collapsed == 0
                    && (0..i32::from(f.count)).contains(&i.value) =>
                {
                    self.nav_mut().selected = i.value + 1;
                    self.revealed = -1;
                    out.effect = Effect {
                        kind: DELETE,
                        value: i.value,
                        invalidate: 1,
                        ..Effect::default()
                    };
                    return out;
                }
                3 if (0..3).contains(&i.value) && f.long_touch == 0 => {
                    self.revealed = -1;
                    out.effect = self.select_tab(i.value as u8, true);
                    return out;
                }
                4 if self.degraded == 0 && f.long_touch == 0 => {
                    out.effect.kind = SEARCH;
                    return out;
                }
                5 if self.degraded == 0 && f.long_touch == 0 => {
                    out.effect.kind = 12;
                    return out;
                }
                6 if f.long_touch == 0 => {
                    out.effect = self.back();
                    return out;
                }
                _ => {}
            }
        }
        if i.swipe == model::SWIPE_UP || i.swipe == model::SWIPE_DOWN {
            let delta = self.nav().page_rows() * if i.swipe == model::SWIPE_UP { 1 } else { -1 };
            self.nav_mut().scroll(delta);
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
        self.update(i, f)
    }
    #[inline(never)]
    pub fn update_bound(&mut self, i: Input, f: Facts) -> Update {
        let before = *self;
        let mut out = self.update_full(i, f);
        if out.effect.kind == SORT {
            out.effect.value = i32::from(self.active_tab) | (i32::from(self.descending_tabs) << 8);
            let repeat = self.last_repeat;
            let started = self.started_on_tabs;
            *self = before;
            self.last_repeat = repeat;
            self.started_on_tabs = started;
            out.effect.invalidate = 0;
        } else if out.effect.kind == CLEAR_QUERY {
            *self = before;
            out.effect.invalidate = 0;
        } else if out.effect.kind == 2 && self.collapsed != 0 {
            out.effect.kind = 13;
        }
        out
    }
    pub fn update(&mut self, i: Input, f: Facts) -> Update {
        let mut out = Update::default();
        if self.suppress_confirm != 0 && i.released & model::CONFIRM != 0 {
            self.suppress_confirm = 0;
            out.consumed = 1;
            return out;
        }
        if self.suppress_back != 0 && i.released & model::BACK != 0 {
            self.suppress_back = 0;
            out.consumed = 1;
            return out;
        }
        if f.long_confirm != 0 {
            out.consumed = 1;
            if self.tabs_focused() {
                if self.degraded == 0 {
                    out.effect = self.select_tab(self.active_tab, true);
                }
            } else {
                out.effect.value = self.entry();
                out.effect.kind = if self.sort < 2 {
                    OPTIONS
                } else if self.delete_eligible(f.books) {
                    if f.count > 0 {
                        DELETE
                    } else {
                        0
                    }
                } else if self.collapsed == 0 && self.groupable(f.books) {
                    COLLAPSE
                } else {
                    2
                };
            }
            return out;
        }
        if i.released & model::BACK != 0 {
            out.effect = self.back();
            out.consumed = 1;
            return out;
        }
        if i.released & model::CONFIRM != 0 {
            out.consumed = 1;
            if self.tabs_focused() {
                out.effect = self.step_tab(1);
            } else if f.count > 0 {
                out.effect.kind = 2;
                out.effect.value = self.entry();
            }
            return out;
        }
        let count = i32::from(f.count);
        if i.pressed & (model::NAV_NEXT | model::NAV_PREVIOUS) != 0 {
            self.started_on_tabs = u8::from(self.tabs_focused());
        }
        if i.pressed & model::NAV_NEXT != 0 && count > 0 {
            self.move_ring(
                if self.nav().selected == count {
                    1
                } else {
                    self.nav().selected + 1
                },
                &mut out,
            );
        }
        if i.pressed & model::NAV_PREVIOUS != 0
            && (self.started_on_tabs == 0 || self.degraded != 0)
            && count > 0
        {
            self.move_ring(
                if self.nav().selected <= 1 {
                    count
                } else {
                    self.nav().selected - 1
                },
                &mut out,
            );
        }
        if i.released & model::NAV_PREVIOUS != 0 {
            if self.last_repeat == 0
                && self.started_on_tabs != 0
                && self.tabs_focused()
                && self.degraded == 0
            {
                out.effect.kind = SEARCH;
            }
            self.last_repeat = 0;
        }
        for (mask, direction) in [(model::NAV_NEXT, 1), (model::NAV_PREVIOUS, -1)] {
            if (i.pressed | i.released) & mask != 0 {
                self.last_repeat = 0;
            } else if i.held & mask != 0
                && i.held_ms > 500
                && i.now_ms.wrapping_sub(self.last_repeat) > 500
            {
                if self.started_on_tabs != 0 {
                    self.nav_mut().selected = 0;
                    let effect = self.step_tab(direction);
                    out.effect.invalidate += effect.invalidate;
                    out.effect.kind = effect.kind;
                    out.effect.value = effect.value;
                } else if count > 0 {
                    let rows = self.nav().drawn.max(self.nav().visible).max(1);
                    let rows = if self.nav().drawn > 0 {
                        self.nav().drawn
                    } else {
                        rows
                    };
                    let current = self.entry();
                    let next = if count <= rows {
                        if direction > 0 {
                            (current + 1) % count
                        } else {
                            (current + count - 1) % count
                        }
                    } else if direction > 0 {
                        if current / rows < (count - 1) / rows {
                            (current / rows + 1) * rows
                        } else {
                            0
                        }
                    } else if current / rows > 0 {
                        (current / rows - 1) * rows
                    } else {
                        (count - 1) / rows * rows
                    };
                    self.move_ring(next + 1, &mut out);
                }
                self.last_repeat = i.now_ms;
            }
        }
        out
    }
}

impl State {
    pub fn event(&mut self, event: u8, value: i32, count: u16) {
        match event {
            1 => self.degraded = u8::from(value != 0),
            2 => {
                self.suppress_confirm = u8::from(value & 1 != 0);
                self.suppress_back = u8::from(value & 2 != 0);
            }
            3 => self.filter_started(value != 0),
            4 => self.query_loaded(count),
            5 => self.sort_begin(value),
            6 => self.sort_finish(value as u8),
            7 => self.rebuilt(count),
            8 => self.collapse(value),
            9 => self.expand(value),
            10 => {
                self.queried = 0;
                self.collapsed = 0;
                self.nav_mut().selected = 0;
                self.nav_mut().top = 0;
            }
            11 => self.nav_mut().selected = value,
            12 => self.revealed = -1,
            _ => {}
        }
    }
    pub fn flags(&self, books: u16) -> u8 {
        u8::from(self.tabs_focused())
            | (u8::from(self.groupable(books)) << 1)
            | (u8::from(self.delete_eligible(books)) << 2)
            | (u8::from(self.sort < 2) << 3)
            | (u8::from(self.sort >= 4) << 4)
            | (u8::from(self.sort & 1 != 0) << 5)
    }
    pub fn pinned(&self, total: u8) -> u8 {
        if self.active_tab == 0 && self.queried == 0 && self.sort & 1 != 0 {
            total
        } else {
            0
        }
    }
    pub fn books(&self, indexed: u16, filtered: u16, pins: u8, overlap: u8) -> u16 {
        if self.queried != 0 {
            filtered
        } else {
            let p = self.pinned(pins);
            indexed
                .saturating_add(u16::from(p))
                .saturating_sub(if p != 0 { u16::from(overlap) } else { 0 })
        }
    }
    pub fn row_for(&self, entry: i32, pins: u8, filtered: &[u16], overlap: &[u16]) -> i32 {
        if self.queried != 0 {
            return usize::try_from(entry)
                .ok()
                .and_then(|i| filtered.get(i))
                .map_or(0, |v| i32::from(*v));
        }
        let p = self.pinned(pins);
        if p == 0 {
            return entry;
        }
        let mut row = entry - i32::from(p);
        for &skip in overlap {
            if i32::from(skip) <= row {
                row += 1;
            }
        }
        row
    }
}
pub fn group_for(book: i32, starts: &[u16]) -> i32 {
    let mut group = 0;
    while group + 1 < starts.len() && i32::from(starts[group + 1]) <= book {
        group += 1;
    }
    group as i32
}
pub fn overlap(ascending: &[u16], total: u16, descending: bool, out: &mut [u16]) -> usize {
    let mut count = 0;
    for &row in ascending {
        if row == u16::MAX || row >= total {
            continue;
        }
        let Some(slot) = out.get_mut(count) else {
            break;
        };
        *slot = if descending { total - 1 - row } else { row };
        count += 1;
    }
    // At most ten pins: insertion sort avoids linking a general-purpose sorter.
    for i in 1..count {
        let value = out[i];
        let mut j = i;
        while j > 0 && out[j - 1] > value {
            out[j] = out[j - 1];
            j -= 1;
        }
        out[j] = value;
    }
    count
}
