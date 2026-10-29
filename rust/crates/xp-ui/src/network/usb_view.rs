use super::usb::{Phase, State};
use crate::scene::{Arena, Command, Error, Kind, Rect};
use core::ffi::c_void;
pub const HEADER: u32 = 1000;
pub const FOOTER: u32 = 1001;
pub const PREPARING: u32 = 1100;
pub const EJECT: u32 = 1101;
pub const WAITING: u32 = 1102;
pub const CONNECTED: u32 = 1103;
pub const DELAY: u32 = 1104;
pub const START_ERROR: u32 = 1105;
pub const ERROR: u32 = 1106;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub safe: Rect,
    pub small_font: i32,
    pub top_padding: i16,
    pub header_height: i16,
    pub hints_height: i16,
    pub side_padding: i16,
    pub space_md: i16,
    pub reserved: u16,
}
#[repr(C)]
pub struct TextServices {
    pub context: *mut c_void,
    pub height: Option<unsafe extern "C" fn(*mut c_void, u32, i32, i16, u8, u8) -> i16>,
}
fn resource(arena: &mut Arena<'_>, handle: u32, rect: Rect) -> Result<(), Error> {
    arena.push(Command {
        kind: Kind::Bitmap as u8,
        rect,
        resource: handle,
        generation: 1,
        ..Command::default()
    })
}
fn text(
    arena: &mut Arena<'_>,
    handle: u32,
    rect: Rect,
    font: i32,
    bold: u8,
    lines: u8,
) -> Result<(), Error> {
    arena.push(Command {
        kind: Kind::TextHandle as u8,
        rect,
        resource: handle,
        generation: 1,
        font,
        style: bold,
        max_lines: lines,
        align: 1,
        ..Command::default()
    })
}
pub fn view(arena: &mut Arena<'_>, s: &State, l: &Layout, t: &TextServices) -> Result<(), Error> {
    if !(1..=8192).contains(&l.screen.w)
        || !(1..=8192).contains(&l.screen.h)
        || l.side_padding < 0
        || !(0..=1024).contains(&l.space_md)
        || !(0..=1024).contains(&l.side_padding)
        || !(0..=1024).contains(&l.top_padding)
        || !(0..=1024).contains(&l.header_height)
        || !(0..=1024).contains(&l.hints_height)
        || l.safe.x < 0
        || l.safe.y < 0
        || l.safe.w < 0
        || l.safe.h < 0
        || i32::from(l.safe.x) + i32::from(l.safe.w) > i32::from(l.screen.w)
        || i32::from(l.safe.y) + i32::from(l.safe.h) > i32::from(l.screen.h)
        || s.phase > 5
    {
        return Err(Error::Invalid);
    }
    arena.push(Command {
        kind: Kind::Clear as u8,
        paint: 1,
        ..Command::default()
    })?;
    resource(
        arena,
        HEADER,
        Rect {
            x: 0,
            y: l.top_padding,
            w: l.screen.w,
            h: l.header_height,
        },
    )?;
    let (message, detail, secondary) = if s.preparing != 0 {
        (PREPARING, EJECT, 0)
    } else {
        match s.phase {
            1 => (WAITING, 0, 0),
            2 => (CONNECTED, DELAY, EJECT),
            5 => (
                if s.start_failed != 0 {
                    START_ERROR
                } else {
                    ERROR
                },
                0,
                0,
            ),
            _ => (0, 0, 0),
        }
    };
    if message != 0 {
        let left = l.side_padding.max(l.safe.x);
        let top = (l.top_padding + l.header_height).max(l.safe.y);
        let right = l.side_padding.max(l.screen.w - l.safe.x - l.safe.w);
        let bottom = l.hints_height.max(l.screen.h - l.safe.y - l.safe.h);
        let mut body = Rect {
            x: left,
            y: top,
            w: (l.screen.w - left - right).max(0),
            h: (l.screen.h - top - bottom).max(0),
        };
        if body.w <= 0 || body.h < 0 {
            return Err(Error::Invalid);
        }
        let measure = t.height.ok_or(Error::Invalid)?;
        let mut heights = [0i16; 3];
        for (i, handle) in [message, detail, secondary].into_iter().enumerate() {
            if handle != 0 {
                let h = unsafe {
                    measure(
                        t.context,
                        handle,
                        l.small_font,
                        body.w,
                        u8::from(i == 0),
                        if i == 0 { 2 } else { 3 },
                    )
                };
                if !(0..=4096).contains(&h) {
                    return Err(Error::Invalid);
                }
                heights[i] = h;
            }
        }
        let total: i32 = heights.iter().map(|x| i32::from(*x)).sum::<i32>()
            + i32::from(l.space_md) * (i32::from(detail != 0) + i32::from(secondary != 0));
        if i32::from(body.h) > total {
            let space = (i32::from(body.h) - total) / 2;
            body.y += space as i16;
            body.h -= space as i16;
        }
        for (i, handle) in [message, detail, secondary].into_iter().enumerate() {
            if handle == 0 {
                continue;
            }
            let h = heights[i].min(body.h);
            let r = Rect { h, ..body };
            text(
                arena,
                handle,
                r,
                l.small_font,
                u8::from(i == 0),
                if i == 0 { 2 } else { 3 },
            )?;
            let gap = if i == 0 && detail != 0 || i == 1 && secondary != 0 {
                l.space_md
            } else {
                0
            };
            let used = h + gap;
            body.y += used;
            body.h = (body.h - used).max(0);
        }
    }
    if s.phase == Phase::Waiting as u8 || s.start_failed != 0 {
        resource(arena, FOOTER, l.screen)?;
    }
    Ok(())
}

const _: () = {
    assert!(HEADER == 1000 && FOOTER == 1001);
    assert!(PREPARING == 1100);
    assert!(EJECT == 1101);
    assert!(WAITING == 1102);
    assert!(CONNECTED == 1103);
    assert!(DELAY == 1104);
    assert!(START_ERROR == 1105);
    assert!(ERROR == 1106);
    assert!(core::mem::size_of::<Layout>() == 32);
    assert!(core::mem::align_of::<Layout>() == 4);
    assert!(core::mem::offset_of!(Layout, small_font) == 16);
    assert!(core::mem::offset_of!(Layout, reserved) == 30);
    assert!(core::mem::size_of::<TextServices>() == 2 * core::mem::size_of::<usize>());
};
