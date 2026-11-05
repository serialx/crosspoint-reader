use super::hits::Hits;
use super::list::{text, Content, Resources, Screen, Theme};
use crate::{
    list::{self, Style},
    model::{self, Effect, Input, ListState},
    scene::{Arena, Command, Error, Interaction, Kind, Rect},
};

pub const CONTENTS: u32 = 0x8000_0009;
pub const TEXT: u32 = 0x8000_000a;
pub const MORE: u32 = 0x8000_000b;
pub const PREV: u32 = 0x8000_000c;
pub const NEXT: u32 = 0x8000_000d;
pub const PAGE: u32 = 0x8000_000e;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Model {
    pub panel: u8,
    pub dense_rows: u8,
    pub reserved: [u8; 2],
    pub active_tool: i32,
    pub progress_permille: i32,
    pub chapter_title: u32,
    pub page_info: u32,
    pub panel_title: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Contact {
    pub active: i16,
    pub last_drag_x: i16,
    pub contact_held: u8,
    pub reserved: [u8; 3],
}
impl Default for Contact {
    fn default() -> Self {
        Self {
            active: -1,
            last_drag_x: -1,
            contact_held: 0,
            reserved: [0; 3],
        }
    }
}
impl Contact {
    pub fn route(
        &mut self,
        hits: &(impl Hits + ?Sized),
        input: Input,
        permille: &mut i32,
    ) -> Effect {
        *permille = -1;
        let held = input.touch_flags & model::TOUCH_HELD != 0;
        let released = input.touch_flags & model::TOUCH_RELEASED != 0;
        let pressed = input.touch_flags & model::TOUCH_PRESSED != 0;
        if !held && !released && !pressed {
            return Effect::default();
        }
        let find = |drag: bool| {
            (0..hits.len()).rev().find(|i| {
                let Some(h) = hits.get(*i) else {
                    return false;
                };
                h.flags & 1 == 0
                    && h.inputs & 1 != 0
                    && (!drag || h.action == 5)
                    && h.rect.contains(input.touch_x, input.touch_y)
            })
        };
        let began = held && self.contact_held == 0;
        if held {
            self.contact_held = 1;
        }
        if released {
            self.contact_held = 0;
        }
        if began {
            self.active = find(true).map_or(-1, |i| i as i16);
            self.last_drag_x = -1;
        }
        if pressed {
            self.active = find(false).map_or(-1, |i| i as i16);
        }
        let active = usize::try_from(self.active).ok().and_then(|i| hits.get(i));
        if held && active.is_some_and(|h| h.action == 5 && h.flags & 1 == 0) {
            self.last_drag_x = input.touch_x;
            return Effect {
                invalidate: 1,
                ..Effect::default()
            };
        }
        if released {
            let hit = if self.last_drag_x >= 0
                && active.is_some_and(|h| h.action == 5 && h.flags & 1 == 0)
            {
                active
            } else {
                find(false).and_then(|i| hits.get(i))
            };
            if let Some(hit) = hit {
                if hit.action == 5 {
                    let x = if self.last_drag_x >= 0 && active.is_some_and(|h| h.action == 5) {
                        self.last_drag_x
                    } else {
                        input.touch_x
                    };
                    *permille = if hit.rect.w <= 1 {
                        0
                    } else {
                        ((i32::from(x) - i32::from(hit.rect.x)) * 1000 / i32::from(hit.rect.w - 1))
                            .clamp(0, 1000)
                    };
                }
                self.active = -1;
                self.last_drag_x = -1;
                return Effect {
                    kind: hit.action as u8,
                    value: hit.value,
                    invalidate: 1,
                    reserved: [0; 2],
                };
            }
            self.active = -1;
            self.last_drag_x = -1;
        }
        Effect {
            invalidate: 1,
            ..Effect::default()
        }
    }
}
fn short(n: i32) -> Result<i16, Error> {
    i16::try_from(n).map_err(|_| Error::Invalid)
}
fn hit(a: &mut Arena<'_>, rect: Rect, action: u32, value: i32, g: u32) -> Result<(), Error> {
    a.hit(Interaction {
        rect,
        action,
        value,
        generation: g,
        inputs: 1,
        flags: 0,
    })
}
fn icon(a: &mut Arena<'_>, r: Rect, handle: u32, g: u32) -> Result<(), Error> {
    a.push(Command {
        rect: Rect {
            x: r.x + (r.w - 24) / 2,
            y: r.y + (r.h - 24) / 2,
            w: 24,
            h: 24,
        },
        kind: Kind::Bitmap as u8,
        resource: handle,
        generation: g,
        ..Command::default()
    })
}
fn tools(a: &mut Arena<'_>, row: Rect, active: i32, radius: u8, g: u32) -> Result<(), Error> {
    for (i, handle) in [CONTENTS, TEXT, MORE].into_iter().enumerate() {
        let slot = Rect {
            x: row.x + row.w / 3 * i as i16,
            w: row.w / 3,
            ..row
        };
        if i as i32 == active {
            a.push(Command {
                rect: Rect {
                    x: slot.x + 10,
                    y: slot.y + 4,
                    w: slot.w - 20,
                    h: slot.h - 8,
                },
                kind: Kind::Stroke as u8,
                width: 2,
                radius,
                ..Command::default()
            })?;
        }
        icon(a, slot, handle, g)?;
        hit(a, slot, 2, i as i32, g)?;
    }
    Ok(())
}
fn sheet(a: &mut Arena<'_>, theme: Theme, height: i16, g: u32) -> Result<Rect, Error> {
    let safe = theme.safe;
    if height < 45 || height > safe.h {
        return Err(Error::Invalid);
    }
    let y = safe.y + safe.h - height;
    let rect = Rect {
        x: 0,
        y,
        w: theme.screen.w,
        h: theme.screen.h - y,
    };
    a.push(Command {
        rect,
        paint: 1,
        radius: theme.sheet_radius,
        corners: 3,
        ..Command::default()
    })?;
    a.push(Command {
        rect: Rect { h: 2, ..rect },
        ..Command::default()
    })?;
    a.push(Command {
        rect: Rect {
            x: (rect.w - 72) / 2,
            y: y + theme.space_lg + theme.space_md,
            w: 72,
            h: 5,
        },
        radius: 2,
        ..Command::default()
    })?;
    if y > safe.y {
        hit(
            a,
            Rect {
                h: y - safe.y,
                ..safe
            },
            1,
            0,
            g,
        )?;
    }
    let band = 2 * theme.space_lg + theme.space_md + 5;
    Ok(Rect {
        x: safe.x,
        y: y + band,
        w: safe.w,
        h: height - band,
    })
}
#[allow(clippy::too_many_arguments)]
pub fn view(
    a: &mut Arena<'_>,
    state: ListState,
    m: Model,
    t: Theme,
    r: &mut dyn Resources,
    count: u16,
    capacity: u16,
    g: u32,
) -> Result<ListState, Error> {
    t.validate()?;
    if m.panel > 1 || m.dense_rows > 1 || t.space_lg > 128 || t.space_md > 128 || t.space_sm > 128 {
        return Err(Error::Invalid);
    }
    let grabber = 2 * i32::from(t.space_lg) + i32::from(t.space_md) + 5;
    if m.panel == 0 {
        let meta = short(r.line(t.small_font)?)?;
        let height = short(
            grabber
                + i32::from(t.space_md)
                + 36
                + i32::from(t.space_lg)
                + i32::from(meta)
                + 2 * i32::from(t.space_sm)
                + 80,
        )?;
        let mut body = sheet(a, t, height, g)?;
        body.x += t.space_lg;
        body.w -= 2 * t.space_lg;
        body.y += t.space_md;
        if body.w <= 96 {
            return Err(Error::Invalid);
        }
        let band = Rect { h: 36, ..body };
        for (x, handle, action) in [(band.x, PREV, 3), (band.x + band.w - 36, NEXT, 4)] {
            let button = Rect { x, w: 36, ..band };
            a.push(Command {
                rect: button,
                paint: 1,
                radius: t.control_radius,
                ..Command::default()
            })?;
            a.push(Command {
                rect: button,
                kind: Kind::Stroke as u8,
                width: 1,
                radius: t.control_radius,
                ..Command::default()
            })?;
            icon(a, button, handle, g)?;
            let minimum = short((r.line(t.body_font)? + 14).max(44))?;
            let mut touch = Rect {
                x: button.x - (minimum - button.w) / 2,
                y: button.y - (minimum - button.h) / 2,
                w: minimum,
                h: minimum,
            };
            touch.x = touch.x.clamp(0, t.screen.w - touch.w);
            touch.y = touch.y.clamp(0, t.screen.h - touch.h);
            if touch.x < 12 {
                touch.w += touch.x;
                touch.x = 0;
            }
            if t.screen.w - touch.x - touch.w < 12 {
                touch.w = t.screen.w - touch.x;
            }
            hit(a, touch, action, 0, g)?;
        }
        let track = Rect {
            x: band.x + 48,
            w: band.w - 96,
            ..band
        };
        a.push(Command {
            rect: Rect {
                y: band.y + 17,
                h: 2,
                ..track
            },
            ..Command::default()
        })?;
        let knob = short(
            i32::from(track.x)
                + (i32::from(track.w) - 1) * m.progress_permille.clamp(0, 1000) / 1000,
        )?;
        a.push(Command {
            rect: Rect {
                x: knob - 8,
                y: band.y + 10,
                w: 16,
                h: 16,
            },
            radius: 8,
            ..Command::default()
        })?;
        hit(a, track, 5, 0, g)?;
        let line = Rect {
            y: band.y + 36 + t.space_lg,
            h: meta,
            ..body
        };
        let width = if m.page_info != 0 {
            short(r.width(m.page_info, t.small_font, 0)?)?
        } else {
            0
        };
        text(
            a,
            Rect {
                w: short(i32::from(line.w) - i32::from(width) - i32::from(t.space_md))?,
                ..line
            },
            m.chapter_title,
            g,
            t.small_font,
            0,
            1,
            0,
            1,
        )?;
        text(a, line, m.page_info, g, t.small_font, 0, 0, 2, 1)?;
        tools(
            a,
            Rect {
                y: line.y + line.h + t.space_sm,
                h: 80,
                ..line
            },
            m.active_tool,
            t.control_radius,
            g,
        )?;
        return Ok(state);
    }
    let title = short(r.line(t.title_font)?)?;
    let row = if m.dense_rows != 0 {
        t.row_min
    } else {
        short(r.line(t.body_font)? * 2 + 8)?
    };
    let gap = if m.dense_rows != 0 {
        t.row_gap
    } else {
        t.row_gap.max(6)
    };
    let stride = i32::from(row) + i32::from(gap);
    if stride <= 0 {
        return Err(Error::Invalid);
    }
    let chrome =
        grabber + i32::from(title) + i32::from(t.space_md) + 2 * i32::from(t.space_sm) + 80;
    let landscape = t.safe.w > t.safe.h;
    let target = i32::from(t.safe.h) * if landscape { 88 } else { 62 } / 100;
    let cap = i32::from(t.safe.h) * if landscape { 88 } else { 72 } / 100;
    let mut rows = (target - chrome + i32::from(gap)) / stride;
    if chrome + (rows + 1) * stride - i32::from(gap) <= cap {
        rows += 1;
    }
    if count > 0 {
        rows = rows.min(i32::from(count));
    }
    rows = rows.max(1);
    let mut body = sheet(a, t, short(chrome + rows * stride - i32::from(gap))?, g)?;
    let title_rect = Rect {
        x: body.x + t.space_lg,
        w: body.w - 2 * t.space_lg,
        h: title,
        ..body
    };
    text(a, title_rect, m.panel_title, g, t.title_font, 0, 1, 0, 1)?;
    body.y += title + t.space_md;
    body.h -= title + t.space_md;
    let tool_row = Rect {
        x: body.x + t.space_lg,
        y: body.y + body.h - t.space_sm - 80,
        w: body.w - 2 * t.space_lg,
        h: 80,
    };
    tools(a, tool_row, m.active_tool, t.control_radius, g)?;
    body.h -= 2 * t.space_sm + 80;
    let style = Style {
        area: body,
        screen: t.screen,
        min_touch: short((r.line(t.body_font)? + 14).max(44))?,
        font: t.body_font,
        row_height: row,
        row_gap: gap,
        inset: t.space_lg,
        padding: 0,
        scroll_width: t.scroll_width,
        radius: t.row_radius,
        selection: t.selection,
        scroll_left: t.scroll_left,
        bold: t.title_bold,
    };
    let visible = ((i32::from(body.h) + i32::from(gap)) / stride).max(1) as u16;
    let mut content = Content::new(
        r,
        count,
        capacity.min(visible),
        Screen::Panel,
        t.small_font,
        t.body_font,
        -1,
    );
    let next = if count > 0 {
        list::view_rows(a, state, style, count, g, &mut content)?
    } else {
        state
    };
    let page_rows = if next.drawn > 0 {
        next.drawn
    } else {
        next.visible
    }
    .max(1);
    if (i32::from(count) + page_rows - 1) / page_rows > 1 {
        text(a, title_rect, PAGE, g, t.small_font, 0, 0, 2, 1)?;
    }
    Ok(next)
}
