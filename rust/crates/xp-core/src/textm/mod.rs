//! Allocation-free measurement of already-shaped, visual codepoints.
//!
//! Resolve the font on logical text before bidi/shaping. The caller owns both
//! streams and any SD prewarming; no measurement function retains a borrow.
use crate::text::{combining, small_caps_upper};

pub const SUP: u8 = 16;
pub const SUB: u8 = 32;
pub const SMALL_CAPS: u8 = 128;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(C)]
pub struct Glyph {
    pub advance: u16,
    pub width: u8,
    pub height: u8,
    pub left: i16,
    pub top: i16,
}

/// One registered font family; style bits retain EpdFontFamily's encoding.
/// All returned metrics are copied, never pointers into an SD overflow ring.
pub trait FontFace {
    /// Optional bitmap metadata for the most recent glyph query. The pointer
    /// expires at the next face/owner operation; consumers must finish synchronously.
    fn glyph_pointer(&self, _cp: u32) -> *const crate::font::raw::Glyph {
        core::ptr::null()
    }

    /// EpdFont::getGlyph, including replacement/turned-comma fallback. May do
    /// SD I/O, evict overflow glyphs, or rasterize/cache a TTF glyph.
    fn glyph(&mut self, cp: u32, style: u8) -> Option<Glyph>;
    /// Coverage only: no glyph loading, replacement, or storage I/O.
    fn has_codepoint(&self, cp: u32, style: u8) -> bool;
    /// Signed 4.4 kerning. A TTF callback may mutate its own cache.
    fn kerning(&mut self, left: u32, right: u32, style: u8) -> i8;
    /// Pair lookup, zero for no substitution; no I/O. The provider preserves
    /// EpdFont's Arabic presentation-form guard.
    fn ligature(&self, left: u32, right: u32, style: u8) -> u32;
    /// RAM-only SD advance-table presence, independent of requested style.
    fn has_advance_table(&self) -> bool;
    /// RAM-only SD style fallback; decorations must not change the variant.
    fn resolve_style(&self, style: u8) -> u8;
    /// RAM-only SD lookup. Zero means missing OR a real zero advance.
    fn advance(&self, cp: u32, resolved_style: u8) -> u16;
    /// Font metadata only, no I/O. Ascender is style-specific for solid glyphs.
    fn ascender(&self, style: u8) -> i32;
    /// The regular face's advanceY, no I/O.
    fn line_height(&self) -> u8;
}

/// Registration and coverage are read-only and never trigger glyph loads.
pub trait FontRegistry {
    fn fallback_id(&self, id: i32) -> Option<i32>;
    fn contains(&self, id: i32) -> bool;
    fn has_codepoint(&self, id: i32, cp: u32, style: u8) -> bool;
}

