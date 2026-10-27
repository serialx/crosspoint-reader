//! Allocation-free page records over a relocatable caller-owned byte arena.
//!
//! The arena retains the compact section representation and adds an element
//! index plus ruby indexes only on annotated lines. All integer loads are byte
//! loads: an arena can start at any address, including on RISC-V.
use crate::section::{self, Kind};

pub mod builder;
pub mod probe;
#[cfg(test)]
mod tests;

const HEADER: usize = 32;
const ELEMENT: usize = 16;
const MAGIC: u32 = 0x31504758;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Invalid,
    NeedInput,
    NeedOutput(usize),
}
pub type Result<T> = core::result::Result<T, Error>;

fn range(bytes: &[u8], start: usize, len: usize) -> Result<&[u8]> {
    bytes
        .get(start..start.checked_add(len).ok_or(Error::Invalid)?)
        .ok_or(Error::NeedInput)
}
fn u16_at(bytes: &[u8], at: usize) -> Result<u16> {
    let b = range(bytes, at, 2)?;
    Ok(u16::from_le_bytes([b[0], b[1]]))
}
fn u32_at(bytes: &[u8], at: usize) -> Result<u32> {
    let b = range(bytes, at, 4)?;
    Ok(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
}
fn put(bytes: &mut [u8], at: usize, value: usize) {
    bytes[at..at + 4].copy_from_slice(&(value as u32).to_le_bytes());
}
fn c_len(bytes: &[u8]) -> usize {
    bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len())
}

fn terminated(bytes: &[u8], extra: &mut usize, out: Option<&mut [u8]>) -> Result<usize> {
    let at = *extra;
    *extra = at
        .checked_add(bytes.len())
        .and_then(|end| end.checked_add(5))
        .ok_or(Error::Invalid)?;
    if let Some(out) = out {
        put(out, at, bytes.len());
        out[at + 4..at + 4 + bytes.len()].copy_from_slice(bytes);
        out[at + 4 + bytes.len()] = 0;
    }
    Ok(at)
}

struct Reader<'a> {
    data: &'a [u8],
    pos: usize,
}
impl<'a> Reader<'a> {
    fn take(&mut self, size: usize) -> Result<&'a [u8]> {
        let b = range(self.data, self.pos, size)?;
        self.pos += size;
        Ok(b)
    }
    fn record(&mut self, kind: Kind) -> Result<section::Record> {
        let b = self.take(kind.size())?;
        section::decode(kind, b)
            .map(|(r, _)| r)
            .map_err(|_| Error::Invalid)
    }
    fn string(&mut self) -> Result<&'a [u8]> {
        let len = self.record(Kind::U32)?.values[0] as usize;
        self.take(len)
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(C)]
pub struct Info {
    pub arena_size: usize,
    pub consumed: usize,
    pub elements: u16,
    pub footnotes: u16,
    pub links: u16,
}

