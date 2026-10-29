use super::rows::Resources;
use crate::{
    reader_chrome::list::text,
    scene::{Arena, Command, Error, Interaction, Kind, Rect},
};
pub const LABEL: u32 = 0x8000_0200;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub safe: Rect,
    pub body_font: i32,
    pub small_font: i32,
    pub height: i16,
    pub gap: i16,
    pub side: i16,
    pub divider: i16,
    pub spacing: i16,
    pub full: u8,
    pub touch: u8,
    pub radius: u8,
    pub bold: u8,
    pub reserved: [u8; 2],
}
#[inline(never)]
fn rect(x: i32, y: i32, w: i32, h: i32) -> Result<Rect, Error> {
    let cv = |n| i16::try_from(n).map_err(|_| Error::Invalid);
    Ok(Rect {
        x: cv(x)?,
        y: cv(y)?,
        w: cv(w)?,
        h: cv(h)?,
    })
}
#[inline(never)]
fn fill(a: &mut Arena<'_>, r: Rect, p: u8, radius: u8) -> Result<(), Error> {
    a.push(Command {
        rect: r,
        paint: p,
        radius,
        corners: 15,
        ..Command::default()
    })
}
pub fn view(
    a: &mut Arena<'_>,
    area: Rect,
    l: Layout,
    active: u8,
    focused: bool,
    descending: bool,
    t: &mut dyn Resources,
) -> Result<Rect, Error> {
    if active >= 3 || l.height < 0 || l.gap < 0 || l.side < 0 {
        return Err(Error::Invalid);
    }
    let font = if l.full != 0 {
        l.body_font
    } else {
        l.small_font
    };
    let bold = if l.full != 0 { l.bold } else { 0 };
    let line = t.line(font)?;
    let band_h = (if l.touch != 0 {
        50
    } else {
        i32::from(l.height)
    })
    .max(line + 10)
    .min(i32::from(area.h));
    let band = rect(
        i32::from(area.x) - i32::from(l.safe.x),
        i32::from(area.y),
        i32::from(area.w) + i32::from(l.screen.w) - i32::from(l.safe.w),
        band_h,
    )?;
    if focused && l.full == 0 {
        fill(a, band, 2, 0)?;
    }
    let divider = i32::from(l.divider.max(1));
    let slots = rect(
        i32::from(area.x) + i32::from(l.side),
        i32::from(area.y),
        i32::from(area.w) - 2 * i32::from(l.side),
        band_h - divider,
    )?;
    let gap = if l.full != 0 { 0 } else { i32::from(l.gap) };
    let sw = (i32::from(slots.w) - 2 * gap) / 3;
    for index in 0..3 {
        let x = i32::from(slots.x) + index * (sw + gap);
        let w = if index == 2 {
            i32::from(slots.x) + i32::from(slots.w) - x
        } else {
            sw
        };
        let slot = rect(x, i32::from(slots.y), w, i32::from(slots.h))?;
        let top = if l.full != 0 { 4 } else { 2 };
        let bottom = if l.full != 0 {
            7
        } else if focused {
            4
        } else {
            0
        };
        let label = t.width(LABEL + index as u32, font, bold)?;
        let indicator = if index == i32::from(active) { 10 } else { 0 };
        let ph = (i32::from(slot.h) - top - bottom).max(0);
        let pw = (w - 8).max(0);
        let wanted = (label + indicator + 32).max(ph);
        let pw = pw.min(wanted);
        let mut pill = rect(
            x + 4 + ((w - 8).max(0) - pw) / 2,
            i32::from(slot.y) + top,
            pw,
            ph,
        )?;
        if pill.w <= 0 || pill.h <= 0 {
            pill = slot;
        }
        // Library tabs have three unequal natural label widths, so retain only these control hits.
        let geometry = super::grid::Geometry {
            screen: l.screen,
            hero: slot,
            books: 1,
            min_touch: 44,
            ..Default::default()
        };
        a.hit(Interaction {
            rect: geometry.hit_rect(0).ok_or(Error::Invalid)?,
            action: 3,
            value: index,
            generation: 1,
            inputs: 1,
            flags: 0,
        })?;
        let selected = index == i32::from(active);
        let fg = u8::from(selected && (focused || l.full != 0));
        if selected {
            fill(
                a,
                pill,
                if focused {
                    0
                } else if l.full != 0 {
                    3
                } else {
                    2
                },
                if focused || l.full != 0 { l.radius } else { 0 },
            )?;
        }
        if selected && !focused && l.full == 0 {
            fill(
                a,
                rect(
                    i32::from(pill.x),
                    i32::from(pill.y) + i32::from(pill.h) - 2,
                    i32::from(pill.w),
                    2,
                )?,
                0,
                0,
            )?;
        }
        let mut content = rect(
            i32::from(pill.x) + 16,
            i32::from(pill.y) + 2,
            (i32::from(pill.w) - 32).max(0),
            (i32::from(pill.h) - 4).max(0),
        )?;
        if content.w <= 0 || content.h <= 0 {
            content = pill;
        }
        if selected {
            let label_w = label.min((i32::from(content.w) - 10).max(0));
            let row_w = label_w + 10;
            let row_x = i32::from(content.x) + (i32::from(content.w) - row_w) / 2;
            if label_w > 0 {
                text(
                    a,
                    rect(row_x, i32::from(content.y), label_w, i32::from(content.h))?,
                    LABEL + index as u32,
                    1,
                    font,
                    fg,
                    bold,
                    1,
                    1,
                )?;
            }
            // The shared scene triangle points right; these two resources bind the existing up/down primitive.
            a.push(Command {
                kind: Kind::Page as u8,
                resource: if descending { 0x8000_0204 } else { 0x8000_0203 },
                generation: 1,
                rect: rect(
                    row_x + label_w + 4,
                    i32::from(content.y) + i32::from(content.h) / 2 - 3,
                    6,
                    6,
                )?,
                paint: fg,
                ..Command::default()
            })?;
        } else {
            text(a, content, LABEL + index as u32, 1, font, fg, bold, 1, 1)?;
        }
    }
    fill(
        a,
        rect(
            i32::from(band.x),
            i32::from(band.y) + band_h - divider,
            i32::from(band.w),
            divider,
        )?,
        0,
        0,
    )?;
    let consumed = band_h + i32::from(l.spacing);
    rect(
        i32::from(area.x),
        i32::from(area.y) + consumed,
        i32::from(area.w),
        (i32::from(area.h) - consumed).max(0),
    )
}
