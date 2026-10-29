use super::{Phase, State};
use crate::{
    list::{self, Rows, Style},
    model::ListState,
    scene::{Arena, Command, Error, Kind, Rect},
};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub list: Style,
    pub safe: Rect,
    pub header: Rect,
    pub small_font: i32,
    pub fixed_font: i32,
    pub header_bottom: i16,
    pub hints_height: i16,
    pub spacing: i16,
    pub space_xs: i16,
    pub space_sm: i16,
    pub space_md: i16,
    pub space_lg: i16,
    pub body_height: i16,
    pub small_height: i16,
    pub fixed_height: i16,
    pub touch: u8,
    pub has_device: u8,
    pub reserved: [u8; 2],
}
fn resource(a: &mut Arena<'_>, handle: u32, rect: Rect, font: i32) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::Page as u8,
        resource: handle,
        rect,
        font,
        generation: 1,
        ..Command::default()
    })
}
#[allow(clippy::too_many_arguments)]
fn text(
    a: &mut Arena<'_>,
    handle: u32,
    rect: Rect,
    font: i32,
    bold: u8,
    align: u8,
    lines: u8,
) -> Result<(), Error> {
    crate::settings::view::text(a, rect, handle, 1, font, 0, bold, align, lines)
}
struct Actions {
    icons: bool,
}
impl Rows for Actions {
    fn height(&mut self, _: u16, _: i16, style: Style) -> Result<i16, Error> {
        Ok(style.row_height)
    }
    fn partial(&self) -> bool {
        false
    }
    fn nav_managed(&self) -> bool {
        false
    }
    fn paint(
        &mut self,
        a: &mut Arena<'_>,
        index: u16,
        r: Rect,
        s: Style,
        fg: u8,
        generation: u32,
    ) -> Result<(), Error> {
        let mut content = Rect {
            x: r.x + s.padding,
            w: r.w - 2 * s.padding,
            ..r
        };
        if self.icons {
            a.push(Command {
                kind: Kind::Bitmap as u8,
                resource: 9 + u32::from(index),
                generation,
                rect: Rect {
                    x: content.x,
                    y: content.y + (content.h - 24) / 2,
                    w: 24,
                    h: 24,
                },
                paint: fg,
                ..Command::default()
            })?;
            content.x += 34;
            content.w -= 34;
        }
        a.push(Command {
            kind: Kind::TextHandle as u8,
            resource: if self.icons {
                66 + u32::from(index)
            } else {
                67
            },
            generation,
            rect: content,
            font: s.font,
            paint: fg,
            style: s.bold,
            max_lines: 1,
            ..Command::default()
        })
    }
}
pub fn view(a: &mut Arena<'_>, s: State, l: Layout) -> Result<(), Error> {
    let phase = Phase::from_wire(s.phase).ok_or(Error::Invalid)?;
    if s.kind > 2
        || l.fixed_height < 0
        || l.body_height < 0
        || l.small_height < 0
        || l.list.screen.w < 0
        || l.list.screen.h < 0
    {
        return Err(Error::Invalid);
    }
    let screen = l.list.screen;
    if !(1..=4096).contains(&screen.w)
        || !(1..=4096).contains(&screen.h)
        || [
            l.safe.x,
            l.safe.y,
            l.safe.w,
            l.safe.h,
            l.header.x,
            l.header.y,
            l.header.w,
            l.header.h,
            l.list.area.x,
            l.list.area.y,
            l.list.area.w,
            l.list.area.h,
        ]
        .iter()
        .any(|v| !(-4096..=4096).contains(v))
        || [
            l.body_height,
            l.small_height,
            l.fixed_height,
            l.header_bottom,
            l.hints_height,
            l.spacing,
            l.space_xs,
            l.space_sm,
            l.space_md,
            l.space_lg,
            l.list.inset,
            l.list.padding,
            l.list.row_gap,
        ]
        .iter()
        .any(|v| !(0..=512).contains(v))
        || !(0..=4096).contains(&l.list.row_height)
    {
        return Err(Error::Invalid);
    }

    a.push(Command {
        kind: Kind::Clear as u8,
        rect: screen,
        paint: 1,
        ..Command::default()
    })?;
    let header = if s.kind == 0 {
        if phase == Phase::Result {
            81
        } else {
            80
        }
    } else if s.kind == 2 {
        83
    } else {
        82
    };
    resource(a, 1, l.header, header)?;
    if s.kind != 0 {
        let top = (screen.h - l.fixed_height) / 2;
        let r = Rect {
            x: 0,
            y: top,
            w: screen.w,
            h: l.fixed_height,
        };
        match phase {
            Phase::Authenticating => text(a, 3, r, l.fixed_font, 0, 1, 0)?,
            Phase::Authenticated | Phase::Failed => {
                let title = if phase == Phase::Authenticated {
                    if s.kind == 2 {
                        76
                    } else {
                        75
                    }
                } else if s.kind == 2 {
                    79
                } else {
                    78
                };
                text(a, title, r, l.fixed_font, 1, 1, 0)?;
                text(
                    a,
                    if phase == Phase::Authenticated { 77 } else { 3 },
                    Rect {
                        y: top + l.fixed_height + 10,
                        ..r
                    },
                    l.fixed_font,
                    0,
                    1,
                    0,
                )?;
            }
            _ => {}
        }
        resource(a, 2, screen, 0)?;
        return Ok(());
    }
    let top = l.safe.y + l.safe.h / 2 - 40;
    let r = Rect {
        x: l.safe.x,
        y: top,
        w: l.safe.w,
        h: l.fixed_height,
    };
    match phase {
        Phase::NoCredentials => {
            text(a, 70, r, l.fixed_font, 1, 1, 0)?;
            text(a, 71, Rect { y: top + 40, ..r }, l.fixed_font, 1, 1, 0)?;
            resource(a, 2, screen, 0)?;
        }
        Phase::Syncing | Phase::Uploading => text(a, 3, r, l.fixed_font, 1, 1, 0)?,
        Phase::Uploaded | Phase::Synchronized => {
            text(
                a,
                if phase == Phase::Uploaded { 72 } else { 73 },
                r,
                l.fixed_font,
                1,
                1,
                0,
            )?;
            resource(a, 2, screen, 3)?;
        }
        Phase::Failed => {
            text(a, 74, r, l.fixed_font, 1, 1, 0)?;
            text(a, 3, Rect { y: top + 40, ..r }, l.fixed_font, 0, 1, 0)?;
            resource(a, 2, screen, 0)?;
        }
        Phase::Result | Phase::Missing => {
            let mut y = l.header_bottom.max(l.list.area.y) + l.spacing;
            let bottom = (screen.h - l.hints_height).min(l.list.area.y + l.list.area.h);
            let mut style = l.list;
            style.scroll_width = 0;
            let band_row = style.row_height;
            let band_gap = style.row_gap;
            if l.touch != 0 {
                style.row_height = (l.body_height + 16)
                    .max(56)
                    .max(l.body_height + 14)
                    .max(style.min_touch);
                style.row_gap = style.row_gap.max(6);
            }
            let result = phase == Phase::Result;
            let area = Rect {
                x: l.list.area.x,
                y,
                w: l.list.area.w,
                h: (bottom - y).max(0),
            };
            if result {
                let indent = style.inset + style.padding;
                for (handle, label) in [
                    (64, true),
                    (4, false),
                    (5, false),
                    (6, false),
                    (65, true),
                    (7, false),
                    (8, false),
                ] {
                    if handle == 6 && l.has_device == 0 {
                        continue;
                    }
                    if handle == 65 {
                        y += l.space_lg;
                    }
                    let left = indent + if label { 0 } else { l.space_md };
                    let height = if label { l.body_height } else { l.small_height };
                    text(
                        a,
                        handle,
                        Rect {
                            x: area.x + left,
                            y,
                            w: (area.w - left).max(0),
                            h: height,
                        },
                        if label { style.font } else { l.small_font },
                        u8::from(label),
                        0,
                        1,
                    )?;
                    y += height + if label { l.space_sm } else { l.space_xs };
                }
                y += l.space_md;
                style.area = Rect {
                    y,
                    h: (2 * band_row + band_gap + l.space_sm).min((bottom - y).max(0)),
                    ..area
                };
            } else {
                text(
                    a,
                    68,
                    Rect {
                        y,
                        h: l.body_height,
                        ..area
                    },
                    style.font,
                    1,
                    1,
                    1,
                )?;
                y += l.body_height + l.space_sm;
                text(
                    a,
                    69,
                    Rect {
                        y,
                        h: l.body_height,
                        ..area
                    },
                    style.font,
                    style.bold,
                    1,
                    1,
                )?;
                let band = band_row + l.space_md;
                style.area = Rect {
                    y: (bottom - band).max(y + l.body_height + l.space_md),
                    h: band,
                    ..area
                };
            }
            let mut rows = Actions { icons: result };
            let mut state = ListState::new(if result { i32::from(s.selected) } else { 0 });
            state.follow = 0;
            state.active = i32::from(s.touch_armed) - 1;
            list::view_rows(a, state, style, if result { 2 } else { 1 }, 1, &mut rows)?;
            resource(a, 2, screen, if result { 1 } else { 2 })?;
        }
        _ => {}
    }
    Ok(())
}
