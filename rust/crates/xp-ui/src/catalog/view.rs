use super::{ffi::Services, state::*};
use crate::{
    list::{self, Rows, Style},
    model::ListState,
    scene::{Arena, Command, Error, Interaction, Kind, Rect},
    settings::view::{text, Content, Resources},
};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub list: Style,
    pub safe: Rect,
    pub search_button: Rect,
    pub small_font: i32,
    pub top_padding: i16,
    pub header_height: i16,
    pub hints_height: i16,
    pub spacing: i16,
    pub tab_height: i16,
    pub side_padding: i16,
    pub space_md: i16,
    pub button_height: i16,
    pub page_current: i32,
    pub page_total: i32,
    pub touch: u8,
    pub search: u8,
    pub pages: u8,
    pub control_radius: u8,
}
pub const HEADER: u32 = 0x8000_0001;
pub const FOOTER: u32 = 0x8000_0002;
pub const STATUS: u32 = 0x8000_0003;
pub const EMPTY: u32 = 0x8000_0004;
pub const ERROR_TITLE: u32 = 0x8000_0005;
pub const ERROR_TEXT: u32 = 0x8000_0006;
pub const TAP_RETRY: u32 = 0x8000_0007;
pub const DOWNLOADING_TEXT: u32 = 0x8000_0008;
pub const BYTES: u32 = 0x8000_0009;
pub const CANCEL_TEXT: u32 = 0x8000_000a;
pub const URL_HINT: u32 = 0x8000_000b;
pub const PAGE_TEXT: u32 = 0x8000_000c;
pub const ICON_FIRST: u32 = 0x8000_0010;
fn narrow(v: i32) -> Result<i16, Error> {
    i16::try_from(v).map_err(|_| Error::Invalid)
}
fn touch_rect(mut r: Rect, bounds: Rect, minimum: i16) -> Rect {
    if r.w < minimum {
        r.x -= (minimum - r.w) / 2;
        r.w = minimum;
    }
    if r.h < minimum {
        r.y -= (minimum - r.h) / 2;
        r.h = minimum;
    }
    r.x = r.x.max(bounds.x).min(bounds.x + bounds.w - r.w);
    r.y = r.y.max(bounds.y).min(bounds.y + bounds.h - r.h);
    if r.x - bounds.x < 12 {
        r.w += r.x - bounds.x;
        r.x = bounds.x;
    }
    if r.y - bounds.y < 12 {
        r.h += r.y - bounds.y;
        r.y = bounds.y;
    }
    if bounds.x + bounds.w - r.x - r.w < 12 {
        r.w = bounds.x + bounds.w - r.x;
    }
    if bounds.y + bounds.h - r.y - r.h < 12 {
        r.h = bounds.y + bounds.h - r.y;
    }
    r
}
fn resource(a: &mut Arena<'_>, rect: Rect, h: u32) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::Page as u8,
        rect,
        resource: h,
        generation: 1,
        ..Command::default()
    })
}
fn label(a: &mut Arena<'_>, r: Rect, h: u32, f: i32, lines: u8, bold: u8) -> Result<(), Error> {
    text(a, r, h, 1, f, 0, bold, 1, lines)
}
struct GroupRows<'a> {
    content: Content<'a>,
    services: Services,
    small: i32,
}
impl Rows for GroupRows<'_> {
    fn height(&mut self, i: u16, w: i16, s: Style) -> Result<i16, Error> {
        self.content.height(i, w, s)
    }
    fn paint(
        &mut self,
        a: &mut Arena<'_>,
        i: u16,
        r: Rect,
        s: Style,
        f: u8,
        g: u32,
    ) -> Result<(), Error> {
        self.content.paint(a, i, r, s, f, g)
    }
    fn section(&mut self, i: u16, first: bool, s: Style) -> Result<(i16, i16), Error> {
        let heading = self.services.data(i)?.heading;
        if heading.is_null() || unsafe { *heading } == 0 {
            return Ok((0, 0));
        }
        let pad = if first { 0 } else { 16 };
        Ok((
            pad,
            narrow(i32::from(pad) + self.services.line(self.small)? + 4 + i32::from(s.row_gap))?,
        ))
    }
    fn paint_section(
        &mut self,
        a: &mut Arena<'_>,
        i: u16,
        r: Rect,
        s: Style,
        g: u32,
    ) -> Result<(), Error> {
        let h = narrow(self.services.line(self.small)?)?;
        let rect = Rect {
            x: r.x + s.padding,
            y: r.y,
            w: r.w - 2 * s.padding,
            h,
        };
        text(a, rect, u32::from(i) * 4 + 4, g, self.small, 0, 0, 0, 1)?;
        a.push(Command {
            rect: Rect {
                y: r.y + h + 2,
                h: 1,
                ..rect
            },
            ..Command::default()
        })
    }
}
#[inline(never)]
pub fn view(a: &mut Arena<'_>, s: State, l: Layout, mut r: Services) -> Result<ListState, Error> {
    if s.kind > BROWSER
        || (s.mode > ERROR && s.mode != AUTH)
        || l.touch > 1
        || l.header_height < 0
        || l.spacing < 0
        || l.hints_height < 0
    {
        return Err(Error::Invalid);
    }
    let screen = l.list.screen;
    let mut style = l.list;
    let device = style.area;
    let settings = s.kind != BROWSER;
    let left = device.x.max(if settings { l.safe.x } else { 0 });
    let right = (i32::from(device.x) + i32::from(device.w)).min(if settings {
        i32::from(l.safe.x) + i32::from(l.safe.w)
    } else {
        i32::from(screen.w)
    });
    let top = (i32::from(l.top_padding)
        + i32::from(l.header_height)
        + if settings {
            i32::from(l.safe.y)
        } else {
            i32::from(l.spacing)
        })
    .max(i32::from(device.y));
    let bottom = (i32::from(screen.h)
        - i32::from(l.hints_height)
        - if settings {
            i32::from(screen.h) - i32::from(l.safe.y) - i32::from(l.safe.h)
        } else {
            0
        })
    .min(i32::from(device.y) + i32::from(device.h));
    let mut body = Rect {
        x: left,
        y: narrow(top)?,
        w: narrow(right - i32::from(left))?,
        h: narrow((bottom - top).max(0))?,
    };
    a.push(Command {
        kind: Kind::Clear as u8,
        rect: screen,
        paint: 1,
        ..Command::default()
    })?;
    resource(
        a,
        Rect {
            x: 0,
            y: l.top_padding,
            w: screen.w,
            h: l.header_height,
        },
        HEADER,
    )?;
    if s.kind == BROWSER && s.mode == BROWSING && l.search != 0 {
        a.hit(Interaction {
            rect: l.search_button,
            action: u32::from(SEARCH),
            generation: 1,
            inputs: 1,
            ..Interaction::default()
        })?;
    }
    let mut next = s.list;
    if settings {
        if s.kind == EDITOR {
            let h = l.tab_height.min(body.h);
            text(
                a,
                Rect {
                    x: body.x + l.side_padding,
                    w: body.w - 2 * l.side_padding,
                    h,
                    ..body
                },
                URL_HINT,
                1,
                l.small_font,
                0,
                0,
                0,
                1,
            )?;
            body.y += h;
            body.h -= h;
        }
        let gap = l.spacing.min(body.h);
        body.y += gap;
        body.h -= gap;
    }
    if s.kind == BROWSER && s.mode != BROWSING && s.mode != AUTH {
        status(a, s, l, &mut r, body)?;
    } else {
        if s.kind == BROWSER && l.pages != 0 {
            pagination(a, l, &mut r, &mut body)?;
        }
        if r.count == 0 {
            let h = narrow(r.line(style.font)?)?;
            label(
                a,
                Rect {
                    y: body.y + (body.h - h) / 2,
                    h,
                    ..body
                },
                EMPTY,
                style.font,
                1,
                style.bold,
            )?;
        } else {
            style.area = body;
            let padding = if l.touch != 0 { 8 } else { 4 };
            let body_line = r.line(style.font)?;
            if s.kind == EDITOR {
                style.font = l.small_font;
                style.bold = 0;
            }
            let line = r.line(style.font)?;
            style.min_touch = narrow((body_line + 14).max(44))?;
            let mut height = (line + 2 * padding).max(i32::from(style.row_height));
            if l.touch != 0 {
                height = height.max(56).max(i32::from(style.min_touch));
                style.row_gap = style.row_gap.max(6);
            }
            style.row_height = narrow(height)?;
            let count = r.count;
            let services = r;
            let content = Content::new(
                &mut r,
                count,
                l.small_font,
                narrow(padding)?,
                if s.kind == EDITOR { 2 } else { 1 },
                if s.kind == SERVERS { 0 } else { 8 },
            );
            let mut rows = GroupRows {
                content,
                services,
                small: l.small_font,
            };
            next = list::view_rows(a, s.list, style, count, 1, &mut rows)?;
        }
    }
    resource(a, screen, FOOTER)?;
    Ok(next)
}
#[inline(never)]
fn status(
    a: &mut Arena<'_>,
    s: State,
    l: Layout,
    r: &mut Services,
    body: Rect,
) -> Result<(), Error> {
    let line = narrow(r.line(l.list.font)?)?;
    if s.mode == ERROR {
        let count = if l.touch != 0 { 3 } else { 2 };
        let ids = [ERROR_TITLE, ERROR_TEXT, TAP_RETRY];
        let width = body.w - 2 * l.side_padding;
        let mut heights = [0i16; 3];
        let mut total = i32::from(l.space_md) * (count - 1) as i32;
        for (i, id) in ids.iter().enumerate().take(count) {
            heights[i] = narrow(
                r.height(*id, l.list.font, 0, 4, width)?
                    .max(i32::from(line)),
            )?;
            total += i32::from(heights[i]);
        }
        let mut y = i32::from(body.y) + (i32::from(body.h) - total).max(0) / 2;
        for (i, id) in ids.iter().enumerate().take(count) {
            label(
                a,
                Rect {
                    x: body.x + l.side_padding,
                    y: narrow(y)?,
                    w: width,
                    h: heights[i],
                },
                *id,
                l.list.font,
                4,
                0,
            )?;
            y += i32::from(heights[i]) + i32::from(l.space_md);
        }
        return Ok(());
    }
    if s.mode != DOWNLOADING {
        return label(
            a,
            Rect {
                y: body.y + (body.h - line) / 2,
                h: line,
                ..body
            },
            STATUS,
            l.list.font,
            1,
            l.list.bold,
        );
    }
    let progress_height = if s.total == 0 { line } else { 16 };
    let block = 3 * line + progress_height + l.button_height + 3 * l.space_md;
    let mut y = body.y + (body.h - block).max(0) / 2;
    label(
        a,
        Rect { y, h: line, ..body },
        DOWNLOADING_TEXT,
        l.list.font,
        1,
        l.list.bold,
    )?;
    y += line + l.space_md;
    label(
        a,
        Rect {
            x: body.x + l.side_padding,
            y,
            w: body.w - 2 * l.side_padding,
            h: 2 * line,
        },
        STATUS,
        l.list.font,
        2,
        l.list.bold,
    )?;
    y += 2 * line + l.space_md;
    let progress = Rect {
        x: body.x + 50,
        y,
        w: body.w - 100,
        h: progress_height,
    };
    if s.total == 0 {
        label(a, progress, BYTES, l.list.font, 1, l.list.bold)?;
    } else {
        a.push(Command {
            kind: Kind::Stroke as u8,
            rect: progress,
            width: 1,
            ..Command::default()
        })?;
        let max = s.total as i32;
        let value = (s.progress as i32).max(0).min(max.max(0));
        let width = if max > 0 {
            (i64::from(progress.w) * i64::from(value) / i64::from(max)) as i16
        } else {
            0
        };
        if width > 0 {
            a.push(Command {
                rect: Rect {
                    w: width,
                    ..progress
                },
                ..Command::default()
            })?;
        }
    }
    y += progress_height + l.space_md;
    let width = body.w / 3;
    let rect = Rect {
        x: body.x + (body.w - width) / 2,
        y,
        w: width,
        h: l.button_height,
    };
    a.push(Command {
        rect,
        paint: 1,
        radius: l.control_radius,
        ..Command::default()
    })?;
    a.push(Command {
        kind: Kind::Stroke as u8,
        rect,
        width: 1,
        radius: l.control_radius,
        ..Command::default()
    })?;
    label(
        a,
        Rect {
            x: rect.x + 4,
            y: rect.y + 2,
            w: rect.w - 8,
            h: rect.h - 4,
        },
        CANCEL_TEXT,
        l.list.font,
        1,
        l.list.bold,
    )?;
    a.hit(Interaction {
        rect: touch_rect(rect, l.list.screen, (line + 14).max(44)),
        action: u32::from(CANCEL_TRANSFER),
        generation: 1,
        inputs: 1,
        ..Interaction::default()
    })
}
fn page_icon(slot: Rect) -> Rect {
    let mut pill = Rect {
        x: slot.x + 4,
        y: slot.y + 4,
        w: slot.w - 8,
        h: slot.h - 12,
    };
    let wanted = 40.max(pill.h);
    if wanted < pill.w {
        pill.x += (pill.w - wanted) / 2;
        pill.w = wanted;
    }
    if pill.w <= 0 || pill.h <= 0 {
        pill = slot;
    }
    let mut content = Rect {
        x: pill.x + 4,
        y: pill.y + 4,
        w: pill.w - 8,
        h: pill.h - 8,
    };
    if content.w <= 0 || content.h <= 0 {
        content = pill;
    }
    Rect {
        x: content.x + (content.w - 32) / 2,
        y: content.y + (content.h - 32) / 2,
        w: 32,
        h: 32,
    }
}
#[inline(never)]
fn pagination(
    a: &mut Arena<'_>,
    l: Layout,
    r: &mut Services,
    body: &mut Rect,
) -> Result<(), Error> {
    let h = l.tab_height.min(body.h);
    body.h -= h;
    let y = body.y + body.h;
    a.push(Command {
        rect: Rect {
            x: 0,
            y,
            w: l.list.screen.w,
            h: 1,
        },
        ..Command::default()
    })?;
    let zone = (l.list.screen.w - 2 * l.side_padding) / 3;
    for right in [false, true] {
        let outer = if right {
            l.pages & 8 != 0
        } else {
            l.pages & 1 != 0
        };
        let count = if outer { 2 } else { 1 };
        let x = if right {
            l.list.screen.w - l.side_padding - zone
        } else {
            l.side_padding
        };
        for slot in 0..count {
            let index = if right {
                2 + slot
            } else {
                if outer {
                    slot
                } else {
                    1
                }
            };
            let width = zone / count;
            let rect = Rect {
                x: x + slot * width,
                y: y + 1,
                w: if slot == count - 1 {
                    zone - slot * width
                } else {
                    width
                },
                h: h - 1,
            };
            let enabled = l.pages & (1 << index) != 0;
            a.push(Command {
                kind: Kind::Bitmap as u8,
                rect: page_icon(rect),
                resource: ICON_FIRST + index as u32,
                generation: 1,
                paint: 0,
                style: u8::from(!enabled),
                ..Command::default()
            })?;
            if enabled {
                a.hit(Interaction {
                    rect: touch_rect(rect, l.list.screen, 44),
                    action: u32::from(PAGE),
                    value: i32::from(index),
                    generation: 1,
                    inputs: 1,
                    ..Interaction::default()
                })?;
            }
        }
    }
    if l.page_current > 0 && l.page_total > 0 {
        let line = narrow(r.line(l.small_font)?)?;
        label(
            a,
            Rect {
                x: l.side_padding + zone,
                y: y + 1 + (h - 1 - line) / 2,
                w: zone,
                h: line,
            },
            PAGE_TEXT,
            l.small_font,
            1,
            0,
        )?;
    }
    Ok(())
}
