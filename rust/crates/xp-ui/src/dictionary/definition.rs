use super::view::{BODY, FOOTER, HEADER};
use crate::{
    model::{self, Effect, Input},
    scene::{Arena, Command, Error, Kind, Rect},
};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub page: i32,
    pub pages: i32,
    pub message_time: u32,
    pub last_repeat: u32,
    pub message: u16,
    pub message_visible: u8,
    pub failed: u8,
    pub lookup: u8,
    pub history_depth: u8,
    pub reserved: [u8; 2],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub top: i16,
    pub right: i16,
    pub bottom: i16,
    pub left: i16,
    pub top_padding: i16,
    pub header_height: i16,
    pub spacing: i16,
    pub side_padding: i16,
    pub hints_height: i16,
    pub side_hints_width: i16,
    pub orientation: u8,
    pub touch_controls: u8,
    pub reserved: [u8; 2],
}
pub const FINISH: u8 = 1;
pub const CANCEL: u8 = 2;
pub const BACK: u8 = 3;
pub const SELECT: u8 = 4;
pub const TOUCH_SELECT: u8 = 5;
impl State {
    pub fn update(
        &mut self,
        input: Input,
        tap: bool,
        long_press: bool,
        header_long: bool,
        width: i32,
        touch_controls: bool,
    ) -> Effect {
        let expired = input.now_ms.wrapping_sub(self.message_time) >= 1500;
        if self.failed != 0 {
            return Effect {
                kind: if expired { FINISH } else { 0 },
                ..Effect::default()
            };
        }
        let mut repaint = false;
        if self.message_visible != 0 && expired {
            self.message_visible = 0;
            repaint = true;
        }
        if header_long || input.released & model::BACK != 0 {
            let kind = if self.lookup != 0 && (header_long || input.held_ms >= 1000) {
                CANCEL
            } else if self.lookup != 0 && self.history_depth > 0 {
                BACK
            } else {
                FINISH
            };
            return Effect {
                kind,
                invalidate: u8::from(repaint),
                ..Effect::default()
            };
        }
        if long_press && touch_controls {
            return Effect {
                kind: TOUCH_SELECT,
                invalidate: u8::from(repaint),
                ..Effect::default()
            };
        }
        if self.lookup != 0 && input.released & model::CONFIRM != 0 {
            return Effect {
                kind: SELECT,
                invalidate: u8::from(repaint),
                ..Effect::default()
            };
        }
        let mut navigated = false;
        if tap {
            if i32::from(input.touch_x) < width / 3 {
                if self.page > 0 {
                    self.page -= 1;
                    navigated = true;
                }
            } else if self.page + 1 < self.pages {
                self.page += 1;
                navigated = true;
            }
        } else {
            // Preserve ButtonNavigator's next-then-previous ordering and shared repeat time.
            for (button, direction) in [(model::NAV_NEXT, 1), (model::NAV_PREVIOUS, -1)] {
                if input.pressed & button != 0 {
                    if direction > 0 && self.page + 1 < self.pages {
                        self.page += 1;
                        navigated = true;
                    }
                    if direction < 0 && self.page > 0 {
                        self.page -= 1;
                        navigated = true;
                    }
                }
                if (input.pressed | input.released) & button != 0 {
                    self.last_repeat = 0;
                    continue;
                }
                if input.held & button != 0
                    && input.held_ms > 500
                    && input.now_ms.wrapping_sub(self.last_repeat) > 500
                {
                    if direction > 0 && self.page + 1 < self.pages {
                        self.page += 1;
                        navigated = true;
                    }
                    if direction < 0 && self.page > 0 {
                        self.page -= 1;
                        navigated = true;
                    }
                    self.last_repeat = input.now_ms;
                }
            }
        }
        Effect {
            invalidate: u8::from(repaint || navigated),
            value: i32::from(navigated),
            ..Effect::default()
        }
    }
}
pub fn body(l: &Layout) -> Rect {
    let landscape = l.orientation == 1 || l.orientation == 3;
    let gutter = if landscape { l.side_hints_width } else { 0 };
    let left = l.left.max(if l.orientation == 1 { gutter } else { 0 });
    let right = l.right.max(if l.orientation == 3 { gutter } else { 0 });
    let x = left + l.side_padding;
    let y = l.top.max(if l.orientation == 2 {
        l.hints_height
    } else {
        0
    }) + l.top_padding
        + l.header_height
        + l.spacing;
    Rect {
        x,
        y,
        w: l.screen.w - left - right - 2 * l.side_padding,
        h: l.screen.h - y - l.bottom.max(l.hints_height) - l.spacing,
    }
}
pub fn view(l: &Layout, arena: &mut Arena<'_>) -> Result<(), Error> {
    let gutter = if l.orientation == 1 || l.orientation == 3 {
        l.side_hints_width
    } else {
        0
    };
    let x = if l.orientation == 1 { gutter } else { 0 };
    let y = if l.orientation == 2 {
        l.hints_height
    } else {
        0
    };
    arena.push(Command {
        kind: Kind::Clear as u8,
        ..Command::default()
    })?;
    for (handle, r) in [
        (
            HEADER,
            Rect {
                x,
                y: y + l.top_padding,
                w: l.screen.w - gutter,
                h: l.header_height,
            },
        ),
        (BODY, body(l)),
        (FOOTER, l.screen),
    ] {
        arena.push(Command {
            kind: Kind::Page as u8,
            resource: handle,
            generation: 1,
            rect: r,
            ..Command::default()
        })?;
    }
    Ok(())
}