// The same scanner performs the sizing pass and fills indexes after capacity
// has been checked. It never allocates or follows a pointer from the input.
fn scan(bytes: &[u8], mut index: Option<&mut [u8]>) -> Result<Info> {
    if bytes.len() > u32::MAX as usize || bytes.len() > isize::MAX as usize {
        return Err(Error::Invalid);
    }
    let mut r = Reader {
        data: bytes,
        pos: 0,
    };
    let count = r.record(Kind::U16)?.values[0] as usize;
    let mut extra = HEADER + count * ELEMENT;
    for i in 0..count {
        let start = r.pos;
        let tag = r.record(Kind::ElementTag)?.values[0];
        let mut style = 0;
        let mut ruby_index = 0;
        match tag {
            1 => {
                r.record(Kind::Position)?;
                let h = r.record(Kind::TextHeader)?.values;
                let n = h[0] as u16;
                let focus = h[1] != 0;
                let len = section::arena_size(n, focus, h[2] as u16);
                if !section::validate_arena(n, focus, h[2] as u16, r.take(len)?) {
                    return Err(Error::Invalid);
                }
                let ruby_start = r.pos;
                let mut has_ruby = false;
                for _ in 0..n {
                    has_ruby |= !r.string()?.is_empty();
                }
                if has_ruby {
                    ruby_index = extra;
                    extra = extra
                        .checked_add(usize::from(n) * 4)
                        .ok_or(Error::Invalid)?;
                    let mut ruby = Reader {
                        data: bytes,
                        pos: ruby_start,
                    };
                    for word in 0..usize::from(n) {
                        let at = terminated(ruby.string()?, &mut extra, index.as_deref_mut())?;
                        if let Some(ref mut out) = index {
                            put(out, ruby_index + word * 4, at);
                        }
                    }
                }
                style = r.pos;
                r.record(Kind::BlockStyle)?;
                if r.record(Kind::WordFontFlag)?.values[0] != 0
                    && !section::validate_fonts(n, r.take(12 + usize::from(n))?)
                {
                    return Err(Error::Invalid);
                }
            }
            2 => {
                r.record(Kind::Position)?;
                style = terminated(r.string()?, &mut extra, index.as_deref_mut())?;
                ruby_index = terminated(r.string()?, &mut extra, index.as_deref_mut())?;
                r.record(Kind::ImageSize)?;
            }
            3 => {
                r.record(Kind::Rule)?;
            }
            4 => {
                let len = r.record(Kind::DropCap)?.values[5] as usize;
                r.take(len)?;
            }
            5 => {
                r.record(Kind::Border)?;
            }
            _ => return Err(Error::Invalid),
        }
        if let Some(ref mut out) = index {
            let entry = HEADER + i * ELEMENT;
            put(out, entry, start);
            put(out, entry + 4, r.pos);
            put(out, entry + 8, style);
            put(out, entry + 12, ruby_index);
        }
    }
    let foot_offset = r.pos;
    let footnotes = r.record(Kind::FootnoteCount)?.values[0] as u16;
    r.take(usize::from(footnotes) * 288)?;
    let link_offset = r.pos;
    let links = r.record(Kind::LinkCount)?.values[0] as u16;
    for _ in 0..links {
        if r.take(256)?[0] == 0 {
            return Err(Error::Invalid);
        }
        r.record(Kind::LinkGeometry)?;
    }
    let total = extra.checked_add(r.pos).ok_or(Error::Invalid)?;
    if total > u32::MAX as usize || total > isize::MAX as usize {
        return Err(Error::Invalid);
    }
    if let Some(out) = index {
        put(out, 0, MAGIC as usize);
        put(out, 4, total);
        put(out, 8, extra);
        put(out, 12, r.pos);
        put(out, 24, foot_offset);
        put(out, 28, link_offset);
    }
    Ok(Info {
        arena_size: total,
        consumed: r.pos,
        elements: count as u16,
        footnotes,
        links,
    })
}

/// Accepts one page prefix, just like Page::deserialize; consumed excludes any
/// following page or section LUT. No input-sized scratch is needed.
pub fn inspect(bytes: &[u8]) -> Result<Info> {
    scan(bytes, None)
}

/// The input and destination are disjoint. On malformed input or short output,
/// destination is unchanged. Only destination[..arena_size] is initialized.
pub fn decode(bytes: &[u8], visible_offset: u32, destination: &mut [u8]) -> Result<Info> {
    let info = inspect(bytes)?;
    if destination.len() < info.arena_size {
        return Err(Error::NeedOutput(info.arena_size));
    }
    scan(bytes, Some(destination))?;
    put(destination, 16, visible_offset as usize);
    put(destination, 20, usize::from(info.elements));
    let wire = u32_at(destination, 8)? as usize;
    destination[wire..info.arena_size].copy_from_slice(&bytes[..info.consumed]);
    normalize(destination, info)
}

