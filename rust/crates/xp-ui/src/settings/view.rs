use super::protocol;
use crate::{
    list::{self, Style},
    model::ListState,
    scene::{Arena, Command, Error, Kind, Rect},
};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Row {
    pub label: u32,
    pub subtitle: u32,
    pub value: u32,
    pub icon: u32,
    pub toggle: u8,
    pub checked: u8,
    pub enabled: u8,
    pub reserved: u8,
}
pub trait Resources {
    fn window(&mut self, first: u16, requested: u16) -> Result<(), Error>;
    fn row(&mut self, index: u16) -> Result<Row, Error>;
    fn width(&mut self, handle: u32, font: i32, bold: u8) -> Result<i32, Error>;
    fn height(
        &mut self,
        handle: u32,
        font: i32,
        bold: u8,
        lines: u8,
        width: i16,
    ) -> Result<i32, Error>;
    fn line(&mut self, font: i32) -> Result<i32, Error>;
}
#[derive(Clone, Copy, Default)]
struct Measured {
    row: Row,
    label_width: i16,
    label_height: i16,
    subtitle_height: i16,
    value_width: i16,
    height: i16,
}
pub struct Content<'a> {
    pub resources: &'a mut dyn Resources,
    pub count: u16,
    pub capacity: u16,
    pub small_font: i32,
    pub value_font: i32,
    pub padding_y: i16,
    pub lines: u8,
    pub value_inset: i16,
    pub disabled: u32,
    pub managed: bool,
    first: Option<u16>,
    measured: Measured,
}
impl<'a> Content<'a> {
    pub fn new(
        resources: &'a mut dyn Resources,
        count: u16,
        small_font: i32,
        padding_y: i16,
        lines: u8,
        value_inset: i16,
    ) -> Self {
        Self {
            resources,
            count,
            capacity: 64,
            small_font,
            value_font: small_font,
            padding_y,
            lines,
            value_inset,
            disabled: 0,
            managed: true,
            first: None,
            measured: Measured::default(),
        }
    }
}
fn i16_value(v: i32) -> Result<i16, Error> {
    i16::try_from(v).map_err(|_| Error::Invalid)
}
#[allow(clippy::too_many_arguments)]
pub fn text(
    arena: &mut Arena<'_>,
    rect: Rect,
    resource: u32,
    generation: u32,
    font: i32,
    paint: u8,
    bold: u8,
    align: u8,
    lines: u8,
) -> Result<(), Error> {
    if resource == 0 {
        return Ok(());
    }
    arena.push(Command {
        kind: Kind::TextHandle as u8,
        rect,
        resource,
        generation,
        font,
        paint,
        style: bold,
        align,
        max_lines: lines,
        ..Command::default()
    })
}

