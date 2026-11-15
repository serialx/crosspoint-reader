//! Text settings policy and placement over the shared reader text/page engines.
mod abi;
pub mod ffi;
mod labels;
pub mod preview;
#[cfg(test)]
mod tests;
pub mod view;
use crate::model::{self, Effect, EffectKind, Input, Interest, ListState};

pub const FAMILY: u8 = 4;
pub const SIZE: u8 = 5;
pub const SAVE: u8 = 6;
pub const OPTIONS: u8 = 7;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    Line,
    Word,
    Character,
    ParagraphGap,
    Indent,
    Alignment,
    Margin,
    Focus,
    Hyphenation,
    Embedded,
    Antialiasing,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Values {
    pub fields: [u8; 11],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Edit {
    pub value: i32,
    pub kind: u8,
    pub field: u8,
    pub count: u8,
    pub selected: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub tabs: [ListState; 4],
    pub last_repeat: u32,
    pub font_count: u16,
    pub size_count: u16,
    pub family: i32,
    pub size: i32,
    pub tab: u8,
    pub started_on_tabs: u8,
    pub language: u8,
    pub reserved: u8,
}
impl State {
    pub fn valid(&self) -> bool {
        self.tab < 4
            && self.font_count > 0
            && self.size_count > 0
            && self.tabs.iter().all(|n| (0..=65536).contains(&n.selected))
    }
    pub fn new(tab: u8, family: i32, size: i32, fonts: u16, sizes: u16, language: u8) -> Self {
        let mut s = Self {
            tabs: [ListState::new(1); 4],
            font_count: fonts,
            size_count: sizes,
            family,
            size,
            tab: tab.min(3),
            language,
            ..Self::default()
        };
        s.tabs[0].selected = family + 1;
        s.tabs[1].selected = size + 1;
        s.tabs[usize::from(s.tab)].selected = 0;
        s
    }
    pub fn count(&self) -> i32 {
        match self.tab {
            0 => i32::from(self.font_count),
            1 => i32::from(self.size_count),
            2 => 7,
            3 => 4,
            _ => 0,
        }
    }
    pub fn interest(&self, now: u32) -> Interest {
        let mut nav = self.tabs[usize::from(self.tab.min(3))];
        nav.last_repeat = self.last_repeat;
        nav.interest(now)
    }
    fn switch(&mut self, direction: i32) {
        let focused = self.tabs[usize::from(self.tab)].selected == 0;
        self.tab = (i32::from(self.tab) + direction + 4).rem_euclid(4) as u8;
        let n = &mut self.tabs[usize::from(self.tab)];
        if focused {
            n.selected = 0;
        }
        n.follow = 1;
        n.navigation_revision = n.navigation_revision.wrapping_add(1);
    }
    pub fn update(&mut self, input: Input) -> Effect {
        if !self.valid() {
            return Effect::default();
        }
        if input.released & model::BACK != 0 {
            return Effect {
                kind: EffectKind::Cancel as u8,
                ..Effect::default()
            };
        }
        if input.released & model::CONFIRM != 0 {
            let ring = self.tabs[usize::from(self.tab)].selected;
            if ring == 0 {
                self.switch(1);
                return Effect {
                    invalidate: 1,
                    ..Effect::default()
                };
            }
            return Effect {
                kind: EffectKind::Activate as u8,
                value: ring - 1,
                ..Effect::default()
            };
        }
        if input.touch_flags & model::TOUCH_RELEASED != 0 {
            if input.action == 2 && (0..4).contains(&input.value) {
                if self.tab == input.value as u8 {
                    return Effect {
                        invalidate: 1,
                        ..Effect::default()
                    };
                }
                self.tab = input.value as u8;
                self.tabs[usize::from(self.tab)].select(0);
                return Effect {
                    invalidate: 1,
                    ..Effect::default()
                };
            }
            if input.action == 1 && (0..self.count()).contains(&input.value) {
                self.tabs[usize::from(self.tab)].selected = input.value + 1;
                return Effect {
                    kind: EffectKind::Activate as u8,
                    value: input.value,
                    invalidate: 1,
                    ..Effect::default()
                };
            }
        }
        if matches!(input.swipe, model::SWIPE_UP | model::SWIPE_DOWN) {
            let n = &mut self.tabs[usize::from(self.tab)];
            let delta = if input.swipe == model::SWIPE_UP {
                n.page_rows()
            } else {
                -n.page_rows()
            };
            n.scroll(delta);
            return Effect {
                kind: EffectKind::Scroll as u8,
                value: delta,
                invalidate: 1,
                ..Effect::default()
            };
        }
        let mut effect = Effect::default();
        if input.pressed & (model::NAV_NEXT | model::NAV_PREVIOUS) != 0 {
            self.started_on_tabs = u8::from(self.tabs[usize::from(self.tab)].selected == 0);
        }
        let count = self.count() + 1;
        for (mask, direction) in [(model::NAV_NEXT, 1), (model::NAV_PREVIOUS, -1)] {
            if input.pressed & mask != 0 {
                let n = &mut self.tabs[usize::from(self.tab)];
                n.select((n.selected + count + direction).rem_euclid(count));
                effect.invalidate = 1;
            }
        }
        for (mask, direction) in [(model::NAV_NEXT, 1), (model::NAV_PREVIOUS, -1)] {
            if (input.pressed | input.released) & mask != 0 {
                self.last_repeat = 0;
            } else if input.held & mask != 0
                && input.held_ms > 500
                && input.now_ms.wrapping_sub(self.last_repeat) > 500
            {
                let count = self.count();
                let n = &mut self.tabs[usize::from(self.tab)];
                if self.started_on_tabs != 0 {
                    n.selected = 0;
                } else if n.selected == 0 && count > 0 {
                    n.selected = 1;
                }
                self.switch(direction);
                self.last_repeat = input.now_ms;
                effect.invalidate = 1;
            }
        }
        effect
    }
    pub fn publish(&mut self, next: &Self, scroll: i32) {
        if next.tab < 4 {
            let i = usize::from(next.tab);
            self.tabs[i].publish(&next.tabs[i], scroll);
        }
    }
    pub fn edit(&self, row: i32, values: Values) -> Edit {
        if !self.valid() || !(0..self.count()).contains(&row) {
            return Edit::default();
        }
        if self.tab < 2 {
            return if row
                == if self.tab == 0 {
                    self.family
                } else {
                    self.size
                } {
                Edit::default()
            } else {
                Edit {
                    kind: if self.tab == 0 { FAMILY } else { SIZE },
                    value: row,
                    ..Edit::default()
                }
            };
        }
        let field = if self.tab == 2 {
            row as u8
        } else {
            row as u8 + Field::Focus as u8
        };
        let value = values.fields[usize::from(field)];
        if field == Field::ParagraphGap as u8 || field >= Field::Focus as u8 {
            return Edit {
                kind: SAVE,
                field,
                value: i32::from(value == 0),
                ..Edit::default()
            };
        }
        let (count, selected) = match field {
            0 => (4, value),
            1 => (7, (value.clamp(50, 200) - 50) / 25),
            2 => (5, value),
            4 => (6, value),
            5 => (5, value),
            6 => (8, (value.clamp(5, 40) - 5) / 5),
            _ => return Edit::default(),
        };
        Edit {
            kind: OPTIONS,
            field,
            count,
            selected: if selected < count { selected } else { 0 },
            ..Edit::default()
        }
    }
}
pub fn choice(field: u8, index: u8) -> u8 {
    match field {
        1 => 50 + index.min(6) * 25,
        6 => 5 + index.min(7) * 5,
        _ => index,
    }
}
