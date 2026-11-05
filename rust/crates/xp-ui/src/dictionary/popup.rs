use super::view::MESSAGE;
use crate::{
    model::Metrics,
    scene::{Arena, Command, Error, Kind, Rect},
};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Lookup {
    pub since: u32,
    pub message: u16,
    pub pending: u8,
    pub popup: u8,
}
pub const LOOKING: u16 = 0;
pub const INDEXING: u16 = 1;
pub const NOT_FOUND: u16 = 2;
pub const LOW_MEMORY: u16 = 3;
pub const READ_FAILED: u16 = 4;
pub const DECOMPRESS: u16 = 5;
pub const ERROR: u16 = 6;
impl Lookup {
    pub fn new() -> Self {
        Self {
            message: LOOKING,
            pending: 1,
            popup: 1,
            ..Self::default()
        }
    }
    pub fn result(&mut self, opened: bool, index: u8, result: u8, found: bool, now: u32) {
        if found {
            self.popup = 0;
            return;
        }
        self.since = now;
        self.popup = 3;
        self.message = if !opened {
            match index {
                1 => LOW_MEMORY,
                2 => READ_FAILED,
                _ => ERROR,
            }
        } else {
            match result {
                2 => LOW_MEMORY,
                3 => DECOMPRESS,
                4 => READ_FAILED,
                _ => {
                    self.popup = 2;
                    NOT_FOUND
                }
            }
        };
    }
    pub fn expired(&self, now: u32) -> bool {
        self.popup >= 2 && now.wrapping_sub(self.since) >= 1500
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub font: i32,
    pub margin_x: i16,
    pub margin_y: i16,
    pub frame: i16,
    pub radius: i16,
    pub text_y: i16,
    pub top_ratio: f32,
    pub bold: u8,
    pub inverted: u8,
    pub reserved: [u8; 2],
}
pub fn view(
    layout: &Layout,
    text: &[u8],
    metrics: &mut dyn Metrics,
    arena: &mut Arena<'_>,
) -> Result<(), Error> {
    let width = metrics.width(layout.font, text, layout.bold);
    let height = metrics.line_height(layout.font);
    if width < 0 || height <= 0 || !layout.top_ratio.is_finite() {
        return Err(Error::Invalid);
    }
    let w = width + i32::from(layout.margin_x) * 2;
    let h = height + i32::from(layout.margin_y) * 2;
    let x = (i32::from(layout.screen.w) - w) / 2;
    let y = (f32::from(layout.screen.h) * layout.top_ratio) as i32;
    let frame = i32::from(layout.frame);
    let rounded = layout.radius > 0;
    for (x, y, w, h, radius, paint) in [
        (
            x - frame,
            y - frame,
            w + 2 * frame,
            h + 2 * frame,
            if rounded {
                layout.radius + layout.frame
            } else {
                0
            },
            u8::from(rounded),
        ),
        (x, y, w, h, layout.radius, u8::from(!rounded)),
    ] {
        let rect = Rect {
            x: i16::try_from(x).map_err(|_| Error::Invalid)?,
            y: i16::try_from(y).map_err(|_| Error::Invalid)?,
            w: i16::try_from(w).map_err(|_| Error::Invalid)?,
            h: i16::try_from(h).map_err(|_| Error::Invalid)?,
        };
        arena.push(Command {
            kind: Kind::Fill as u8,
            rect,
            radius: radius as u8,
            paint,
            ..Command::default()
        })?;
    }
    arena.push(Command {
        kind: Kind::TextHandle as u8,
        resource: MESSAGE,
        generation: 1,
        rect: Rect {
            x: (x + (w - width) / 2) as i16,
            y: (y + i32::from(layout.margin_y) + i32::from(layout.text_y)) as i16,
            w: width as i16,
            h: height as i16,
        },
        font: layout.font,
        style: layout.bold,
        paint: u8::from(layout.inverted == 0),
        ..Command::default()
    })
}