impl list::Rows for Content<'_> {
    fn partial(&self) -> bool {
        self.managed
    }
    fn nav_managed(&self) -> bool {
        self.managed
    }
    fn height(&mut self, index: u16, width: i16, style: Style) -> Result<i16, Error> {
        if self.capacity == 0 {
            return Err(Error::Invalid);
        }
        if self.first.is_none() {
            self.resources
                .window(index, self.capacity.min(self.count - index))?;
            self.first = Some(index);
        }
        let row = self.resources.row(index)?;
        if row.enabled == 0 {
            let position = u32::from(index.saturating_sub(self.first.unwrap_or(index)));
            self.disabled |= 1u32.checked_shl(position).ok_or(Error::Capacity)?;
        }
        let line = self.resources.line(style.font)?;
        let icon = if row.icon != 0 { 32 } else { 0 };
        let content_width =
            i32::from(width) - 2 * i32::from(style.padding) - if icon != 0 { icon + 10 } else { 0 };
        let value_width = if row.toggle != 0 {
            28
        } else if row.value != 0 {
            self.resources.width(row.value, self.value_font, 0)?
        } else {
            0
        };
        let has_value = row.toggle != 0 || row.value != 0;
        let mut label_width = content_width
            - if has_value {
                value_width + i32::from(self.value_inset) + 10
            } else {
                0
            };
        let lines = self.lines;
        if has_value
            && lines > 1
            && self.resources.width(row.label, style.font, style.bold)? > label_width
        {
            label_width = label_width.min(content_width * 3 / 5);
        }
        label_width = label_width.max(0);
        let label_height = if lines > 1 && label_width > 0 {
            self.resources
                .height(
                    row.label,
                    style.font,
                    style.bold,
                    lines,
                    i16_value(label_width)?,
                )?
                .max(line)
        } else {
            line
        };
        let subtitle_height = if row.subtitle != 0 {
            self.resources.line(self.small_font)?
        } else {
            0
        };
        let value_height = if row.toggle != 0 {
            28
        } else if row.value != 0 {
            self.resources.line(self.value_font)?
        } else {
            0
        };
        let band_height = label_height.max(value_height);
        let text_height = band_height + subtitle_height;
        let row_min = i32::from(style.row_height);
        let needed = if self.padding_y >= 0 {
            text_height.max(icon) + 2 * i32::from(self.padding_y)
        } else if row.subtitle != 0 {
            text_height + (row_min - line - subtitle_height).max(0)
        } else if label_height > row_min {
            row_min + label_height - line
        } else {
            row_min
        };
        let height = needed.max(row_min).max(text_height).max(icon);
        self.measured = Measured {
            row,
            label_width: i16_value(label_width)?,
            label_height: i16_value(band_height)?,
            subtitle_height: i16_value(subtitle_height)?,
            value_width: i16_value(value_width)?,
            height: i16_value(height)?,
        };
        Ok(self.measured.height)
    }
    fn paint(
        &mut self,
        arena: &mut Arena<'_>,
        _: u16,
        rect: Rect,
        style: Style,
        fg: u8,
        generation: u32,
    ) -> Result<(), Error> {
        let m = self.measured;
        let row = m.row;
        let fg = if row.enabled == 0 {
            arena.push(Command {
                rect,
                paint: 1,
                radius: style.radius,
                ..Command::default()
            })?;
            0
        } else {
            fg
        };
        let mut content = Rect {
            x: rect.x + style.padding,
            w: rect.w - 2 * style.padding,
            ..rect
        };
        let mut band = content;
        if row.subtitle != 0 {
            band.y += ((content.h - m.label_height - m.subtitle_height) / 2).max(0);
            band.h = m.label_height;
        }
        if row.icon != 0 {
            arena.push(Command {
                kind: Kind::Bitmap as u8,
                rect: Rect {
                    x: content.x,
                    y: content.y + (content.h - 32) / 2,
                    w: 32,
                    h: 32,
                },
                resource: row.icon,
                generation,
                paint: fg,
                ..Command::default()
            })?;
            content.x += 42;
            content.w -= 42;
            band.x = content.x;
            band.w = content.w;
        }
        if row.toggle != 0 {
            let box_rect = Rect {
                x: band.x + band.w - self.value_inset - 25,
                y: band.y + (band.h - 22) / 2,
                w: 22,
                h: 22,
            };
            arena.push(Command {
                kind: Kind::Stroke as u8,
                rect: box_rect,
                paint: fg,
                width: 2,
                radius: 2,
                ..Command::default()
            })?;
            if row.checked != 0 {
                arena.push(Command {
                    rect: Rect {
                        x: box_rect.x + 5,
                        y: box_rect.y + 5,
                        w: 12,
                        h: 12,
                    },
                    paint: fg,
                    radius: 2,
                    ..Command::default()
                })?;
            }
        } else if row.value != 0 {
            text(
                arena,
                Rect {
                    x: i16_value(
                        i32::from(band.x) + i32::from(band.w)
                            - i32::from(m.value_width)
                            - i32::from(self.value_inset),
                    )?,
                    w: m.value_width,
                    ..band
                },
                row.value,
                generation,
                self.value_font,
                fg,
                0,
                2,
                1,
            )?;
        }
        text(
            arena,
            Rect {
                w: m.label_width,
                ..band
            },
            row.label,
            generation,
            style.font,
            fg,
            style.bold,
            0,
            self.lines,
        )?;
        if row.subtitle != 0 {
            text(
                arena,
                Rect {
                    x: content.x,
                    y: band.y + band.h,
                    w: content.w,
                    h: m.subtitle_height,
                },
                row.subtitle,
                generation,
                self.small_font,
                fg,
                0,
                0,
                1,
            )?;
        }
        Ok(())
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub list: Style,
    pub safe: Rect,
    pub small_font: i32,
    pub fixed_font: i32,
    pub help_font: i32,
    pub top_padding: i16,
    pub header_height: i16,
    pub hints_height: i16,
    pub spacing: i16,
    pub tab_height: i16,
    pub preview_height: i16,
    pub side_padding: i16,
    pub kind: u8,
    pub touch: u8,
    pub flags: u8,
    pub reserved: u8,
}

