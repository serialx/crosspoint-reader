//! Home navigation uses the same mapped snapshot as the Activity owner.
use crate::{
    model::{self, Effect, EffectKind, Input, Interest},
    scene::Rect,
};

pub const FILES: i32 = 100;
pub const LIBRARY: i32 = 101;
pub const OPDS: i32 = 102;
pub const PLUGINS: i32 = 103;
pub const TRANSFER: i32 = 104;
pub const SETTINGS: i32 = 105;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HomeState {
    pub selected: i32,
    pub last_repeat: u32,
    pub books: u8,
    pub grid: u8,
    pub opds: u8,
    pub plugins: u8,
    pub continuing: u8,
    pub first_render: u8,
    pub covers_loaded: u8,
    pub loading: u8,
}

/// Geometry from the completed paint, with uniform rows/columns routed arithmetically.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HomeTouch {
    pub cover: Rect,
    pub menu: Rect,
    pub menu_step: i16,
    pub columns: u8,
    pub continue_in_menu: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HomeUpdate {
    pub effect: Effect,
    pub haptic: u8,
    pub reserved: [u8; 3],
}

impl HomeState {
    pub fn menu_keys(&self, continuing_in_menu: bool) -> ([u8; 6], usize) {
        let mut keys = [0; 6];
        let mut count = 0;
        if continuing_in_menu && self.books != 0 {
            keys[count] = 0;
            count += 1;
        }
        for key in [1, 2] {
            keys[count] = key;
            count += 1;
        }
        if self.plugins != 0 || self.opds != 0 {
            keys[count] = if self.plugins != 0 { 3 } else { 4 };
            count += 1;
        }
        for key in [5, 6] {
            keys[count] = key;
            count += 1;
        }
        (keys, count)
    }
    pub fn painted(&mut self, thumb_changed: bool) -> Effect {
        if thumb_changed {
            self.covers_loaded = 0;
        }
        if self.first_render == 0 {
            self.first_render = 1;
            return Effect {
                invalidate: 1,
                ..Effect::default()
            };
        }
        if self.covers_loaded == 0 && self.loading == 0 {
            self.loading = 1;
            return Effect {
                kind: 4,
                ..Effect::default()
            };
        }
        Effect::default()
    }
    pub fn loaded(&mut self) {
        self.covers_loaded = 1;
        self.loading = 0;
    }
    pub fn new(books: u8, grid: bool, opds: bool, plugins: bool, continuing: bool) -> Self {
        Self {
            books,
            grid: u8::from(grid),
            opds: u8::from(opds),
            plugins: u8::from(plugins),
            continuing: u8::from(continuing),
            ..Self::default()
        }
    }
    pub fn count(&self) -> i32 {
        i32::from(self.books) + 4 + i32::from(self.opds != 0 || self.plugins != 0)
    }
    pub fn initial_selection(&mut self, menu: i32) {
        if menu == 0 {
            self.selected = 0;
            return;
        }
        let slot = self.opds != 0 || self.plugins != 0;
        let index = match menu {
            1 => 0,
            2 => 1,
            3 => {
                if slot {
                    2
                } else {
                    0
                }
            }
            4 => 2 + i32::from(slot),
            5 => 3 + i32::from(slot),
            _ => 0,
        };
        self.selected = i32::from(self.books) + index;
    }
    pub fn interest(&self, now: u32) -> Interest {
        let nav = if self.grid != 0 {
            model::UP | model::DOWN | model::LEFT | model::RIGHT
        } else {
            model::NAV_NEXT | model::NAV_PREVIOUS
        };
        Interest {
            pressed: nav,
            released: nav | model::BACK | model::CONFIRM,
            held: nav,
            held_after_ms: 501,
            deadline_ms: self.last_repeat.wrapping_add(501),
            deadline_enabled: u8::from(now.wrapping_sub(self.last_repeat) <= 500),
            touch_flags: model::TOUCH_PRESSED | model::TOUCH_RELEASED,
            swipes: (1 << model::SWIPE_UP) | (1 << model::SWIPE_DOWN),
            ..Interest::default()
        }
    }
    fn activate(&self) -> i32 {
        if self.selected < i32::from(self.books) {
            return self.selected;
        }
        let index = self.selected - i32::from(self.books);
        let slot = self.opds != 0 || self.plugins != 0;
        match index {
            0 => FILES,
            1 => LIBRARY,
            2 if slot => {
                if self.plugins != 0 {
                    PLUGINS
                } else {
                    OPDS
                }
            }
            i if i == 2 + i32::from(slot) => TRANSFER,
            i if i == 3 + i32::from(slot) => SETTINGS,
            _ => -1,
        }
    }
    fn band(&mut self, base: i32, count: i32, direction: i32, out: &mut HomeUpdate) {
        if count <= 0 {
            return;
        }
        let index = self.selected - base;
        self.selected = base
            + if !(0..count).contains(&index) {
                if direction > 0 {
                    0
                } else {
                    count - 1
                }
            } else {
                (index + count + direction) % count
            };
        out.effect.invalidate += 1;
    }
    fn navigate(
        &mut self,
        input: Input,
        mask: u32,
        base: i32,
        count: i32,
        dir: i32,
        out: &mut HomeUpdate,
    ) {
        if input.pressed & mask != 0 {
            self.band(base, count, dir, out);
        }
        if (input.pressed | input.released) & mask != 0 {
            self.last_repeat = 0;
        } else if input.held & mask != 0
            && input.held_ms > 500
            && input.now_ms.wrapping_sub(self.last_repeat) > 500
        {
            self.band(base, count, dir, out);
            self.last_repeat = input.now_ms;
        }
    }
    pub fn update_grid(
        &mut self,
        mut input: Input,
        geometry: Option<&grid::Geometry>,
    ) -> HomeUpdate {
        input.action = 0;
        input.value = geometry.map_or(-1, |g| g.hit(input.touch_x, input.touch_y));
        if input.value >= 0 {
            input.action = 1;
        }
        let touch = input.action != 0 && input.touch_flags & model::TOUCH_RELEASED != 0;
        let bypass = input.swipe == model::SWIPE_UP
            || input.swipe == model::SWIPE_DOWN
            || (input.released & model::BACK != 0 && self.continuing != 0 && self.books != 0);
        let mut result = self.update(input, &HomeTouch::default());
        result.haptic =
            u8::from(touch && !bypass && result.effect.kind == EffectKind::Activate as u8);
        result
    }
    pub fn update(&mut self, input: Input, published: &HomeTouch) -> HomeUpdate {
        let mut out = HomeUpdate::default();
        let count = self.count();
        if self.grid == 0 {
            self.navigate(input, model::NAV_NEXT, 0, count, 1, &mut out);
            self.navigate(input, model::NAV_PREVIOUS, 0, count, -1, &mut out);
        }
        if input.swipe == model::SWIPE_UP || input.swipe == model::SWIPE_DOWN {
            self.band(
                0,
                count,
                if input.swipe == model::SWIPE_UP {
                    1
                } else {
                    -1
                },
                &mut out,
            );
            return out;
        }
        if input.released & model::BACK != 0 && self.continuing != 0 && self.books != 0 {
            out.effect.kind = EffectKind::Activate as u8;
            out.effect.value = 0;
            return out;
        }
        if self.grid != 0 {
            if input.touch_flags & model::TOUCH_RELEASED != 0
                && input.action != 0
                && (0..count).contains(&input.value)
            {
                self.selected = input.value;
                out.effect.kind = EffectKind::Activate as u8;
                out.effect.value = self.activate();
                return out;
            }
            if input.released & model::CONFIRM != 0 {
                out.effect.kind = EffectKind::Activate as u8;
                out.effect.value = self.activate();
                return out;
            }
            let books = i32::from(self.books);
            for (mask, base, total, dir) in [
                (model::UP, 0, books, -1),
                (model::DOWN, 0, books, 1),
                (model::LEFT, books, count - books, -1),
                (model::RIGHT, books, count - books, 1),
            ] {
                self.navigate(input, mask, base, total, dir, &mut out);
            }
            return out;
        }
        if input.touch_flags & (model::TOUCH_PRESSED | model::TOUCH_RELEASED) != 0 {
            let mut selected = -1;
            let c = published.cover;
            let x = i32::from(input.touch_x);
            let y = i32::from(input.touch_y);
            if c.w > 0
                && x >= i32::from(c.x)
                && y >= i32::from(c.y)
                && y < i32::from(c.y) + i32::from(c.h)
            {
                let col = (x - i32::from(c.x)) / i32::from(c.w);
                if col < i32::from(self.books.min(published.columns)) {
                    selected = col;
                }
            }
            if selected < 0 {
                let r = published.menu;
                let step = i32::from(published.menu_step);
                let offset = if published.continue_in_menu != 0 {
                    0
                } else {
                    i32::from(self.books)
                };
                if step > 0
                    && x >= i32::from(r.x)
                    && x < i32::from(r.x) + i32::from(r.w)
                    && y >= i32::from(r.y)
                {
                    let dy = y - i32::from(r.y);
                    if dy / step < count - offset && (r.h <= 0 || dy % step < i32::from(r.h)) {
                        selected = dy / step + offset;
                    }
                }
            }
            if selected >= 0 {
                if input.touch_flags & model::TOUCH_PRESSED != 0 {
                    if self.selected != selected {
                        self.selected = selected;
                        out.effect.invalidate += 1;
                    }
                } else {
                    self.selected = selected;
                    out.haptic = 1;
                    out.effect.kind = EffectKind::Activate as u8;
                    out.effect.value = self.activate();
                }
                return out;
            }
        }
        if input.released & model::CONFIRM != 0 {
            out.effect.kind = EffectKind::Activate as u8;
            out.effect.value = self.activate();
        }
        out
    }
}

pub mod cache;
pub mod ffi;
pub mod file;
pub mod library;
pub mod view;

pub mod grid;

pub mod rows;

pub mod hits;

pub mod file_view;

pub mod list_ffi;

pub mod tabs;

pub mod library_view;

mod abi;

#[cfg(test)]
mod tests;