/// Validates bytes already at their final offset, then fills the disjoint index.
/// Invalid input or sizing disagreement leaves the allocation unchanged.
pub fn decode_in_place(
    arena: &mut [u8],
    wire_offset: usize,
    wire_len: usize,
    visible: u32,
) -> Result<Info> {
    let info = inspect(range(arena, wire_offset, wire_len)?)?;
    if info.arena_size
        != wire_offset
            .checked_add(info.consumed)
            .ok_or(Error::Invalid)?
    {
        return Err(Error::Invalid);
    }
    let (index, wire) = arena.split_at_mut(wire_offset);
    scan(&wire[..wire_len], Some(index))?;
    put(index, 16, visible as usize);
    put(index, 20, usize::from(info.elements));
    normalize(arena, info)
}

fn normalize(destination: &mut [u8], info: Info) -> Result<Info> {
    let wire = u32_at(destination, 8)? as usize;
    // C++ converts these bytes to bool and terminates fixed strings on load.
    for i in 0..usize::from(info.elements) {
        let entry = HEADER + i * ELEMENT;
        let start = wire + u32_at(destination, entry)? as usize;
        match destination[start] {
            1 => {
                destination[start + 7] = u8::from(destination[start + 7] != 0);
                let style = wire + u32_at(destination, entry + 8)? as usize;
                for off in [1, 20, 21, 22] {
                    destination[style + off] = u8::from(destination[style + off] != 0);
                }
            }
            5 => {
                destination[start + 17] = u8::from(destination[start + 17] != 0);
            }
            _ => {}
        }
    }
    let foot = wire + u32_at(destination, 24)? as usize + 2;
    for i in 0..usize::from(info.footnotes) {
        destination[foot + i * 288 + 31] = 0;
        destination[foot + i * 288 + 287] = 0;
    }
    let link = wire + u32_at(destination, 28)? as usize + 2;
    for i in 0..usize::from(info.links) {
        destination[link + i * 264 + 255] = 0;
    }
    Ok(info)
}

