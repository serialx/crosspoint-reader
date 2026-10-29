//! File and library rows borrow their strings from the existing storage windows.
use crate::reader_chrome::list::text;
use crate::{
    list::{self, Style},
    scene::{Arena, Command, Error, Kind, Rect},
};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Row {
    pub label: u32,
    pub subtitle: u32,
    pub value: u32,
    pub icon: u32,
    pub heading: u32,
    pub icon_size: i16,
    pub reserved: u16,
}
pub trait Resources {
    fn position(&mut self, _: i32, _: u16, _: bool) -> Result<(), Error> {
        Err(Error::Invalid)
    }
    fn window(&mut self, first: u16, requested: u16) -> Result<(), Error>;
    fn row(&mut self, index: u16) -> Result<Row, Error>;
    fn width(&mut self, h: u32, f: i32, b: u8) -> Result<i32, Error>;
    fn height(&mut self, h: u32, f: i32, b: u8, n: u8, w: i16) -> Result<i32, Error>;
    fn line(&mut self, f: i32) -> Result<i32, Error>;
}
#[derive(Default)]
struct Measured {
    row: Row,
    label_width: i16,
    label_height: i16,
    subtitle_height: i16,
    value_width: i16,
    height: i16,
    heading_padding: i16,
}
pub struct Content<'a> {
    pub resources: &'a mut dyn Resources,
    pub count: u16,
    pub capacity: u16,
    pub file: bool,
    pub small_font: i32,
    pub value_font: i32,
    pub padding_y: i16,
    pub revealed: i32,
    pub reveal_width: i16,
    first: Option<u16>,
    measured: Measured,
}
impl<'a> Content<'a> {
    pub fn new(
        resources: &'a mut dyn Resources,
        count: u16,
        capacity: u16,
        file: bool,
        small_font: i32,
        value_font: i32,
        padding_y: i16,
    ) -> Self {
        Self {
            resources,
            count,
            capacity,
            file,
            small_font,
            value_font,
            padding_y,
            revealed: -1,
            reveal_width: 0,
            first: None,
            measured: Measured::default(),
        }
    }
}
fn cv(n: i32) -> Result<i16, Error> {
    i16::try_from(n).map_err(|_| Error::Invalid)
}
impl list::Rows for Content<'_> {
    fn available(&self, index: u16) -> bool {
        self.first
            .is_none_or(|f| index < f.saturating_add(self.capacity))
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
        let line = self.resources.line(style.font)?;
        let icon = if row.icon != 0 {
            i32::from(row.icon_size)
        } else {
            0
        };
        if icon < 0 {
            return Err(Error::Invalid);
        }
        let content =
            i32::from(width) - 2 * i32::from(style.padding) - if icon != 0 { icon + 10 } else { 0 };
        let value_width = if row.value != 0 {
            self.resources.width(row.value, self.value_font, 0)?
        } else {
            0
        };
        let label_width = (content
            - if row.value != 0 {
                value_width + if self.file { 8 } else { 0 } + 10
            } else {
                0
            })
        .max(0);
        let label_height = if self.file && label_width > 0 {
            self.resources
                .height(row.label, style.font, style.bold, 2, cv(label_width)?)?
                .max(line)
        } else {
            line
        };
        let sub = if row.subtitle != 0 {
            self.resources.line(self.small_font)?
        } else {
            0
        };
        let value_h = if row.value != 0 {
            self.resources.line(self.value_font)?
        } else {
            0
        };
        let band = label_height.max(value_h);
        let text_h = band + sub;
        let min = i32::from(style.row_height);
        let needed = if self.padding_y >= 0 {
            text_h.max(icon) + 2 * i32::from(self.padding_y)
        } else if row.subtitle != 0 {
            text_h + (min - line - sub).max(0)
        } else if label_height > min {
            min + label_height - line
        } else {
            min
        };
        self.measured = Measured {
            row,
            label_width: cv(label_width)?,
            label_height: cv(band)?,
            subtitle_height: cv(sub)?,
            value_width: cv(value_width)?,
            height: cv(needed.max(min).max(text_h).max(icon))?,
            heading_padding: 0,
        };
        Ok(self.measured.height)
    }
    fn heading_height(&mut self, index: u16, _: i16, style: Style) -> Result<(i16, i16), Error> {
        if self.measured.row.heading == 0 {
            return Ok((0, 0));
        }
        let pad = if self.first == Some(index) { 0 } else { 16 };
        self.measured.heading_padding = pad;
        Ok((
            cv(i32::from(pad)
                + self.resources.line(self.small_font)?
                + 4
                + i32::from(style.row_gap))?,
            pad,
        ))
    }
    fn paint_heading(
        &mut self,
        a: &mut Arena<'_>,
        _: u16,
        r: Rect,
        s: Style,
        g: u32,
    ) -> Result<(), Error> {
        let h = cv(self.resources.line(self.small_font)?)?;
        text(
            a,
            Rect {
                x: r.x + s.padding,
                y: r.y + self.measured.heading_padding,
                w: r.w - 2 * s.padding,
                h,
            },
            self.measured.row.heading,
            g,
            self.small_font,
            0,
            0,
            0,
            1,
        )
    }
    fn paint(
        &mut self,
        a: &mut Arena<'_>,
        _: u16,
        r: Rect,
        s: Style,
        fg: u8,
        g: u32,
    ) -> Result<(), Error> {
        let m = &self.measured;
        let row = m.row;
        let mut content = Rect {
            x: r.x + s.padding,
            w: r.w - 2 * s.padding,
            ..r
        };
        let mut band = content;
        if row.subtitle != 0 {
            band.y += ((r.h - m.label_height - m.subtitle_height) / 2).max(0);
            band.h = m.label_height;
        }
        if row.icon != 0 {
            let size = row.icon_size;
            a.push(Command {
                kind: Kind::Bitmap as u8,
                rect: Rect {
                    x: content.x,
                    y: content.y + (content.h - size) / 2,
                    w: size,
                    h: size,
                },
                resource: row.icon,
                generation: g,
                paint: fg,
                ..Command::default()
            })?;
            content.x += size + 10;
            content.w -= size + 10;
            band.x = content.x;
            band.w = content.w;
        }
        if row.value != 0 {
            text(
                a,
                Rect {
                    x: cv(i32::from(band.x) + i32::from(band.w)
                        - i32::from(m.value_width)
                        - if self.file { 8 } else { 0 })?,
                    w: m.value_width,
                    ..band
                },
                row.value,
                g,
                self.value_font,
                fg,
                0,
                2,
                1,
            )?;
        }
        text(
            a,
            Rect {
                w: m.label_width,
                ..band
            },
            row.label,
            g,
            s.font,
            fg,
            s.bold,
            0,
            if self.file { 2 } else { 1 },
        )?;
        if row.subtitle != 0 {
            text(
                a,
                Rect {
                    x: content.x,
                    y: band.y + band.h,
                    w: content.w,
                    h: m.subtitle_height,
                },
                row.subtitle,
                g,
                self.small_font,
                fg,
                0,
                0,
                1,
            )?;
        }
        Ok(())
    }
    fn overlay(
        &mut self,
        a: &mut Arena<'_>,
        index: u16,
        r: Rect,
        s: Style,
        g: u32,
    ) -> Result<(), Error> {
        if self.revealed != i32::from(index) {
            return Ok(());
        }
        let width = self.reveal_width.min(r.w / 2);
        let hit = Rect {
            x: r.x + r.w - width,
            w: width,
            ..r
        };
        let pad = s.padding / 2;
        let button = Rect {
            x: hit.x + pad,
            y: hit.y + pad,
            w: hit.w - 2 * pad,
            h: hit.h - 2 * pad,
        };
        a.hit(crate::scene::Interaction {
            rect: hit,
            action: 2,
            value: i32::from(index),
            generation: g,
            inputs: 1,
            flags: 0,
        })?;
        a.push(Command {
            rect: button,
            paint: 1,
            radius: s.radius,
            ..Command::default()
        })?;
        a.push(Command {
            rect: button,
            kind: Kind::Stroke as u8,
            width: 1,
            radius: s.radius,
            ..Command::default()
        })?;
        a.push(Command {
            rect: Rect {
                x: button.x + (button.w - 24) / 2,
                y: button.y + (button.h - 24) / 2,
                w: 24,
                h: 24,
            },
            kind: Kind::Bitmap as u8,
            resource: 0x8000_0008,
            generation: g,
            ..Command::default()
        })
    }
}
