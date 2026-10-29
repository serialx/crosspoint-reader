use crate::scene::Rect;
use core::ffi::{c_char, CStr};
use xp_core::para::paragraph::Paragraph;
#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq)]
pub struct Key {
    pub font_id: i32,
    pub point_size: i32,
    pub margin: i32,
    pub width: i32,
    pub height: i32,
    pub compression: f32,
    pub alignment: u8,
    pub paragraph_gap: u8,
    pub indent: u8,
    pub tracking: i8,
    pub word_spacing: u8,
    pub focus: u8,
    pub hyphenation: u8,
    pub reserved: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Geometry {
    pub caption: Rect,
    pub text: Rect,
    pub advance: i32,
    pub gap: i32,
    pub caption_visible: u8,
    pub text_visible: u8,
    pub reserved: [u8; 2],
}
pub fn geometry(
    bounds: Rect,
    padding: i16,
    label_gap: i16,
    label_height: i32,
    line_height: i32,
    advance: i32,
    key: Key,
) -> Option<Geometry> {
    if padding < 0
        || label_gap < 0
        || !(0..=8192).contains(&label_height)
        || !(0..=8192).contains(&line_height)
        || !(0..=8192).contains(&key.margin)
    {
        return None;
    }
    let left = i32::from(bounds.x) + i32::from(padding);
    let width = i32::from(bounds.w) - 2 * i32::from(padding);
    let height = i32::from(bounds.h);
    let y = i32::from(bounds.y);
    let caption = Rect {
        x: i16::try_from(left).ok()?,
        y: i16::try_from(y + height - i32::from(padding) - label_height).ok()?,
        w: i16::try_from(width).ok()?,
        h: 0,
    };
    let text = Rect {
        x: i16::try_from(left + key.margin).ok()?,
        y: i16::try_from(y + i32::from(padding)).ok()?,
        w: i16::try_from(width - 2 * key.margin).ok()?,
        h: i16::try_from(height - label_height - i32::from(label_gap) - 2 * i32::from(padding))
            .ok()?,
    };
    let visible = width > 0 && height > 0;
    Some(Geometry {
        caption,
        text,
        advance: advance.max(1),
        gap: if key.paragraph_gap != 0 {
            advance.max(1) / 2
        } else {
            0
        },
        caption_visible: u8::from(visible),
        text_visible: u8::from(visible && key.font_id != 0 && line_height > 0 && text.w > 0),
        reserved: [0; 2],
    })
}
pub fn capacity(paragraph: &Paragraph, height: i32, line_height: i32, advance: i32) -> usize {
    if advance <= 0 || line_height <= 0 || height > 8192 {
        return usize::MAX;
    }
    let lines = if height < line_height {
        0
    } else {
        ((height - line_height) / advance + 1) as usize
    };
    let words = paragraph.tokens.len();
    let mut bytes = 0usize;
    for i in 0..words {
        let Some(n) = bytes.checked_add(paragraph.word(i).len() + 1) else {
            return usize::MAX;
        };
        bytes = n;
    }
    let Some(extra) = lines.checked_add(2) else {
        return usize::MAX;
    };
    let Some(words) = words.checked_mul(2).and_then(|n| n.checked_add(extra)) else {
        return usize::MAX;
    };
    let Some(bytes) = bytes.checked_mul(2).and_then(|n| n.checked_add(extra * 4)) else {
        return usize::MAX;
    };
    xp_core::page::builder::line_capacity(lines, words, bytes, true, false, 0)
}
/// # Safety
/// paragraph is live and exclusively owned; text is a live terminated string.
pub unsafe fn feed(paragraph: &mut Paragraph, text: *const c_char) {
    if text.is_null() {
        return;
    }
    for word in unsafe { CStr::from_ptr(text) }
        .to_bytes()
        .split(|c| *c == b' ')
    {
        if !word.is_empty() {
            paragraph.add_word(word, 0, false, false, 0, 0, 0, false);
        }
    }
}
pub fn line(
    row: i32,
    after: i32,
    height: i32,
    line_height: i32,
    advance: i32,
    gap: i32,
) -> Option<([i16; 2], u8)> {
    if row < 0 || after < 0 || line_height <= 0 || advance <= 0 || gap < 0 {
        return None;
    }
    let first = row.checked_mul(advance)?;
    let second = row
        .checked_add(after)?
        .checked_add(1)?
        .checked_mul(advance)?
        .checked_add(gap)?
        .checked_add(first)?;
    let y = [i16::try_from(first).ok()?, i16::try_from(second).ok()?];
    Some((
        y,
        u8::from(first.checked_add(line_height)? <= height)
            | u8::from(second.checked_add(line_height)? <= height) << 1,
    ))
}