#[derive(Clone, Copy)]
pub struct Page<'a> {
    arena: &'a [u8],
    wire: &'a [u8],
}
impl<'a> Page<'a> {
    /// Open a previously decoded arena. Index accesses remain bounds checked;
    /// callers must preserve the arena bytes while views are borrowed.
    pub fn open(arena: &'a [u8]) -> Result<Self> {
        if u32_at(arena, 0)? != MAGIC {
            return Err(Error::Invalid);
        }
        let total = u32_at(arena, 4)? as usize;
        let start = u32_at(arena, 8)? as usize;
        let len = u32_at(arena, 12)? as usize;
        if start < HEADER || start.checked_add(len).is_none_or(|end| end > total) {
            return Err(Error::Invalid);
        }
        Ok(Self {
            arena: range(arena, 0, total)?,
            wire: range(arena, start, len)?,
        })
    }
    pub fn info(&self) -> Info {
        Info {
            arena_size: self.arena.len(),
            consumed: self.wire.len(),
            elements: self.element_count(),
            footnotes: self.footnote_count(),
            links: self.link_count(),
        }
    }
    pub fn image_bounds(&self) -> Result<Option<[i16; 4]>> {
        let mut bounds: Option<[i16; 4]> = None;
        for i in 0..self.element_count() {
            let e = self.element(i)?;
            if e.tag()? != 2 {
                continue;
            }
            let (_, _, width, height) = e.image()?;
            let (x, y) = e.position()?;
            let right = x.wrapping_add(width);
            let bottom = y.wrapping_add(height);
            bounds = Some(match bounds {
                None => [x, y, right, bottom],
                Some(b) => [x.min(b[0]), y.min(b[1]), right.max(b[2]), bottom.max(b[3])],
            });
        }
        Ok(bounds.map(|b| [b[0], b[1], b[2].wrapping_sub(b[0]), b[3].wrapping_sub(b[1])]))
    }
    pub fn visible_offset(&self) -> u32 {
        u32_at(self.arena, 16).unwrap_or(0)
    }
    pub fn element_count(&self) -> u16 {
        u16_at(self.wire, 0).unwrap_or(0)
    }
    pub fn footnote_count(&self) -> u16 {
        self.footer(24)
            .and_then(|p| u16_at(self.wire, p))
            .unwrap_or(0)
    }
    pub fn link_count(&self) -> u16 {
        self.footer(28)
            .and_then(|p| u16_at(self.wire, p))
            .unwrap_or(0)
    }
    fn footer(&self, at: usize) -> Result<usize> {
        Ok(u32_at(self.arena, at)? as usize)
    }
    pub fn position(&self, index: u16) -> Result<(i16, i16, u16)> {
        if index >= self.element_count() {
            return Err(Error::Invalid);
        }
        let start = u32_at(self.arena, HEADER + usize::from(index) * ELEMENT)? as usize;
        let bytes = range(self.wire, start, 5)?;
        Ok((
            u16_at(bytes, 1)? as i16,
            u16_at(bytes, 3)? as i16,
            u16::from(bytes[0]),
        ))
    }
    pub fn element(&self, index: u16) -> Result<Element<'a>> {
        if index >= self.element_count() {
            return Err(Error::Invalid);
        }
        let at = HEADER + usize::from(index) * ELEMENT;
        let start = u32_at(self.arena, at)? as usize;
        let end = u32_at(self.arena, at + 4)? as usize;
        let data = range(
            self.wire,
            start,
            end.checked_sub(start).ok_or(Error::Invalid)?,
        )?;
        Ok(Element {
            page: *self,
            data,
            style: u32_at(self.arena, at + 8)? as usize,
            ruby_index: u32_at(self.arena, at + 12)? as usize,
        })
    }
    pub fn footnote(&self, index: u16) -> Result<(&'a [u8], &'a [u8])> {
        if index >= self.footnote_count() {
            return Err(Error::Invalid);
        }
        let at = self.footer(24)? + 2 + usize::from(index) * 288;
        Ok((range(self.wire, at, 32)?, range(self.wire, at + 32, 256)?))
    }
    pub fn link(&self, index: u16) -> Result<Link<'a>> {
        if index >= self.link_count() {
            return Err(Error::Invalid);
        }
        let at = self.footer(28)? + 2 + usize::from(index) * 264;
        Ok(Link {
            href: range(self.wire, at, 256)?,
            x: u16_at(self.wire, at + 256)? as i16,
            y: u16_at(self.wire, at + 258)? as i16,
            width: u16_at(self.wire, at + 260)? as i16,
            height: u16_at(self.wire, at + 262)? as i16,
        })
    }
    pub fn encoded_size(&self) -> Result<usize> {
        let mut size = self.wire.len();
        for i in 0..self.element_count() {
            let e = self.element(i)?;
            if e.tag()? == 4 {
                size -= e.data.len() - 13 - c_len(range(e.data, 13, e.data.len() - 13)?);
            }
        }
        Ok(size)
    }
    /// Canonical C++ reserialization (including bool and fixed-string cleanup).
    pub fn encode(&self, output: &mut [u8]) -> Result<usize> {
        let size = self.encoded_size()?;
        if output.len() < size {
            return Err(Error::NeedOutput(size));
        }
        output[..2].copy_from_slice(range(self.wire, 0, 2)?);
        let mut pos = 2;
        for i in 0..self.element_count() {
            let e = self.element(i)?;
            let len = if e.tag()? == 4 {
                13 + c_len(&e.data[13..])
            } else {
                e.data.len()
            };
            output[pos..pos + len].copy_from_slice(&e.data[..len]);
            if e.tag()? == 4 {
                output[pos + 12] = (len - 13) as u8;
            }
            pos += len;
        }
        let tail = &self.wire[self.footer(24)?..];
        output[pos..pos + tail.len()].copy_from_slice(tail);
        Ok(size)
    }
}

