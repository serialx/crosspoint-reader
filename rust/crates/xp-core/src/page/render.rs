//! Page painting over borrowed records. The backend owns fonts, pixels and I/O.
use super::{Element, Line, Page, Result};
use crate::{
    bidi, text,
    textm::{MeasureMode, SMALL_CAPS, SUB, SUP},
};

#[derive(Clone, Copy)]
pub struct Text<'a> {
    pub font: i32,
    pub bytes: &'a [u8],
    pub x: i32,
    pub y: i32,
    pub style: u8,
    pub tracking: i8,
    pub direction: i32,
}
pub trait Canvas {
    #[cfg(feature = "strip-spike")]
    fn element(&mut self, _index: u16, _element: Element<'_>, _x: i32, _y: i32) -> bool {
        true
    }

    fn scanning(&self) -> bool;
    fn font(&mut self, requested: i32, section: i32) -> i32;
    fn ascent(&self, font: i32) -> i32;
    fn advance(&self, text: Text<'_>, mode: MeasureMode) -> i32;
    fn top(&self, font: i32, cp: u32, style: u8) -> Option<i32>;
    fn text(&mut self, text: Text<'_>);
    fn end_line(&mut self);
    fn rect(&mut self, x: i32, y: i32, w: i32, h: i32, shade: bool);
    fn scaled(&mut self, font: i32, text: &[u8], style: u8, scale: i32, x: i32, y: i32);
    fn image(&mut self, index: u16, x: i32, y: i32, placeholder: bool);
}
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    All,
    Images,
    Placeholders,
}

pub fn page(
    page: Page<'_>,
    canvas: &mut impl Canvas,
    font: i32,
    x: i32,
    y: i32,
    mode: Mode,
) -> Result<()> {
    if mode != Mode::Images {
        for i in 0..page.element_count() {
            if page.position(i)?.2 != 5 {
                continue;
            }
            let e = page.element(i)?;
            #[cfg(feature = "strip-spike")]
            {
                let (ex, ey) = e.position()?;
                if !canvas.element(i, e, x + i32::from(ex), y + i32::from(ey)) {
                    continue;
                }
            }
            let d = e.data.get(..18).ok_or(super::Error::Invalid)?;
            if d[17] != 0 {
                let (ex, ey) = e.position()?;
                canvas.rect(
                    x + i32::from(ex),
                    y + i32::from(ey),
                    i32::from(read16(&d[5..], 0)),
                    i32::from(read16(&d[7..], 0)),
                    true,
                );
            }
        }
        for i in 0..page.element_count() {
            if page.position(i)?.2 == 5 {
                let e = page.element(i)?;
                #[cfg(feature = "strip-spike")]
                {
                    let (ex, ey) = e.position()?;
                    if !canvas.element(i, e, x + i32::from(ex), y + i32::from(ey)) {
                        continue;
                    }
                }
                border(e, canvas, x, y)?;
            }
        }
    }
    for i in 0..page.element_count() {
        let e = page.element(i)?;
        let (ex, ey) = e.position()?;
        let (ex, ey) = (x + i32::from(ex), y + i32::from(ey));
        #[cfg(feature = "strip-spike")]
        if !canvas.element(i, e, ex, ey) {
            continue;
        }
        match e.tag()? {
            2 => canvas.image(i, ex, ey, mode == Mode::Placeholders),
            1 if mode != Mode::Images => line(render_line(e)?, canvas, font, ex, ey)?,
            3 if mode != Mode::Images => {
                let d = e.data.get(..8).ok_or(super::Error::Invalid)?;
                canvas.rect(
                    ex,
                    ey,
                    i32::from(read16(&d[5..], 0)),
                    i32::from(d[7]),
                    false,
                );
            }
            4 if mode != Mode::Images => {
                let d = e.data.get(..13).ok_or(super::Error::Invalid)?;
                let id = i32::from_le_bytes(d[5..9].try_into().unwrap());
                let draw_font = canvas.font(id, font);
                canvas.scaled(
                    draw_font,
                    e.drop_cap_text()?,
                    d[11],
                    i32::from(read16(&d[9..], 0)),
                    ex,
                    ey,
                );
            }
            _ => {}
        }
    }
    canvas.end_line();
    Ok(())
}
fn border(e: Element<'_>, c: &mut impl Canvas, x: i32, y: i32) -> Result<()> {
    let d = e.data.get(..18).ok_or(super::Error::Invalid)?;
    let (ex, ey) = e.position()?;
    let (x, y, w, h) = (
        x + i32::from(ex),
        y + i32::from(ey),
        i32::from(read16(&d[5..], 0)),
        i32::from(read16(&d[7..], 0)),
    );
    // The legacy order is top, bottom, left, right.
    for side in [0, 2, 3, 1] {
        let thickness = i32::from(d[9 + side * 2]);
        let style = d[10 + side * 2];
        if thickness == 0 || style == 0 {
            continue;
        }
        let horizontal = side % 2 == 0;
        let (sx, sy) = (
            x + if side == 1 { w - thickness } else { 0 },
            y + if side == 2 { h - thickness } else { 0 },
        );
        let length = if horizontal { w } else { h };
        let mut strip = |offset, along, span, depth| {
            if horizontal {
                c.rect(sx + along, sy + offset, span, depth, false);
            } else {
                c.rect(sx + offset, sy + along, depth, span, false);
            }
        };
        match style {
            2 if thickness >= 3 => {
                let line = 1.max(thickness / 3);
                strip(0, 0, length, line);
                strip(thickness - line, 0, length, line);
            }
            3 | 4 => {
                let dash = if style == 3 {
                    thickness
                } else {
                    3.max(thickness * 3)
                };
                let gap = if style == 3 {
                    2.max(thickness)
                } else {
                    2.max(thickness * 2)
                };
                let mut along = 0;
                while along < length {
                    strip(0, along, dash.min(length - along), thickness);
                    along += dash + gap;
                }
            }
            _ => strip(0, 0, length, thickness),
        }
    }
    Ok(())
}
#[derive(Clone, Copy)]
struct Decoration {
    start: i32,
    end: i32,
    y: i32,
}
impl Decoration {
    const EMPTY: Self = Self {
        start: -1,
        end: -1,
        y: 0,
    };
    fn flush(&mut self, c: &mut impl Canvas) {
        if self.start != -1 {
            c.rect(
                self.start.min(self.end),
                self.y,
                (self.end - self.start).abs() + 1,
                2,
                false,
            );
            *self = Self::EMPTY;
        }
    }
}

fn render_line(element: Element<'_>) -> Result<Line<'_>> {
    let h = element.data.get(5..12).ok_or(super::Error::Invalid)?;
    Ok(Line {
        element,
        count: read16(h, 0),
        focus: h[2] != 0,
        text_bytes: read16(&h[3..], 0),
        paragraph_start: read16(&h[5..], 0),
    })
}

// Resolve packed-array boundaries once per line, outside the word loop.
struct RenderLine<'a> {
    line: Line<'a>,
    offsets: &'a [u8],
    positions: &'a [u8],
    styles: &'a [u8],
    boundaries: &'a [u8],
    suffixes: &'a [u8],
    text: &'a [u8],
    slots: &'a [u8],
    fonts: Option<[i32; 3]>,
    font: i32,
    tracking: i8,
    rtl: bool,
}
#[derive(Clone, Copy)]
struct RenderWord<'a> {
    text: &'a [u8],
    x: i16,
    style: u8,
    font: i32,
    focus_boundary: u8,
    focus_suffix: u16,
}
impl<'a> RenderLine<'a> {
    fn new(line: Line<'a>, section: i32) -> Result<Self> {
        let n = usize::from(line.count);
        let packed = line.packed_data()?;
        let style = line.style_data()?;
        let raw_font = i32::from_le_bytes(style[24..28].try_into().unwrap());
        let fonts = line.fonts()?;
        let styles_at = n * (12 + if line.focus { 2 } else { 0 });
        let text_at = n * (13 + if line.focus { 3 } else { 0 });
        Ok(Self {
            line,
            offsets: &packed[n * 8..n * 10],
            positions: &packed[n * 10..n * 12],
            styles: &packed[styles_at..styles_at + n],
            boundaries: if line.focus {
                &packed[styles_at + n..text_at]
            } else {
                &[]
            },
            suffixes: if line.focus {
                &packed[n * 12..n * 14]
            } else {
                &[]
            },
            text: &packed[text_at..],
            slots: if fonts.is_some() {
                &line.font_data()?[12..]
            } else {
                &[]
            },
            fonts,
            font: if raw_font == 0 { section } else { raw_font },
            tracking: style[23] as i8,
            rtl: style[21] != 0,
        })
    }
    fn word(&self, i: u16) -> Result<RenderWord<'a>> {
        let i = usize::from(i);
        let start = usize::from(read16(self.offsets, i));
        let end = if i + 1 < usize::from(self.line.count) {
            usize::from(read16(self.offsets, i + 1))
        } else {
            self.text.len()
        };
        let text = self
            .text
            .get(start..end.saturating_sub(1))
            .ok_or(super::Error::Invalid)?;
        let slot = self.slots.get(i).copied().unwrap_or(0);
        let font = match self.fonts {
            Some(fonts) if slot & 127 != 0 => *fonts
                .get(usize::from((slot & 127) - 1))
                .ok_or(super::Error::Invalid)?,
            _ => self.font,
        };
        Ok(RenderWord {
            text,
            x: read16(self.positions, i) as i16,
            style: (self.styles[i] & 127) | (slot & 128),
            font,
            focus_boundary: self.boundaries.get(i).copied().unwrap_or(0),
            focus_suffix: if self.line.focus {
                read16(self.suffixes, i)
            } else {
                0
            },
        })
    }
}
#[inline]
fn read16(data: &[u8], i: usize) -> u16 {
    u16::from_le_bytes([data[i * 2], data[i * 2 + 1]])
}

