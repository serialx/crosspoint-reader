//! Selection policy over immutable word records borrowed from retained pages.
use crate::{model, scene::Rect};
use core::ffi::c_char;

#[repr(C)]
#[derive(Clone, Copy)]
#[allow(non_snake_case)]
pub struct Word {
    pub x: i16,
    pub y: i16,
    pub width: i16,
    pub height: i16,
    pub row: u16,
    pub pageOffset: u8,
    pub characterSpacing: i8,
    pub pageWordIndex: u16,
    pub reserved: u16,
    pub fontId: i32,
    pub startOffset: u32,
    pub endOffset: u32,
    pub text: *const c_char,
    pub style: u8,
    pub paragraphStart: u8,
    pub isRtl: u8,
    pub discretionaryHyphen: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub selected: i32,
    pub range_start: i32,
    pub page: i32,
    pub row_count: i32,
    pub drag_x: i32,
    pub drag_y: i32,
    pub drag_start_x: i32,
    pub drag_start_y: i32,
    pub dwell_index: i32,
    pub dwell_since: u32,
    pub last_repeat: u32,
    pub dragging: u8,
    pub ignore_touch: u8,
    pub moved: u8,
    pub mode: u8,
    pub repeating: u8,
    pub pending_count: u8,
    pub reserved: [u8; 2],
    pub pending: [u8; 16],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub safe: Rect,
    pub top: i16,
    pub right: i16,
    pub bottom: i16,
    pub left: i16,
    pub spacing: i16,
    pub padding: i16,
    pub top_padding: i16,
    pub line_height: i16,
    pub label_width: i16,
    pub label_height: i16,
    pub action_font: i32,
    pub touch: u8,
    pub radius: u8,
    pub reserved: [u8; 2],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Painted {
    pub selected: i32,
    pub range_start: i32,
    pub page: i32,
    pub offset_x: i32,
    pub offset_y: i32,
    pub valid: u8,
    pub reserved: [u8; 3],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Input {
    pub input: model::Input,
    pub tap: u8,
    pub pressed: u8,
    pub held: u8,
    pub popup: u8,
    pub reserved: [u8; 4],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Update {
    pub kind: u8,
    pub invalidate: u8,
    pub action: u8,
    pub reserved: u8,
}
const LEFT: u8 = 1;
const RIGHT: u8 = 2;
const UP: u8 = 4;
const DOWN: u8 = 8;
const CONFIRM: u8 = 16;
const BACK: u8 = 32;
pub const CANCEL: u8 = 1;
pub const CONFIRMED: u8 = 2;

fn rect(x: i32, y: i32, w: i32, h: i32) -> Rect {
    Rect {
        x: x as i16,
        y: y as i16,
        w: w as i16,
        h: h as i16,
    }
}
fn contains(r: Rect, x: i32, y: i32) -> bool {
    x >= i32::from(r.x)
        && x < i32::from(r.x) + i32::from(r.w)
        && y >= i32::from(r.y)
        && y < i32::from(r.y) + i32::from(r.h)
}
fn keep_visible(position: i32, extent: i32, start: i32, size: i32) -> i32 {
    if position < start {
        start - position
    } else {
        (start + size - position - extent).min(0)
    }
}
pub fn closest(words: &[Word], row: u16, center: i32) -> Option<usize> {
    words
        .iter()
        .enumerate()
        .filter(|(_, w)| w.row == row)
        .min_by_key(|(_, w)| (i32::from(w.x) + i32::from(w.width) / 2 - center).abs())
        .map(|(i, _)| i)
}
impl State {
    pub fn valid(&self, words: &[Word]) -> bool {
        self.mode <= 2
            && self.selected >= 0
            && (self.selected as usize) < words.len()
            && self.range_start >= -1
            && (self.range_start < 0 || (self.range_start as usize) < words.len())
    }
    pub fn init(words: &[Word], mode: u8, screen_width: i32, first: i32, last: i32) -> Self {
        let rows = words
            .iter()
            .take_while(|w| w.pageOffset == 0)
            .map(|w| w.row.saturating_add(1))
            .max()
            .unwrap_or(0);
        let selected = if last >= 0 && (last as usize) < words.len() {
            last
        } else {
            closest(words, rows / 2, screen_width / 2).unwrap_or(0) as i32
        };
        Self {
            selected,
            range_start: first,
            page: words
                .get(selected as usize)
                .map_or(0, |w| i32::from(w.pageOffset)),
            row_count: words
                .iter()
                .map(|w| i32::from(w.row) + 1)
                .max()
                .unwrap_or(0),
            dwell_index: -1,
            mode,
            ..Self::default()
        }
    }
    pub fn bounds(&self) -> (usize, usize) {
        if self.range_start < 0 {
            (self.selected as usize, self.selected as usize)
        } else {
            (
                self.selected.min(self.range_start) as usize,
                self.selected.max(self.range_start) as usize,
            )
        }
    }
    pub fn offsets(&self, words: &[Word], layout: &Layout) -> (i32, i32) {
        if layout.touch != 0 {
            return (0, 0);
        }
        let Some(w) = words.get(self.selected as usize) else {
            return (0, 0);
        };
        (
            keep_visible(
                w.x.into(),
                w.width.into(),
                layout.safe.x.into(),
                layout.safe.w.into(),
            ),
            keep_visible(
                w.y.into(),
                w.height.into(),
                layout.safe.y.into(),
                layout.safe.h.into(),
            ),
        )
    }
    pub fn word_at(
        &self,
        words: &[Word],
        layout: &Layout,
        x: i32,
        y: i32,
        drag: bool,
    ) -> Option<usize> {
        let y = y - self.offsets(words, layout).1;
        let rows = words
            .iter()
            .enumerate()
            .filter(|(_, w)| i32::from(w.pageOffset) == self.page);
        if drag {
            rows.min_by_key(|(_, w)| {
                (
                    (i32::from(w.y) + i32::from(w.height) / 2 - y).abs(),
                    (i32::from(w.x) - x)
                        .max(0)
                        .max(x - i32::from(w.x) - i32::from(w.width)),
                )
            })
            .map(|(i, _)| i)
        } else {
            rows.filter(|(_, w)| {
                contains(
                    rect(
                        i32::from(w.x) - 4,
                        i32::from(w.y) - 4,
                        i32::from(w.width) + 8,
                        i32::from(w.height) + 8,
                    ),
                    x,
                    y,
                )
            })
            .map(|(i, _)| i)
            .next()
        }
    }
    pub fn handle(&self, words: &[Word], layout: &Layout, index: usize, start: bool) -> Rect {
        let Some(w) = words.get(index) else {
            return Rect::default();
        };
        let safe = layout.safe;
        let size = 24.max(i32::from(layout.spacing) * 2);
        let left = start != (w.isRtl != 0);
        let edge = i32::from(w.x) + if left { 0 } else { i32::from(w.width) };
        let max_x = (i32::from(safe.x) + i32::from(safe.w) - size).max(safe.x.into());
        let max_y = (i32::from(safe.y) + i32::from(safe.h) - size).max(safe.y.into());
        rect(
            (edge - if left { size } else { 0 }).clamp(safe.x.into(), max_x),
            (i32::from(w.y) + self.offsets(words, layout).1 + i32::from(w.height))
                .clamp(safe.y.into(), max_y),
            size,
            size,
        )
    }
    pub fn actions(&self, words: &[Word], l: &Layout) -> Rect {
        let right = (i32::from(l.safe.x) + i32::from(l.safe.w))
            .min(i32::from(l.screen.w) - i32::from(l.right));
        let bottom = (i32::from(l.safe.y) + i32::from(l.safe.h))
            .min(i32::from(l.screen.h) - i32::from(l.bottom));
        let x = i32::from(l.safe.x).max(l.left.into());
        let y = i32::from(l.safe.y).max(l.top.into()) + i32::from(l.top_padding.max(l.spacing));
        let safe_width = right - x;
        let _safe_height = bottom - y;
        let count = if self.mode == 2 { 1 } else { 3 };
        let padding = i32::from(l.padding);
        let width = safe_width
            .min(count * (i32::from(l.label_width) + padding * 2) + padding * (count + 1));
        let lines = if i32::from(l.label_width) > (width - padding * (count + 1)) / count {
            2
        } else {
            1
        };
        let height = 36.max(i32::from(l.label_height) * lines + padding * 2) + padding * 2;
        let (first, last) = self.bounds();
        let mut anchor = first;
        while anchor < last
            && words
                .get(anchor)
                .is_some_and(|w| i32::from(w.pageOffset) < self.page)
        {
            anchor += 1;
        }
        let top = words
            .get(first..=last)
            .unwrap_or(&[])
            .iter()
            .filter(|w| i32::from(w.pageOffset) == self.page)
            .map(|w| i32::from(w.y))
            .fold(i32::from(l.screen.h), i32::min);
        let anchor_x = words.get(anchor).map_or(0, |w| i32::from(w.x)) - padding;
        rect(
            anchor_x.clamp(x, (x + safe_width - width).max(x)),
            y.max(top - height - i32::from(l.spacing)),
            width,
            height,
        )
    }
    pub fn action_at(&self, words: &[Word], layout: &Layout, x: i32, y: i32) -> Option<u8> {
        let count = if self.mode == 2 { 1 } else { 3 };
        let r = self.actions(words, layout);
        (0..count)
            .find(|&i| contains(action_button(r, layout.padding, count, i), x, y))
            .map(|v| v as u8)
    }
    pub fn contains(&self, words: &[Word], l: &Layout, x: i32, y: i32) -> bool {
        let mut previous: Option<&Word> = None;
        let (first, last) = self.bounds();
        let offset = self.offsets(words, l).1;
        for w in words.get(first..=last).unwrap_or(&[]) {
            if i32::from(w.pageOffset) != self.page {
                continue;
            }
            let mut left = i32::from(w.x);
            let mut right = left + i32::from(w.width);
            if let Some(p) = previous.filter(|p| p.row == w.row) {
                left = left.min(p.x.into());
                right = right.max(i32::from(p.x) + i32::from(p.width));
            }
            if contains(
                rect(left, i32::from(w.y) + offset, right - left, w.height.into()),
                x,
                y,
            ) {
                return true;
            }
            previous = Some(w);
        }
        false
    }
    pub fn next_page(&self, words: &[Word]) -> Option<usize> {
        if self.moved == 0 || self.range_start < 0 || self.selected < self.range_start {
            return None;
        }
        let current = words.get(self.selected as usize)?.pageOffset;
        words
            .iter()
            .enumerate()
            .skip(self.selected as usize + 1)
            .find(|(_, w)| w.pageOffset > current)
            .map(|(i, _)| i)
    }
    pub fn dwell(&self, words: &[Word], l: &Layout, x: i32, y: i32) -> bool {
        let Some(w) = words.get(self.selected as usize) else {
            return false;
        };
        let safe = l.safe;
        if x >= i32::from(safe.x)
            && x < i32::from(safe.x) + i32::from(safe.w)
            && y >= i32::from(safe.y) + i32::from(safe.h) - i32::from(l.line_height)
        {
            return true;
        }
        if words
            .get(self.selected as usize + 1)
            .is_some_and(|w| i32::from(w.pageOffset) == self.page)
        {
            return false;
        }
        i32::from(w.pageOffset) == self.page
            && contains(
                rect(
                    i32::from(w.x) - 8,
                    i32::from(w.y) + self.offsets(words, l).1 - 8,
                    i32::from(w.width) + 16,
                    i32::from(w.height) + 16,
                ),
                x,
                y,
            )
    }
    pub fn select(&mut self, words: &[Word], index: usize) -> bool {
        let Some(word) = words.get(index) else {
            return false;
        };
        if index as i32 == self.selected {
            return false;
        }
        self.selected = index as i32;
        self.page = i32::from(word.pageOffset);
        true
    }
    pub fn vertical(&mut self, words: &[Word], direction: i32) -> bool {
        let Some(w) = words.get(self.selected as usize) else {
            return false;
        };
        let row = i32::from(w.row) + direction;
        if row < 0 || row >= self.row_count {
            return false;
        }
        let Some(index) = closest(words, row as u16, i32::from(w.x) + i32::from(w.width) / 2)
        else {
            return false;
        };
        self.select(words, index)
    }
    pub fn move_page(&mut self, words: &[Word], page: i32) -> bool {
        if page < 0 || page == self.page {
            return false;
        }
        words
            .iter()
            .position(|w| i32::from(w.pageOffset) == page)
            .is_some_and(|i| self.select(words, i))
    }
    fn confirm(&mut self, action: u8) -> Update {
        if self.range_start < 0 {
            self.range_start = self.selected;
            Update {
                invalidate: 1,
                ..Update::default()
            }
        } else {
            Update {
                kind: CONFIRMED,
                action,
                ..Update::default()
            }
        }
    }
    pub fn buttons(&mut self, words: &[Word], buttons: u8) -> Update {
        if buttons & BACK != 0 {
            if self.range_start >= 0 && self.mode != 2 {
                self.range_start = -1;
                return Update {
                    invalidate: 1,
                    ..Update::default()
                };
            }
            return Update {
                kind: CANCEL,
                ..Update::default()
            };
        }
        if !self.valid(words) {
            return Update::default();
        }
        if buttons & CONFIRM != 0 {
            return self.confirm(u8::from(self.mode != 0));
        }
        let w = &words[self.selected as usize];
        let direction = i32::from(buttons & RIGHT != 0) - i32::from(buttons & LEFT != 0);
        let next = (self.selected + if w.isRtl != 0 { -direction } else { direction })
            .clamp(0, words.len() as i32 - 1);
        let changed = if next != self.selected {
            self.select(words, next as usize)
        } else if buttons & UP != 0 {
            self.vertical(words, -1)
        } else if buttons & DOWN != 0 {
            self.vertical(words, 1)
        } else {
            false
        };
        Update {
            invalidate: u8::from(changed),
            ..Update::default()
        }
    }
    pub fn poll(&mut self, input: model::Input) -> bool {
        let mut buttons = edges(input);
        let held = u8::from(input.held & model::SCREEN_LEFT != 0)
            | (u8::from(input.held & model::SCREEN_RIGHT != 0) << 1);
        if held != self.repeating || buttons & (LEFT | RIGHT) != 0 {
            self.repeating = held;
            self.last_repeat = input.now_ms;
        } else if (held == LEFT || held == RIGHT)
            && input.now_ms.wrapping_sub(self.last_repeat) >= 500
        {
            buttons |= held;
            self.last_repeat = input.now_ms;
        }
        if buttons == 0 {
            return true;
        }
        if let Some(slot) = self.pending.get_mut(self.pending_count as usize) {
            *slot = buttons;
            self.pending_count += 1;
            true
        } else {
            false
        }
    }
    pub fn dequeue(&mut self) -> u8 {
        let count = usize::from(self.pending_count).min(self.pending.len());
        if count == 0 {
            return 0;
        }
        let value = self.pending[0];
        self.pending.copy_within(1..count, 0);
        self.pending_count -= 1;
        value
    }
    pub fn update(&mut self, words: &[Word], layout: &Layout, event: Input) -> Update {
        if !self.valid(words) {
            return Update {
                kind: CANCEL,
                ..Update::default()
            };
        }
        let input = event.input;
        let x = i32::from(input.touch_x);
        let y = i32::from(input.touch_y);
        if self.ignore_touch != 0 {
            if event.held == 0 {
                self.ignore_touch = 0;
            }
            return Update::default();
        }
        if self.dragging == 0 && event.tap != 0 {
            if self.range_start >= 0 {
                if let Some(action) = self.action_at(words, layout, x, y) {
                    return self.confirm(match action {
                        0 => 1,
                        1 => 0,
                        _ => 2,
                    });
                }
                let (first, last) = self.bounds();
                let start = i32::from(words[first].pageOffset) == self.page
                    && contains(self.handle(words, layout, first, true), x, y);
                let end = i32::from(words[last].pageOffset) == self.page
                    && contains(self.handle(words, layout, last, false), x, y);
                if !start && !end && !self.contains(words, layout, x, y) {
                    return Update {
                        kind: CANCEL,
                        ..Update::default()
                    };
                }
            } else if let Some(hit) = self.word_at(words, layout, x, y, false) {
                self.selected = hit as i32;
                self.range_start = self.selected;
                return Update {
                    invalidate: 1,
                    ..Update::default()
                };
            } else {
                return Update {
                    kind: CANCEL,
                    ..Update::default()
                };
            }
            return Update::default();
        }
        if self.dragging != 0 {
            if event.held != 0 {
                self.moved |= u8::from(
                    (x - self.drag_start_x).abs() >= 4 || (y - self.drag_start_y).abs() >= 4,
                );
                let mut changed = self
                    .word_at(words, layout, x + self.drag_x, y + self.drag_y, true)
                    .is_some_and(|i| self.select(words, i));
                if let Some(next) = self.next_page(words).filter(|_| {
                    self.dwell(words, layout, x, y)
                        || self.dwell(words, layout, x + self.drag_x, y + self.drag_y)
                }) {
                    if self.dwell_index != next as i32 {
                        self.dwell_index = next as i32;
                        self.dwell_since = input.now_ms;
                    } else if input.now_ms.wrapping_sub(self.dwell_since) >= 1000 {
                        self.dwell_index = -1;
                        changed |= self.select(words, next);
                        self.drag_x = 0;
                        self.drag_y = 0;
                    }
                } else {
                    self.dwell_index = -1;
                }
                return Update {
                    invalidate: u8::from(changed),
                    ..Update::default()
                };
            }
            self.dragging = 0;
            self.moved = 0;
            self.dwell_index = -1;
            return Update {
                invalidate: 1,
                ..Update::default()
            };
        }
        if event.pressed != 0 {
            if self.range_start >= 0 {
                if self.action_at(words, layout, x, y).is_some() {
                    return Update::default();
                }
                let (first, last) = self.bounds();
                for (index, other, start) in [(first, last, true), (last, first, false)] {
                    if i32::from(words[index].pageOffset) != self.page
                        || !contains(self.handle(words, layout, index, start), x, y)
                    {
                        continue;
                    }
                    self.selected = index as i32;
                    self.range_start = other as i32;
                    self.drag_x = i32::from(words[index].x) + i32::from(words[index].width) / 2 - x;
                    self.drag_y = i32::from(words[index].y)
                        + self.offsets(words, layout).1
                        + i32::from(words[index].height) / 2
                        - y;
                    self.start_drag(x, y);
                    return Update::default();
                }
            } else if let Some(hit) = self.word_at(words, layout, x, y, false) {
                self.selected = hit as i32;
                self.range_start = self.selected;
                self.drag_x = 0;
                self.drag_y = 0;
                self.start_drag(x, y);
                return Update {
                    invalidate: 1,
                    ..Update::default()
                };
            }
            return Update::default();
        }
        let buttons = edges(input);
        if buttons & (BACK | CONFIRM) != 0 {
            return self.buttons(words, buttons);
        }
        let delta = match input.swipe {
            model::SWIPE_LEFT | model::SWIPE_UP => 1,
            model::SWIPE_RIGHT | model::SWIPE_DOWN => -1,
            _ => 0,
        };
        if delta != 0 {
            return Update {
                invalidate: u8::from(self.move_page(words, self.page + delta)),
                ..Update::default()
            };
        }
        self.buttons(words, buttons)
    }
    fn start_drag(&mut self, x: i32, y: i32) {
        self.dragging = 1;
        self.moved = 0;
        self.drag_start_x = x;
        self.drag_start_y = y;
        self.dwell_index = -1;
    }
}
pub fn action_button(r: Rect, padding: i16, count: i32, index: i32) -> Rect {
    let p = i32::from(padding);
    let width = (i32::from(r.w) - p * (count + 1)) / count;
    rect(
        i32::from(r.x) + p + index * (width + p),
        i32::from(r.y) + p,
        width,
        i32::from(r.h) - p * 2,
    )
}
pub fn edges(input: model::Input) -> u8 {
    u8::from(input.pressed & model::SCREEN_LEFT != 0)
        | (u8::from(input.pressed & model::SCREEN_RIGHT != 0) << 1)
        | (u8::from(input.pressed & model::SCREEN_UP != 0) << 2)
        | (u8::from(input.pressed & model::SCREEN_DOWN != 0) << 3)
        | (u8::from(input.released & model::CONFIRM != 0) << 4)
        | (u8::from(input.released & model::BACK != 0) << 5)
        | (u8::from(input.pressed & model::NAV_PREVIOUS != 0) << 6)
        | (u8::from(input.pressed & model::NAV_NEXT != 0) << 7)
}
