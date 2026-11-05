use crate::scene::{Arena, Command, Error, Kind, Rect};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub title_font: i32,
    pub small_font: i32,
    pub error_font: i32,
    pub side_padding: i16,
    pub top_padding: i16,
    pub header_height: i16,
    pub spacing: i16,
    pub line_height: i16,
    pub view: u8,
    pub refresh: u8,
    pub flags: u16,
    pub description_lines: u8,
    pub panic_lines: u8,
}
pub const LOGO: u32 = 1;
pub const MOON: u32 = 2;
pub const HEADER: u32 = 3;
pub const HINTS: u32 = 4;
pub const VERSION: u32 = 5;
pub const TITLE: u32 = 6;
pub const SUBTITLE: u32 = 7;
pub const REASON: u32 = 8;
pub const ERROR: u32 = 9;
pub const DESCRIPTION: u32 = 16;
pub const PANIC: u32 = 32;
pub const XTC_PIXELS: u32 = 12;
pub const XTC_PAGE: u8 = 8;
struct View<'a, 'b> {
    arena: &'a mut Arena<'b>,
    layout: &'a Layout,
    width: &'a mut dyn FnMut(u32, i32, u8) -> i32,
}
impl View<'_, '_> {
    fn resource(&mut self, id: u32, rect: Rect) -> Result<(), Error> {
        self.arena.push(Command {
            kind: Kind::Bitmap as u8,
            resource: id,
            generation: 1,
            rect,
            ..Command::default()
        })
    }
    fn text(&mut self, id: u32, font: i32, x: i16, y: i16, style: u8) -> Result<(), Error> {
        self.arena.push(Command {
            kind: Kind::TextHandle as u8,
            resource: id,
            generation: 1,
            font,
            style,
            rect: Rect {
                x,
                y,
                w: self.layout.screen.w,
                h: self.layout.line_height.max(1),
            },
            ..Command::default()
        })
    }
    fn centered(&mut self, id: u32, font: i32, y: i16, style: u8) -> Result<(), Error> {
        let width = (self.width)(id, font, style);
        if !(0..=32767).contains(&width) {
            return Err(Error::Invalid);
        }
        self.text(
            id,
            font,
            ((i32::from(self.layout.screen.w) - width) / 2) as i16,
            y,
            style,
        )
    }
}
pub fn view(
    arena: &mut Arena<'_>,
    l: &Layout,
    width: &mut dyn FnMut(u32, i32, u8) -> i32,
) -> Result<(), Error> {
    if l.screen.x != 0
        || l.screen.y != 0
        || !(1..=8192).contains(&l.screen.w)
        || !(1..=8192).contains(&l.screen.h)
        || l.view > XTC_PAGE
        || l.flags > 7
        || l.refresh > 3
        || l.description_lines > 10
        || l.panic_lines > 5
        || [
            l.side_padding,
            l.top_padding,
            l.header_height,
            l.spacing,
            l.line_height,
        ]
        .iter()
        .any(|&v| !(0..=1024).contains(&v))
    {
        return Err(Error::Invalid);
    }
    let mut v = View {
        arena,
        layout: l,
        width,
    };
    if l.view != 3 && l.view != 7 && l.view != XTC_PAGE && l.flags & 4 == 0 {
        v.arena.push(Command {
            kind: Kind::Clear as u8,
            rect: l.screen,
            ..Command::default()
        })?;
    }
    match l.view {
        0 | 1 => {
            v.resource(
                LOGO,
                Rect {
                    x: (l.screen.w - 120) / 2,
                    y: (l.screen.h - 120) / 2,
                    w: 120,
                    h: 120,
                },
            )?;
            v.centered(TITLE, l.title_font, l.screen.h / 2 + 70, 1)?;
            v.centered(SUBTITLE, l.small_font, l.screen.h / 2 + 95, 0)?;
            if l.view == 0 {
                v.centered(VERSION, l.small_font, l.screen.h - 30, 0)?;
            }
        }
        2 => {}
        3 => v.resource(
            MOON,
            Rect {
                x: 0,
                y: l.screen.h - 48,
                w: 48,
                h: 48,
            },
        )?,
        4 => {
            v.resource(
                HEADER,
                Rect {
                    x: 0,
                    y: l.top_padding,
                    w: l.screen.w,
                    h: l.header_height,
                },
            )?;
            let mut y = l.top_padding + l.header_height + l.spacing;
            for n in 0..l.description_lines {
                v.text(
                    DESCRIPTION + u32::from(n),
                    l.title_font,
                    l.side_padding,
                    y,
                    0,
                )?;
                y += l.line_height;
            }
            y += l.spacing * 2;
            v.text(REASON, l.title_font, l.side_padding, y, 0)?;
            y += l.line_height + l.spacing;
            for n in 0..l.panic_lines {
                v.text(PANIC + u32::from(n), l.title_font, l.side_padding, y, 0)?;
                y += l.line_height;
            }
            v.resource(HINTS, l.screen)?;
        }
        5 => {
            v.centered(ERROR, l.title_font, l.screen.h / 2, 0)?;
            if l.flags & 1 == 0 {
                v.resource(if l.flags & 2 != 0 { 11 } else { HINTS }, l.screen)?;
            }
        }
        6 => v.centered(ERROR, l.error_font, 300, 1)?,
        7 => v.resource(10, l.screen)?,
        XTC_PAGE => v.resource(XTC_PIXELS, l.screen)?,
        _ => return Err(Error::Invalid),
    }
    Ok(())
}