pub fn line(line: Line<'_>, c: &mut impl Canvas, section: i32, x: i32, y: i32) -> Result<()> {
    let data = RenderLine::new(line, section)?;
    let font = c.font(data.font, section);
    let tracking = data.tracking;
    let fonts = data.fonts;
    let mut ascender = c.ascent(font);
    if let Some(fonts) = fonts {
        for id in fonts {
            if id != 0 {
                let id = c.font(id, section);
                ascender = ascender.max(c.ascent(id));
            }
        }
    }
    let shift = if line.has_ruby() { ascender / 2 } else { 0 };
    let mut decorations = [Decoration::EMPTY; 2];
    for i in 0..line.count {
        let word = data.word(i)?;
        let direction = bidi::detect_paragraph_level(word.text, i32::from(data.rtl), 64);
        let word_font = if fonts.is_some() {
            c.font(word.font, section)
        } else {
            font
        };
        let word_ascent = if fonts.is_none() {
            ascender
        } else {
            c.ascent(word_font)
        };
        let mut word_y = y + shift + ascender - word_ascent;
        if word.style & SUP != 0 {
            word_y -= ascender * 2 / 5;
        } else if word.style & SUB != 0 {
            word_y += ascender / 4;
        }
        let t = Text {
            font: word_font,
            bytes: word.text,
            x: x + i32::from(word.x),
            y: word_y,
            style: word.style,
            tracking,
            direction,
        };
        if word.focus_boundary != 0 {
            let n = usize::from(word.focus_boundary)
                .min(word.text.len())
                .min(39);
            c.text(Text {
                bytes: &word.text[..n],
                style: word.style | 1,
                ..t
            });
            c.text(Text {
                bytes: &word.text[n..],
                x: t.x + i32::from(word.focus_suffix),
                ..t
            });
        } else {
            c.text(t);
        }
        let ruby = line.ruby(i)?;
        if !ruby.is_empty() && word.style & 64 == 0 {
            if c.scanning() {
                c.text(Text {
                    font,
                    bytes: ruby,
                    x: 0,
                    y: 0,
                    style: SUP,
                    tracking: 0,
                    direction: -1,
                });
            } else {
                draw_ruby(
                    &data,
                    i,
                    c,
                    Text {
                        font,
                        bytes: ruby,
                        y: word_y - ascender,
                        style: SUP,
                        ..t
                    },
                )?;
            }
        }
        if c.scanning() {
            continue;
        }
        if word.style & 12 == 0 {
            for d in &mut decorations {
                d.flush(c);
            }
            continue;
        }
        let mut start = t.x;
        let mut width = c.advance(t, MeasureMode::Render);
        if word.text.starts_with(&[0xe2, 0x80, 0x83]) {
            start += c.advance(
                Text {
                    bytes: &word.text[..3],
                    ..t
                },
                MeasureMode::Render,
            );
            width = c.advance(
                Text {
                    bytes: &word.text[3..],
                    ..t
                },
                MeasureMode::Render,
            );
        }
        for (d, flag) in decorations.iter_mut().zip([4, 8]) {
            if word.style & flag == 0 {
                d.flush(c);
                continue;
            }
            let mut offset = 2;
            if flag == 8 {
                let small = word.style & SMALL_CAPS != 0;
                let mut height = c
                    .top(
                        word_font,
                        if small {
                            u32::from(b'X')
                        } else {
                            u32::from(b'x')
                        },
                        word.style,
                    )
                    .filter(|&h| h > 0)
                    .unwrap_or(word_ascent * 2 / 3);
                if word.style & (SUP | SUB) != 0 {
                    height = (height + 1) / 2;
                } else if small {
                    height = (height * 3 + 3) / 4;
                }
                offset = -1.max(height / 2);
            }
            let line_y = word_y + word_ascent + offset;
            if d.start != -1 && d.y != line_y {
                d.flush(c);
            }
            if d.start == -1 {
                d.start = start;
                d.y = line_y;
            }
            d.end = start + width;
        }
    }
    for d in &mut decorations {
        d.flush(c);
    }
    c.end_line();
    Ok(())
}
// Ruby has no retained token vector: offsets are streamed from the same CJK rules.
fn token(bytes: &[u8], k: usize) -> &[u8] {
    let (mut start, mut end, mut i) = (0, bytes.len(), 0);
    text::character_offsets(bytes, usize::MAX, &mut |offset| {
        if i + 1 == k {
            start = offset;
        }
        if i == k {
            end = offset;
        }
        i += 1;
    });
    &bytes[start..end]
}
fn draw_ruby(data: &RenderLine<'_>, i: u16, c: &mut impl Canvas, t: Text<'_>) -> Result<()> {
    let line = data.line;
    let mut count = 1;
    while i + count < line.count && data.word(i + count)?.style & 64 != 0 {
        count += 1;
    }
    let (mut width, mut chars) = (0, 0);
    for k in 0..count {
        let w = data.word(i + k)?;
        width += c.advance(
            Text {
                bytes: w.text,
                style: w.style,
                direction: -1,
                ..t
            },
            MeasureMode::Layout,
        );
        chars += w
            .text
            .iter()
            .take_while(|&&b| b != 0)
            .filter(|&&b| b & 0xc0 != 0x80)
            .count() as i32;
    }
    let ruby_width = c.advance(Text { direction: -1, ..t }, MeasureMode::Layout);
    let breaks = if ruby_width < width {
        text::character_offsets(t.bytes, 0, &mut |_| {})
    } else {
        0
    };
    if breaks != 0 {
        let n = breaks + 1;
        let mut sum = 0;
        for k in 0..n {
            sum += c.advance(
                Text {
                    bytes: token(t.bytes, k),
                    direction: -1,
                    ..t
                },
                MeasureMode::Layout,
            );
        }
        if sum < width {
            let spread = width - sum;
            let edge = (spread / (2 * n as i32)).min(width / (2 * chars.max(1)));
            let inner = spread - 2 * edge;
            let mut pen = 0;
            for k in 0..n {
                let bytes = token(t.bytes, if t.direction == 1 { n - 1 - k } else { k });
                let gx =
                    t.x + pen + edge + (inner * k as i32 + (n as i32 - 1) / 2) / (n as i32 - 1);
                c.text(Text { bytes, x: gx, ..t });
                pen += c.advance(
                    Text {
                        bytes,
                        direction: -1,
                        ..t
                    },
                    MeasureMode::Layout,
                );
            }
            return Ok(());
        }
    }
    c.text(Text {
        x: t.x - (ruby_width - width) / 2,
        ..t
    });
    Ok(())
}