#[derive(Clone, Copy)]
pub struct Link<'a> {
    pub href: &'a [u8],
    pub x: i16,
    pub y: i16,
    pub width: i16,
    pub height: i16,
}
impl Link<'_> {
    pub fn contains(&self, x: i32, y: i32, slop: i32, min_width: i32) -> bool {
        let hs = i64::from(slop).max((i64::from(min_width) - i64::from(self.width)) / 2);
        let (x, y, slop) = (i64::from(x), i64::from(y), i64::from(slop));
        x >= i64::from(self.x) - hs
            && x < i64::from(self.x) + i64::from(self.width) + hs
            && y >= i64::from(self.y) - slop
            && y < i64::from(self.y) + i64::from(self.height) + slop
    }
}

#[derive(Clone, Copy)]
pub struct Element<'a> {
    page: Page<'a>,
    data: &'a [u8],
    style: usize,
    ruby_index: usize,
}
impl<'a> Element<'a> {
    pub fn tag(&self) -> Result<u8> {
        self.data.first().copied().ok_or(Error::Invalid)
    }
    pub fn position(&self) -> Result<(i16, i16)> {
        Ok((u16_at(self.data, 1)? as i16, u16_at(self.data, 3)? as i16))
    }
    pub fn record(&self) -> Result<section::Record> {
        let kind = match self.tag()? {
            1 | 2 => Kind::Position,
            3 => Kind::Rule,
            4 => Kind::DropCap,
            5 => Kind::Border,
            _ => return Err(Error::Invalid),
        };
        section::decode(kind, range(self.data, 1, kind.size())?)
            .map(|x| x.0)
            .map_err(|_| Error::Invalid)
    }
    pub fn drop_cap_text(&self) -> Result<&'a [u8]> {
        if self.tag()? != 4 {
            return Err(Error::Invalid);
        }
        let data = self.data.get(13..).ok_or(Error::Invalid)?;
        Ok(&data[..c_len(data)])
    }
    pub fn image(&self) -> Result<(&'a [u8], &'a [u8], i16, i16)> {
        if self.tag()? != 2 {
            return Err(Error::Invalid);
        }
        let mut r = Reader {
            data: self.data,
            pos: 5,
        };
        r.string()?;
        r.string()?;
        let size = r.record(Kind::ImageSize)?;
        let path = Reader {
            data: self.page.arena,
            pos: self.style,
        }
        .string()?;
        let src = Reader {
            data: self.page.arena,
            pos: self.ruby_index,
        }
        .string()?;
        Ok((path, src, size.values[0] as i16, size.values[1] as i16))
    }
    pub fn line(&self) -> Result<Line<'a>> {
        if self.tag()? != 1 {
            return Err(Error::Invalid);
        }
        let h = section::decode(Kind::TextHeader, range(self.data, 5, 7)?)
            .map_err(|_| Error::Invalid)?
            .0
            .values;
        Ok(Line {
            element: *self,
            count: h[0] as u16,
            focus: h[1] != 0,
            text_bytes: h[2] as u16,
            paragraph_start: h[3] as u16,
        })
    }
}
#[derive(Clone, Copy)]
pub struct Line<'a> {
    element: Element<'a>,
    pub count: u16,
    pub focus: bool,
    pub text_bytes: u16,
    pub paragraph_start: u16,
}
#[derive(Clone, Copy, Debug)]
pub struct Word<'a> {
    pub text: &'a [u8],
    pub source_start: u32,
    pub source_end: u32,
    pub x: i16,
    pub style: u8,
    pub discretionary_hyphen: bool,
    pub small_caps: bool,
    pub focus_boundary: u8,
    pub focus_suffix: u16,
    pub starts_paragraph: bool,
    pub font: i32,
}
impl<'a> Line<'a> {
    pub fn ruby_offsets(&self) -> Result<&'a [u8]> {
        if !self.has_ruby() {
            return Ok(&[]);
        }
        range(
            self.element.page.arena,
            self.element.ruby_index,
            usize::from(self.count) * 4,
        )
    }
    pub fn packed_data(&self) -> Result<&'a [u8]> {
        range(
            self.element.data,
            12,
            section::arena_size(self.count, self.focus, self.text_bytes),
        )
    }
    pub fn font_data(&self) -> Result<&'a [u8]> {
        let at = self.element.style + Kind::BlockStyle.size();
        if self.element.page.wire[at] == 0 {
            return Ok(&[]);
        }
        range(self.element.page.wire, at + 1, 12 + usize::from(self.count))
    }
    pub fn style_data(&self) -> Result<&'a [u8]> {
        range(
            self.element.page.wire,
            self.element.style,
            Kind::BlockStyle.size(),
        )
    }
    pub fn block_style(&self) -> Result<section::Record> {
        section::decode(
            Kind::BlockStyle,
            range(
                self.element.page.wire,
                self.element.style,
                Kind::BlockStyle.size(),
            )?,
        )
        .map(|r| r.0)
        .map_err(|_| Error::Invalid)
    }
    pub fn block_font(&self, section_font: i32) -> Result<i32> {
        let font = self.block_style()?.values[15] as i32;
        Ok(if font == 0 { section_font } else { font })
    }
    pub fn fonts(&self) -> Result<Option<[i32; 3]>> {
        let at = self.element.style + Kind::BlockStyle.size();
        if range(self.element.page.wire, at, 1)?[0] == 0 {
            return Ok(None);
        }
        Ok(Some([
            u32_at(self.element.page.wire, at + 1)? as i32,
            u32_at(self.element.page.wire, at + 5)? as i32,
            u32_at(self.element.page.wire, at + 9)? as i32,
        ]))
    }
    pub fn has_ruby(&self) -> bool {
        self.element.ruby_index != 0
    }
    pub fn ruby(&self, word: u16) -> Result<&'a [u8]> {
        if word >= self.count {
            return Err(Error::Invalid);
        }
        if !self.has_ruby() {
            return Ok(&[]);
        }
        let at = u32_at(
            self.element.page.arena,
            self.element.ruby_index + usize::from(word) * 4,
        )? as usize;
        Reader {
            data: self.element.page.arena,
            pos: at,
        }
        .string()
    }
    pub fn word(&self, word: u16, section_font: i32) -> Result<Word<'a>> {
        if word >= self.count {
            return Err(Error::Invalid);
        }
        let n = usize::from(self.count);
        let i = usize::from(word);
        let data = self.element.data.get(12..).ok_or(Error::Invalid)?;
        let offset = usize::from(u16_at(data, n * 8 + i * 2)?);
        let end = if i + 1 < n {
            usize::from(u16_at(data, n * 8 + (i + 1) * 2)?)
        } else {
            usize::from(self.text_bytes)
        };
        let styles = n * (12 + if self.focus { 2 } else { 0 });
        let raw_style = range(data, styles + i, 1)?[0];
        let text_at = n * (13 + if self.focus { 3 } else { 0 });
        let text_len = end.checked_sub(offset + 1).ok_or(Error::Invalid)?;
        let mut font = self.block_font(section_font)?;
        let mut small_caps = false;
        if let Some(fonts) = self.fonts()? {
            let slot = range(
                self.element.page.wire,
                self.element.style + Kind::BlockStyle.size() + 13 + i,
                1,
            )?[0];
            small_caps = slot & 128 != 0;
            if slot & 127 != 0 {
                font = *fonts
                    .get(usize::from((slot & 127) - 1))
                    .ok_or(Error::Invalid)?;
            }
        }
        Ok(Word {
            text: range(data, text_at + offset, text_len)?,
            source_start: u32_at(data, i * 8)?,
            source_end: u32_at(data, i * 8 + 4)?,
            x: u16_at(data, n * 10 + i * 2)? as i16,
            style: (raw_style & 127) | if small_caps { 128 } else { 0 },
            discretionary_hyphen: raw_style & 128 != 0,
            small_caps,
            focus_boundary: if self.focus {
                range(data, styles + n + i, 1)?[0]
            } else {
                0
            },
            focus_suffix: if self.focus {
                u16_at(data, n * 12 + i * 2)?
            } else {
                0
            },
            starts_paragraph: word == self.paragraph_start,
            font,
        })
    }
}
