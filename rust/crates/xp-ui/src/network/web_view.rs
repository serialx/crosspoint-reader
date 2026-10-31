use super::view::resource;
use super::{
    usb_view::{FOOTER, HEADER},
    web::{Phase, State},
};
use crate::scene::{Arena, Command, Error, Kind, Rect, Refresh};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub font: i32,
    pub small_font: i32,
    pub top_padding: i16,
    pub header_height: i16,
    pub tab_height: i16,
    pub spacing: i16,
    pub side_padding: i16,
    pub line_height: i16,
    pub wifi_connected: u8,
    pub reserved: [u8; 3],
}
pub const SUBHEADER: u32 = 1300;
pub const STARTING: u32 = 1301;
pub const CONNECT_HINT: u32 = 1302;
pub const WIFI_QR: u32 = 1303;
pub const SSID: u32 = 1304;
pub const OPEN_HINT: u32 = 1305;
pub const SCAN_HINT: u32 = 1306;
pub const URL_QR: u32 = 1307;
pub const PRIMARY_URL: u32 = 1308;
pub const ALTERNATE_URL: u32 = 1309;
fn text(
    a: &mut Arena<'_>,
    l: &Layout,
    handle: u32,
    at: (i16, i16),
    bold: bool,
    small: bool,
    center: bool,
) -> Result<(), Error> {
    let (x, y) = at;
    a.push(Command {
        kind: Kind::TextHandle as u8,
        rect: Rect {
            x,
            y,
            w: l.screen.w,
            h: l.line_height,
        },
        resource: handle,
        generation: 1,
        font: if small { l.small_font } else { l.font },
        style: u8::from(bold),
        align: u8::from(center),
        ..Command::default()
    })?;
    Ok(())
}
fn wifi(a: &mut Arena<'_>, s: &State, l: &Layout) -> Result<(), Error> {
    let right = l.screen.w - l.side_padding;
    let bottom = l.top_padding + l.header_height + l.tab_height - l.spacing;
    if l.wifi_connected != 0 && s.disconnects == 0 {
        for i in 0..4 {
            let height = (i + 1) * 14 / 4;
            a.push(Command {
                kind: if i32::from(i) < s.bars {
                    Kind::Fill
                } else {
                    Kind::Stroke
                } as u8,
                rect: Rect {
                    x: right - 22 + i * 6,
                    y: bottom - height,
                    w: 4,
                    h: height,
                },
                width: 1,
                ..Command::default()
            })?;
        }
    } else {
        for (y, delta) in [(bottom - 14, 14), (bottom, -14)] {
            a.push(Command {
                kind: Kind::Line as u8,
                rect: Rect {
                    x: right - 14,
                    y,
                    w: 14,
                    h: delta,
                },
                generation: 1,
                width: 2,
                ..Command::default()
            })?;
        }
    }
    Ok(())
}
pub fn view(a: &mut Arena<'_>, s: &State, l: &Layout) -> Result<Refresh, Error> {
    if s.phase > 4
        || !(1..=8192).contains(&l.screen.w)
        || !(1..=8192).contains(&l.screen.h)
        || [
            l.top_padding,
            l.header_height,
            l.tab_height,
            l.spacing,
            l.side_padding,
            l.line_height,
        ]
        .iter()
        .any(|n| !(0..=1024).contains(n))
    {
        return Err(Error::Invalid);
    }
    if s.phase != Phase::Running as u8 && s.phase != Phase::ApStarting as u8 {
        return Ok(Refresh::None);
    }
    a.push(Command {
        kind: Kind::Clear as u8,
        paint: 1,
        ..Command::default()
    })?;
    let header = Rect {
        x: 0,
        y: l.top_padding,
        w: l.screen.w,
        h: l.header_height,
    };
    let sub = Rect {
        x: 0,
        y: l.top_padding + l.header_height,
        w: l.screen.w,
        h: l.tab_height,
    };
    resource(a, HEADER, header)?;
    if s.phase == Phase::ApStarting as u8 {
        text(
            a,
            l,
            STARTING,
            (0, (l.screen.h - l.line_height) / 2),
            false,
            false,
            true,
        )?;
        return Ok(Refresh::Fast);
    }
    resource(a, SUBHEADER, sub)?;
    // The original running renderer draws both chrome rows again.
    resource(a, HEADER, header)?;
    resource(a, SUBHEADER, sub)?;
    if s.is_ap == 0 {
        wifi(a, s, l)?;
    }
    let mut y = l.top_padding + l.header_height + l.tab_height + l.spacing * 2;
    if s.is_ap != 0 {
        text(a, l, CONNECT_HINT, (l.side_padding, y), true, false, false)?;
        y += l.line_height + l.spacing * 2;
        resource(
            a,
            WIFI_QR,
            Rect {
                x: l.side_padding,
                y,
                w: 198,
                h: 198,
            },
        )?;
        text(
            a,
            l,
            SSID,
            (l.side_padding + 198 + l.spacing, y + 80),
            false,
            false,
            false,
        )?;
        y += 198 + l.spacing * 2;
        text(a, l, OPEN_HINT, (l.side_padding, y), true, false, false)?;
        y += l.line_height + l.spacing * 2;
        resource(
            a,
            URL_QR,
            Rect {
                x: l.side_padding,
                y,
                w: 198,
                h: 198,
            },
        )?;
        text(
            a,
            l,
            PRIMARY_URL,
            (l.side_padding + 198 + l.spacing, y + 80),
            false,
            false,
            false,
        )?;
        text(
            a,
            l,
            ALTERNATE_URL,
            (l.side_padding + 198 + l.spacing, y + 100),
            false,
            true,
            false,
        )?;
    } else {
        y += l.spacing * 2;
        text(a, l, OPEN_HINT, (0, y), true, false, true)?;
        y += l.line_height;
        text(a, l, SCAN_HINT, (0, y), true, false, true)?;
        y += l.line_height + l.spacing * 2;
        resource(
            a,
            URL_QR,
            Rect {
                x: (l.screen.w - 198) / 2,
                y,
                w: 198,
                h: 198,
            },
        )?;
        y += 198 + l.spacing * 2;
        text(a, l, PRIMARY_URL, (0, y), false, false, true)?;
        y += l.line_height + 5;
        text(a, l, ALTERNATE_URL, (0, y), false, true, true)?;
    }
    resource(a, FOOTER, l.screen)?;
    Ok(Refresh::Fast)
}
const _: () = {
    assert!(core::mem::size_of::<Layout>() == 32);
    assert!(core::mem::align_of::<Layout>() == 4);
};

const _: () = {
    assert!(SUBHEADER == 1300);
    assert!(STARTING == 1301);
    assert!(CONNECT_HINT == 1302);
    assert!(WIFI_QR == 1303);
    assert!(SSID == 1304);
    assert!(OPEN_HINT == 1305);
    assert!(SCAN_HINT == 1306);
    assert!(URL_QR == 1307);
    assert!(PRIMARY_URL == 1308);
    assert!(ALTERNATE_URL == 1309);
    assert!(core::mem::offset_of!(Layout, screen) == 0);
    assert!(core::mem::offset_of!(Layout, font) == 8);
    assert!(core::mem::offset_of!(Layout, small_font) == 12);
    assert!(core::mem::offset_of!(Layout, top_padding) == 16);
    assert!(core::mem::offset_of!(Layout, header_height) == 18);
    assert!(core::mem::offset_of!(Layout, tab_height) == 20);
    assert!(core::mem::offset_of!(Layout, spacing) == 22);
    assert!(core::mem::offset_of!(Layout, side_padding) == 24);
    assert!(core::mem::offset_of!(Layout, line_height) == 26);
    assert!(core::mem::offset_of!(Layout, wifi_connected) == 28);
    assert!(core::mem::offset_of!(Layout, reserved) == 29);
};