pub const HEADER: u32 = 0x8000_0001;
pub const FOOTER: u32 = 0x8000_0002;
pub const PREVIEW: u32 = 0x8000_0003;
pub fn view(
    arena: &mut Arena<'_>,
    state: ListState,
    l: Layout,
    resources: &mut dyn Resources,
    count: u16,
) -> Result<(ListState, u32), Error> {
    if l.kind > protocol::TIMEZONE
        || l.touch > 1
        || l.header_height < 0
        || l.spacing < 0
        || l.hints_height < 0
    {
        return Err(Error::Invalid);
    }
    if l.kind == protocol::SYNC {
        return sync_view(arena, state, l, resources);
    }
    let mut style = l.list;
    let screen = style.screen;
    let device_safe = style.area;
    let mut top = i32::from(l.top_padding) + i32::from(l.header_height);
    let mut left = i32::from(device_safe.x);
    let mut right = i32::from(device_safe.x) + i32::from(device_safe.w);
    let mut bottom = (i32::from(screen.h) - i32::from(l.hints_height))
        .min(i32::from(device_safe.y) + i32::from(device_safe.h));
    let mut lines = 2;
    let mut inset = 8;
    let mut padding = if l.touch != 0 { 8 } else { 4 };
    let body_font = style.font;
    if l.kind == protocol::ROOT || l.kind == protocol::BLE || l.kind == protocol::LAYOUTS {
        lines = 1;
        inset = 0;
    } else if l.kind == protocol::ABOUT || l.kind == protocol::TIMEZONE {
        // An unmodified smallText style inherits bodyText in the SDK.
        lines = 1;
    } else if l.kind != protocol::SECTION
        || l.touch == 0
        || l.flags & protocol::SMALL_LABELS_ON_TOUCH != 0
    {
        style.font = l.small_font;
        style.bold = 0;
    }
    if l.kind == protocol::LAYOUTS {
        style.font = body_font;
    }
    if l.kind == protocol::HOME_BUTTON {
        inset = 0;
    }
    if l.kind == protocol::LANGUAGE {
        top += i32::from(l.safe.y);
        left = left.max(i32::from(l.safe.x));
        right = right.min(i32::from(l.safe.x) + i32::from(l.safe.w));
        bottom = (i32::from(l.safe.y) + i32::from(l.safe.h))
            .min(i32::from(device_safe.y) + i32::from(device_safe.h));
        inset = 0;
    }
    if l.kind == protocol::REMAP {
        top = i32::from(l.safe.y)
            + i32::from(l.top_padding)
            + i32::from(l.header_height)
            + i32::from(l.tab_height);
        left = left.max(i32::from(l.safe.x) + i32::from(l.spacing));
        right = right.min(i32::from(l.safe.x) + i32::from(l.safe.w) - i32::from(l.spacing));
        bottom = (i32::from(l.safe.y) + i32::from(l.safe.h) - i32::from(l.spacing))
            .min(i32::from(device_safe.y) + i32::from(device_safe.h));
        style.scroll_width = 0;
        inset = 0;
    }
    if l.kind == protocol::STATUS_BAR {
        bottom -= i32::from(l.preview_height) + 40 + i32::from(l.spacing);
    }
    top = top.max(i32::from(device_safe.y)) + i32::from(l.spacing);
    style.area = Rect {
        x: i16_value(left)?,
        y: i16_value(top)?,
        w: i16_value(right - left)?,
        h: i16_value((bottom - top).max(0))?,
    };
    let label_line = resources.line(style.font)?;
    let body_line = resources.line(body_font)?;
    style.min_touch = i16_value((body_line + 14).max(44))?;
    let mut row_height = (label_line + 2 * padding).max(i32::from(style.row_height));
    if l.touch != 0 {
        row_height = row_height.max(i32::from(style.min_touch)).max(56);
        style.row_gap = style.row_gap.max(6);
    }
    if l.kind == protocol::ROOT {
        row_height = (body_line + resources.line(l.small_font)?).max(32) + 2 * padding;
        padding = -1;
    }
    style.row_height = i16_value(row_height)?;
    arena.push(Command {
        kind: Kind::Clear as u8,
        rect: screen,
        paint: 1,
        ..Command::default()
    })?;
    arena.push(Command {
        kind: Kind::Page as u8,
        rect: Rect {
            x: 0,
            y: l.top_padding,
            w: screen.w,
            h: l.header_height,
        },
        resource: HEADER,
        generation: 1,
        ..Command::default()
    })?;
    let mut content = Content::new(
        resources,
        count,
        l.small_font,
        i16_value(padding)?,
        lines,
        inset,
    );
    if l.kind == protocol::REMAP {
        content.managed = false;
        arena.push(Command {
            kind: Kind::Page as u8,
            rect: Rect {
                x: 0,
                y: l.top_padding + l.header_height,
                w: screen.w,
                h: l.tab_height,
            },
            resource: 0x8000_0004,
            generation: 1,
            ..Command::default()
        })?;
    }
    let next = list::view_rows(arena, state, style, count, 1, &mut content)?;
    let disabled = content.disabled;
    if l.kind == protocol::REMAP {
        let offset = l.top_padding + l.header_height + l.tab_height + l.spacing;
        if l.flags & 1 != 0 {
            arena.push(Command {
                kind: Kind::Page as u8,
                rect: Rect {
                    x: 0,
                    y: screen.h - l.hints_height - l.side_padding - 15,
                    w: screen.w,
                    h: 20,
                },
                resource: 0x8000_0005,
                generation: 1,
                ..Command::default()
            })?;
        }
        for (extra, handle) in [(0, 0x8000_0006), (l.spacing + 20, 0x8000_0007)] {
            arena.push(Command {
                kind: Kind::Page as u8,
                rect: Rect {
                    x: 0,
                    y: offset + 4 * l.list.row_height + 4 * l.spacing + extra,
                    w: screen.w,
                    h: 20,
                },
                resource: handle,
                generation: 1,
                ..Command::default()
            })?;
        }
    }

    arena.push(Command {
        kind: Kind::Page as u8,
        rect: screen,
        resource: FOOTER,
        generation: 1,
        ..Command::default()
    })?;
    if l.kind == protocol::STATUS_BAR {
        arena.push(Command {
            kind: Kind::Page as u8,
            rect: screen,
            resource: PREVIEW,
            generation: 1,
            ..Command::default()
        })?;
    }
    Ok((next, disabled))
}