pub fn resolve_font_id(registry: &impl FontRegistry, id: i32, logical: &[u32], style: u8) -> i32 {
    if logical.is_empty() {
        return id;
    }
    resolve_stream(registry, id, logical.iter().copied(), style)
}
pub fn resolve_stream(
    registry: &impl FontRegistry,
    id: i32,
    logical: impl Iterator<Item = u32>,
    style: u8,
) -> i32 {
    let Some(fallback) = registry.fallback_id(id) else {
        return id;
    };
    if !registry.contains(id) || !registry.contains(fallback) {
        return id;
    }
    for cp in logical {
        if cp == 0 {
            break;
        }
        if cp >= 0x80
            && !registry.has_codepoint(id, cp, style)
            && registry.has_codepoint(fallback, cp, style)
        {
            return fallback;
        }
    }
    id
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum MeasureMode {
    Layout,
    Render,
}

pub fn to_pixel(fp: i32) -> i32 {
    fp.wrapping_add(8) >> 4
}
pub fn tracking_between(left: u32, right: u32, tracking: i8) -> i32 {
    if left == 0 || matches!(left, 32 | 0xa0 | 0x3000) || matches!(right, 32 | 0xa0 | 0x3000) {
        0
    } else {
        i32::from(tracking)
    }
}
fn scaled(advance: i32, style: u8, small: bool) -> i32 {
    if style & (SUP | SUB) != 0 {
        (advance + 1) / 2
    } else if small {
        (advance * 3 + 2) / 4
    } else {
        advance
    }
}
fn solid(cp: u32) -> bool {
    matches!(cp, 0x2588 | 0x25a0)
}

/// Like getGlyphMetrics: synthesize a solid only when the font lacks it.
/// The optional M lookup has the same miss/cache side effects as glyph().
pub fn glyph_metrics(face: &mut impl FontFace, cp: u32, style: u8) -> Option<Glyph> {
    if !solid(cp) || face.has_codepoint(cp, style) {
        return face.glyph(cp, style);
    }
    let em = if face.has_codepoint(77, style) {
        face.glyph(77, style)
    } else {
        None
    };
    let g = crate::font::solid_metrics(cp, face.ascender(style), em.map(|g| g.advance));
    Some(Glyph {
        advance: g.advance_x,
        width: g.width,
        height: g.height,
        left: g.left,
        top: g.top,
    })
}

#[inline(always)]
fn apply_ligatures(
    face: &impl FontFace,
    mut cp: u32,
    rest: &mut (impl Iterator<Item = u32> + Clone),
    style: u8,
) -> u32 {
    loop {
        let mut tail = rest.clone();
        let Some(next) = tail.next() else {
            break;
        };
        if next == 0 {
            break;
        }
        let lig = face.ligature(cp, next, style);
        if lig == 0 {
            break;
        }
        cp = lig;
        *rest = tail;
    }
    cp
}

/// `transparent` is the bidi provider's isTransparentMark predicate. It must
/// classify the visual stream without modifying it or the face.
pub fn text_advance_x(
    face: &mut impl FontFace,
    visual: &[u32],
    style: u8,
    tracking: i8,
    mode: MeasureMode,
    transparent: impl Fn(u32) -> bool,
) -> i32 {
    advance_stream(
        face,
        visual.iter().copied(),
        style,
        tracking,
        mode,
        transparent,
    )
}
pub fn advance_stream(
    face: &mut impl FontFace,
    mut visual: impl Iterator<Item = u32> + Clone,
    style: u8,
    tracking: i8,
    mode: MeasureMode,
    transparent: impl Fn(u32) -> bool,
) -> i32 {
    let mut prev = 0;
    if mode == MeasureMode::Layout && face.has_advance_table() {
        let resolved = face.resolve_style(style);
        let mut width = 0i32;
        let mut spacing = 0i32;
        for cp in visual {
            if cp == 0 {
                break;
            }
            if transparent(cp) {
                continue;
            }
            let mapped = if style & SMALL_CAPS != 0 {
                small_caps_upper(cp)
            } else {
                cp
            };
            let mut advance = i32::from(face.advance(mapped, resolved));
            if mapped < 0x300 || !combining(mapped) {
                if advance == 0 || (solid(mapped) && !face.has_codepoint(mapped, style)) {
                    advance =
                        glyph_metrics(face, mapped, style).map_or(0, |g| i32::from(g.advance));
                }
                spacing = spacing.wrapping_add(tracking_between(prev, mapped, tracking));
                prev = mapped;
            }
            width = width.wrapping_add(scaled(advance, style, mapped != cp));
        }
        return to_pixel(width).wrapping_add(spacing);
    }
    let mut width = 0i32;
    let mut previous_advance = 0;
    while let Some(cp) = visual.next() {
        if cp == 0 {
            break;
        }
        if transparent(cp) || (cp >= 0x300 && combining(cp)) {
            continue;
        }
        let small = if style & SMALL_CAPS != 0 {
            small_caps_upper(cp)
        } else {
            cp
        };
        let mapped = if style & SMALL_CAPS != 0 {
            small
        } else {
            apply_ligatures(face, cp, &mut visual, style)
        };
        if prev != 0 {
            let kern = i32::from(face.kerning(prev, mapped, style));
            width = width
                .wrapping_add(to_pixel(previous_advance + kern))
                .wrapping_add(tracking_between(prev, mapped, tracking));
        }
        let advance = glyph_metrics(face, mapped, style).map_or(0, |g| i32::from(g.advance));
        previous_advance = scaled(advance, style, small != cp);
        prev = mapped;
    }
    width.wrapping_add(to_pixel(previous_advance))
}

fn space_fp(face: &mut impl FontFace, style: u8) -> i32 {
    let resolved = face.resolve_style(style);
    let advance = face.advance(32, resolved);
    if advance != 0 {
        i32::from(advance)
    } else {
        face.glyph(32, resolved).map_or(0, |g| i32::from(g.advance))
    }
}
pub fn space_width(face: &mut impl FontFace, style: u8) -> i32 {
    if face.has_advance_table() {
        to_pixel(space_fp(face, style))
    } else {
        face.glyph(32, style)
            .map_or(0, |g| to_pixel(i32::from(g.advance)))
    }
}
pub fn space_advance(face: &mut impl FontFace, left: u32, right: u32, style: u8) -> i32 {
    if face.has_advance_table() {
        return to_pixel(space_fp(face, style));
    }
    let advance = face.glyph(32, style).map_or(0, |g| i32::from(g.advance));
    let kern = i32::from(face.kerning(left, 32, style)) + i32::from(face.kerning(32, right, style));
    to_pixel(advance + kern)
}
pub fn kerning(face: &mut impl FontFace, left: u32, right: u32, style: u8, tracking: i8) -> i32 {
    to_pixel(i32::from(face.kerning(left, right, style))) + tracking_between(left, right, tracking)
}
/// Raw glyph query: no solid synthesis or scaling, like getCodepointMetrics.
pub fn codepoint_metrics(face: &mut impl FontFace, cp: u32, style: u8) -> Option<(i32, i32)> {
    face.glyph(cp, style)
        .map(|g| (i32::from(g.advance), i32::from(g.top)))
}
pub fn font_ascender(face: &impl FontFace) -> i32 {
    face.ascender(0)
}
pub fn text_height(face: &impl FontFace) -> i32 {
    face.ascender(0)
}
pub fn line_height(face: &impl FontFace) -> i32 {
    i32::from(face.line_height())
}
pub fn compressed_line_height(face: &impl FontFace, compression: f32) -> i32 {
    (line_height(face) as f32 * compression + 0.5) as i32
}

/// Ink bounds, matching EpdFont::getTextDimensions. Decorations, small caps,
/// sup/sub and transparent-mark skipping deliberately do not apply here.
pub fn text_dimensions(face: &mut impl FontFace, visual: &[u32], style: u8) -> (i32, i32) {
    dimensions_stream(face, visual.iter().copied(), style)
}
pub fn dimensions_stream(
    face: &mut impl FontFace,
    mut visual: impl Iterator<Item = u32> + Clone,
    style: u8,
) -> (i32, i32) {
    let (mut min_x, mut max_x, mut min_y, mut max_y) = (0i32, 0i32, 0i32, 0i32);
    let (mut x, mut left, mut width, mut top, mut advance, mut prev) =
        (0i32, 0i32, 0i32, 0i32, 0i32, 0u32);
    while let Some(cp) = visual.next() {
        if cp == 0 {
            break;
        }
        let mark = cp >= 0x300 && combining(cp);
        let cp = if mark {
            cp
        } else {
            apply_ligatures(face, cp, &mut visual, style)
        };
        let Some(g) = glyph_metrics(face, cp, style) else {
            if !mark {
                x = x.wrapping_add(to_pixel(advance));
                prev = 0;
                advance = 0;
                left = 0;
                width = 0;
                top = 0;
            }
            continue;
        };
        // utf8IsCombiningMark covers only the CenterRaised anchor ranges.
        let raise = if mark && i32::from(g.top) - i32::from(g.height) > 0 {
            (1 - (i32::from(g.top) - i32::from(g.height) - top)).max(0)
        } else {
            0
        };
        if !mark && prev != 0 {
            x = x.wrapping_add(to_pixel(advance + i32::from(face.kerning(prev, cp, style))));
        }
        let glyph_x = if mark {
            x.wrapping_add(left + width / 2 - i32::from(g.width) / 2 - i32::from(g.left))
        } else {
            x
        };
        let gx = glyph_x.wrapping_add(i32::from(g.left));
        min_x = min_x.min(gx);
        max_x = max_x.max(gx.wrapping_add(i32::from(g.width)));
        min_y = min_y.min(-raise + i32::from(g.top) - i32::from(g.height));
        max_y = max_y.max(-raise + i32::from(g.top));
        if !mark {
            left = i32::from(g.left);
            width = i32::from(g.width);
            top = i32::from(g.top);
            advance = i32::from(g.advance);
            prev = cp;
        }
    }
    (max_x.wrapping_sub(min_x), max_y.wrapping_sub(min_y))
}
pub fn text_width(face: &mut impl FontFace, visual: &[u32], style: u8) -> i32 {
    text_dimensions(face, visual, style).0
}

pub mod native;

pub mod draw;
