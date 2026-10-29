use super::{
    calibre::{Phase, State},
    usb_view::{FOOTER, HEADER},
};
use crate::scene::{Arena, Command, Error, Kind, Rect};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub font10: i32,
    pub font12: i32,
    pub small_font: i32,
    pub top_padding: i16,
    pub header_height: i16,
    pub tab_height: i16,
    pub spacing: i16,
    pub side_padding: i16,
    pub progress_height: i16,
    pub line10: i16,
    pub text_height12: i16,
    pub banner_width: i16,
    pub reserved: u16,
}
pub const SUBHEADER: u32 = 1200;
pub const PROGRESS: u32 = 1201;
pub const STARTING: u32 = 1202;
pub const ERROR: u32 = 1203;
pub const SETUP: u32 = 1204;
pub const INSTRUCTION1: u32 = 1205;
pub const INSTRUCTION2: u32 = 1206;
pub const INSTRUCTION3: u32 = 1207;
pub const INSTRUCTION4: u32 = 1208;
pub const STATUS: u32 = 1209;
pub const RECEIVING: u32 = 1210;
pub const COMPLETED: u32 = 1211;
fn resource(a: &mut Arena<'_>, handle: u32, rect: Rect) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::Bitmap as u8,
        rect,
        resource: handle,
        generation: 1,
        ..Command::default()
    })?;
    Ok(())
}
fn text(
    a: &mut Arena<'_>,
    l: &Layout,
    handle: u32,
    x: i16,
    y: i16,
    font: i32,
    bold: bool,
) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::TextHandle as u8,
        rect: Rect {
            x,
            y,
            w: l.screen.w,
            h: l.line10,
        },
        resource: handle,
        generation: 1,
        font,
        style: u8::from(bold),
        ..Command::default()
    })?;
    Ok(())
}
pub fn view(a: &mut Arena<'_>, s: &State, l: &Layout, now: u32) -> Result<(), Error> {
    if !(1..=8192).contains(&l.screen.w)
        || !(1..=8192).contains(&l.screen.h)
        || s.phase > 3
        || [
            l.top_padding,
            l.header_height,
            l.tab_height,
            l.spacing,
            l.side_padding,
            l.progress_height,
            l.line10,
            l.text_height12,
        ]
        .iter()
        .any(|n| !(0..=1024).contains(n))
        || l.banner_width < 0
    {
        return Err(Error::Invalid);
    }
    a.push(Command {
        kind: Kind::Clear as u8,
        paint: 1,
        ..Command::default()
    })?;
    resource(
        a,
        HEADER,
        Rect {
            x: 0,
            y: l.top_padding,
            w: l.screen.w,
            h: l.header_height,
        },
    )?;
    let top = (l.screen.h - l.line10) / 2;
    match s.phase {
        p if p == Phase::Starting as u8 => text(
            a,
            l,
            STARTING,
            (l.screen.w - l.banner_width) / 2,
            top,
            l.font12,
            false,
        )?,
        p if p == Phase::Error as u8 => text(
            a,
            l,
            ERROR,
            (l.screen.w - l.banner_width) / 2,
            top,
            l.font12,
            true,
        )?,
        p if p == Phase::Running as u8 => {
            resource(
                a,
                SUBHEADER,
                Rect {
                    x: 0,
                    y: l.top_padding + l.header_height,
                    w: l.screen.w,
                    h: l.tab_height,
                },
            )?;
            let mut y = l.top_padding + l.header_height + l.tab_height + l.spacing * 4;
            text(a, l, SETUP, l.side_padding, y, l.font12, true)?;
            y += l.text_height12 + l.spacing * 2;
            for (i, handle) in [INSTRUCTION1, INSTRUCTION2, INSTRUCTION3, INSTRUCTION4]
                .into_iter()
                .enumerate()
            {
                text(
                    a,
                    l,
                    handle,
                    l.side_padding,
                    y + l.line10 * i as i16,
                    l.small_font,
                    false,
                )?;
            }
            y += l.line10 * 3 + l.spacing * 4;
            text(a, l, STATUS, l.side_padding, y, l.font12, true)?;
            y += l.text_height12 + l.spacing * 2;
            if s.total > 0 && s.received <= s.total {
                text(a, l, RECEIVING, l.side_padding, y, l.small_font, false)?;
                resource(
                    a,
                    PROGRESS,
                    Rect {
                        x: l.side_padding,
                        y: y + l.line10 + l.spacing,
                        w: (l.screen.w - l.side_padding * 2).max(0),
                        h: l.progress_height,
                    },
                )?;
                y += l.line10 + l.spacing * 2 + l.progress_height;
            }
            if s.completed_at > 0 && now.wrapping_sub(s.completed_at) < 6000 {
                text(a, l, COMPLETED, l.side_padding, y, l.small_font, false)?;
            }
            resource(a, FOOTER, l.screen)?;
        }
        _ => {}
    }
    Ok(())
}
const _: () = {
    assert!(SUBHEADER == 1200);
    assert!(PROGRESS == 1201);
    assert!(STARTING == 1202);
    assert!(ERROR == 1203);
    assert!(SETUP == 1204);
    assert!(INSTRUCTION1 == 1205);
    assert!(INSTRUCTION2 == 1206);
    assert!(INSTRUCTION3 == 1207);
    assert!(INSTRUCTION4 == 1208);
    assert!(STATUS == 1209);
    assert!(RECEIVING == 1210);
    assert!(COMPLETED == 1211);
    assert!(core::mem::offset_of!(Layout, screen) == 0);
    assert!(core::mem::offset_of!(Layout, font10) == 8);
    assert!(core::mem::offset_of!(Layout, font12) == 12);
    assert!(core::mem::offset_of!(Layout, small_font) == 16);
    assert!(core::mem::offset_of!(Layout, top_padding) == 20);
    assert!(core::mem::offset_of!(Layout, header_height) == 22);
    assert!(core::mem::offset_of!(Layout, tab_height) == 24);
    assert!(core::mem::offset_of!(Layout, spacing) == 26);
    assert!(core::mem::offset_of!(Layout, side_padding) == 28);
    assert!(core::mem::offset_of!(Layout, progress_height) == 30);
    assert!(core::mem::offset_of!(Layout, line10) == 32);
    assert!(core::mem::offset_of!(Layout, text_height12) == 34);
    assert!(core::mem::offset_of!(Layout, banner_width) == 36);
    assert!(core::mem::offset_of!(Layout, reserved) == 38);
    assert!(core::mem::size_of::<Layout>() == 40);
    assert!(core::mem::align_of::<Layout>() == 4);
    assert!(core::mem::offset_of!(Layout, font10) == 8);
    assert!(core::mem::offset_of!(Layout, top_padding) == 20);
    assert!(core::mem::offset_of!(Layout, reserved) == 38);
    assert!(SUBHEADER > HEADER && SUBHEADER > FOOTER && COMPLETED < 1300);
};