fn sync_view(
    arena: &mut Arena<'_>,
    state: ListState,
    l: Layout,
    resources: &mut dyn Resources,
) -> Result<(ListState, u32), Error> {
    let screen = l.list.screen;
    if !(0..4).contains(&state.selected) {
        return Err(Error::Invalid);
    }
    let row = resources.row(state.selected as u16)?;
    arena.push(Command {
        kind: Kind::Clear as u8,
        rect: screen,
        paint: 1,
        ..Command::default()
    })?;
    arena.push(Command {
        kind: Kind::Page as u8,
        rect: Rect {
            x: 0,
            y: l.top_padding,
            w: screen.w,
            h: l.header_height,
        },
        resource: HEADER,
        generation: 1,
        ..Command::default()
    })?;
    let bold = u8::from(state.selected != 0);
    let y = screen.h / 2 - if state.selected == 0 { 0 } else { 20 };
    center(arena, resources, screen.w, row.label, l.fixed_font, y, bold)?;
    if state.selected != 0 {
        if row.value != 0 {
            center(
                arena,
                resources,
                screen.w,
                row.value,
                l.help_font,
                screen.h / 2 + 10,
                0,
            )?;
        }
        arena.push(Command {
            kind: Kind::Page as u8,
            rect: screen,
            resource: FOOTER,
            generation: 1,
            ..Command::default()
        })?;
    }
    Ok((state, 0))
}
fn center(
    arena: &mut Arena<'_>,
    resources: &mut dyn Resources,
    width: i16,
    handle: u32,
    font: i32,
    y: i16,
    bold: u8,
) -> Result<(), Error> {
    let w = resources.width(handle, font, bold)?;
    let h = resources.line(font)?;
    text(
        arena,
        Rect {
            x: i16_value((i32::from(width) - w) / 2)?,
            y,
            w: i16_value(w)?,
            h: i16_value(h)?,
        },
        handle,
        1,
        font,
        0,
        bold,
        0,
        0,
    )
}
