//! Streaming glyph placements. Bitmap ownership and rasterization stay with the caller.
use super::{
    apply_ligatures, glyph_metrics, scaled, to_pixel, tracking_between, FontFace, Glyph,
    SMALL_CAPS, SUB, SUP,
};
use crate::{
    bidi,
    text::{combining, small_caps_upper},
};

#[derive(Clone, Copy)]
#[repr(C)]
pub struct Placement {
    pub codepoint: u32,
    pub glyph: *const crate::font::raw::Glyph,
    pub x: i32,
    pub y: i32,
    pub scale_num: u8,
    pub scale_den: u8,
}

pub fn walk(
    face: &mut impl FontFace,
    mut text: impl Iterator<Item = u32> + Clone,
    style: u8,
    tracking: i8,
    rotated: bool,
    emit: &mut impl FnMut(Placement),
) {
    let mut cursor = 0i32;
    let mut base = Glyph::default();
    let mut previous = 0;
    let mut advance = 0;
    while let Some(cp) = text.next() {
        if cp == 0 {
            break;
        }
        if cp >= 0x300 && (combining(cp) || bidi::is_transparent_mark(cp)) {
            let Some(mark) = face.glyph(cp, style) else {
                continue;
            };
            let shift = match cp {
                0x05b9 | 0x05c2 => 0,
                0x05c1 => i32::from(base.width) - i32::from(mark.width),
                _ => i32::from(base.width) / 2 - i32::from(mark.width) / 2,
            };
            let native = matches!(cp, 0x05bc | 0x05ba | 0x05c1 | 0x05b9 | 0x05c2);
            let bottom = i32::from(mark.top) - i32::from(mark.height);
            let raise = if !native && bottom > 0 {
                (1 - (bottom - i32::from(base.top))).max(0)
            } else {
                0
            };
            let along = cursor + i32::from(base.left) + shift - i32::from(mark.left);
            emit(Placement {
                codepoint: cp,
                glyph: face.glyph_pointer(cp),
                x: if rotated { -raise } else { along },
                y: if rotated { -along } else { -raise },
                scale_num: 1,
                scale_den: 1,
            });
            continue;
        }
        let upper = if !rotated && style & SMALL_CAPS != 0 {
            small_caps_upper(cp)
        } else {
            cp
        };
        let mapped = if !rotated && style & SMALL_CAPS != 0 {
            upper
        } else {
            apply_ligatures(face, cp, &mut text, style)
        };
        if previous != 0 {
            let kern = i32::from(face.kerning(previous, mapped, style));
            cursor += to_pixel(advance + kern)
                + if rotated {
                    0
                } else {
                    tracking_between(previous, mapped, tracking)
                };
        }
        base = if matches!(mapped, 0x2588 | 0x25a0) {
            glyph_metrics(face, mapped, style)
        } else {
            face.glyph(mapped, style)
        }
        .unwrap_or_default();
        let (num, den) = if !rotated && style & (SUP | SUB) != 0 {
            (1, 2)
        } else if upper != cp {
            (3, 4)
        } else {
            (1, 1)
        };
        advance = if rotated {
            i32::from(base.advance)
        } else {
            scaled(i32::from(base.advance), style, upper != cp)
        };
        emit(Placement {
            codepoint: mapped,
            glyph: face.glyph_pointer(mapped),
            x: if rotated { 0 } else { cursor },
            y: if rotated { -cursor } else { 0 },
            scale_num: num,
            scale_den: den,
        });
        previous = mapped;
    }
}
