//! A bounded page writer for the chapter sink. Append methods validate and size
//! before writing, so an error preserves both the cursor and existing records.
use super::{c_len, Error, Result};
use crate::section::{self, Kind, Record};

pub trait Rubies {
    fn len(&self) -> usize;
    fn is_empty(&self) -> bool {
        self.len() == 0
    }
    fn get(&self, index: usize) -> &[u8];
}
impl Rubies for &[&[u8]] {
    fn len(&self) -> usize {
        <[&[u8]]>::len(self)
    }
    fn get(&self, index: usize) -> &[u8] {
        self[index]
    }
}

pub struct PackedLine<'a> {
    pub text: &'a [u8],
    pub offsets: &'a [u16],
    pub positions: &'a [i16],
    pub styles: &'a [u8],
    pub boundaries: &'a [u8],
    pub suffixes: &'a [u16],
    pub sources: &'a [u32],
    pub ruby: &'a dyn Rubies,
    pub paragraph_start: u16,
    pub block: Record,
    pub fonts: Option<([i32; 3], &'a [u8])>,
}

pub enum Content<'a> {
    Line(&'a PackedLine<'a>),
    Image {
        path: &'a [u8],
        source: &'a [u8],
        width: i16,
        height: i16,
    },
    Rule {
        width: u16,
        thickness: u8,
    },
    DropCap {
        font: i32,
        scale: u16,
        style: u8,
        text: &'a [u8],
    },
    Border {
        width: u16,
        height: u16,
        sides: [u8; 8],
        shaded: bool,
    },
}

pub(super) struct Writer<'a> {
    pub data: Option<&'a mut [u8]>,
    pub pos: usize,
}
impl Writer<'_> {
    pub fn bytes(&mut self, data: &[u8]) -> Result<()> {
        let end = self.pos.checked_add(data.len()).ok_or(Error::Invalid)?;
        if let Some(ref mut out) = self.data {
            out.get_mut(self.pos..end)
                .ok_or(Error::NeedOutput(end))?
                .copy_from_slice(data);
        }
        self.pos = end;
        Ok(())
    }
    pub fn record(&mut self, kind: Kind, values: &[u32]) -> Result<()> {
        let mut record = Record {
            kind: kind as u32,
            ..Record::default()
        };
        record.values[..values.len()].copy_from_slice(values);
        let mut data = [0; section::HEADER_SIZE];
        let len = section::encode(&record, &mut data).map_err(|_| Error::Invalid)?;
        section::decode(kind, &data[..len]).map_err(|_| Error::Invalid)?;
        self.bytes(&data[..len])
    }
    pub fn string(&mut self, s: &[u8]) -> Result<()> {
        let len = u32::try_from(s.len()).map_err(|_| Error::Invalid)?;
        self.record(Kind::U32, &[len])?;
        self.bytes(s)
    }
}

fn line(w: &mut Writer<'_>, l: &PackedLine<'_>) -> Result<()> {
    let n = l.offsets.len();
    let focus = !l.boundaries.is_empty();
    if n > 10000
        || l.positions.len() != n
        || l.styles.len() != n
        || l.sources.len() != n * 2
        || (!l.ruby.is_empty() && l.ruby.len() != n)
        || (focus && (l.boundaries.len() != n || l.suffixes.len() != n))
        || l.block.kind != Kind::BlockStyle as u32
    {
        return Err(Error::Invalid);
    }
    // The existing TextBlock constructor copies each NUL-terminated packed word,
    // excluding unused gaps and bytes following its first NUL.
    let word = |i: usize| -> Result<&[u8]> {
        let tail = l
            .text
            .get(usize::from(l.offsets[i])..)
            .ok_or(Error::Invalid)?;
        let len = c_len(tail);
        if len == tail.len() {
            return Err(Error::Invalid);
        }
        Ok(&tail[..len + 1])
    };
    let mut total = 0usize;
    for i in 0..n {
        total = total.checked_add(word(i)?.len()).ok_or(Error::Invalid)?;
    }
    if total > u16::MAX as usize {
        return Err(Error::Invalid);
    }
    w.record(
        Kind::TextHeader,
        &[
            n as u32,
            focus as u32,
            total as u32,
            u32::from(l.paragraph_start),
        ],
    )?;
    for source in l.sources {
        w.bytes(&source.to_le_bytes())?;
    }
    let mut offset = 0u16;
    for i in 0..n {
        w.bytes(&offset.to_le_bytes())?;
        offset += word(i)?.len() as u16;
    }
    for x in l.positions {
        w.bytes(&x.to_le_bytes())?;
    }
    if focus {
        for x in l.suffixes {
            w.bytes(&x.to_le_bytes())?;
        }
    }
    w.bytes(l.styles)?;
    if focus {
        w.bytes(l.boundaries)?;
    }
    for i in 0..n {
        w.bytes(word(i)?)?;
    }
    for i in 0..n {
        w.string(if l.ruby.is_empty() {
            &[]
        } else {
            l.ruby.get(i)
        })?;
    }
    let mut style = l.block.values;
    for i in [1, 11, 12, 13] {
        style[i] = u32::from(style[i] != 0);
    }
    w.record(Kind::BlockStyle, &style)?;
    w.record(Kind::WordFontFlag, &[u32::from(l.fonts.is_some())])?;
    if let Some((fonts, slots)) = l.fonts {
        if slots.len() != n
            || slots
                .iter()
                .any(|&s| s & 127 > 3 || (s & 127 != 0 && fonts[usize::from((s & 127) - 1)] == 0))
        {
            return Err(Error::Invalid);
        }
        for font in fonts {
            w.bytes(&font.to_le_bytes())?;
        }
        w.bytes(slots)?;
    }
    Ok(())
}
fn element(w: &mut Writer<'_>, x: i16, y: i16, content: &Content<'_>) -> Result<()> {
    let x = x as u32;
    let y = y as u32;
    match content {
        Content::Line(l) => {
            w.bytes(&[1])?;
            w.record(Kind::Position, &[x, y])?;
            line(w, l)
        }
        Content::Image {
            path,
            source,
            width,
            height,
        } => {
            w.bytes(&[2])?;
            w.record(Kind::Position, &[x, y])?;
            w.string(path)?;
            w.string(source)?;
            w.record(Kind::ImageSize, &[*width as u32, *height as u32])
        }
        Content::Rule { width, thickness } => {
            w.bytes(&[3])?;
            w.record(
                Kind::Rule,
                &[x, y, u32::from(*width), u32::from(*thickness)],
            )
        }
        Content::DropCap {
            font,
            scale,
            style,
            text,
        } => {
            let text = &text[..c_len(text).min(12)];
            w.bytes(&[4])?;
            w.record(
                Kind::DropCap,
                &[
                    x,
                    y,
                    *font as u32,
                    u32::from(*scale),
                    u32::from(*style),
                    text.len() as u32,
                ],
            )?;
            w.bytes(text)
        }
        Content::Border {
            width,
            height,
            sides,
            shaded,
        } => {
            w.bytes(&[5])?;
            w.record(
                Kind::Border,
                &[
                    x,
                    y,
                    u32::from(*width),
                    u32::from(*height),
                    u32::from(sides[0]),
                    u32::from(sides[1]),
                    u32::from(sides[2]),
                    u32::from(sides[3]),
                    u32::from(sides[4]),
                    u32::from(sides[5]),
                    u32::from(sides[6]),
                    u32::from(sides[7]),
                    u32::from(*shaded),
                ],
            )
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Phase {
    Elements,
    Footnotes,
    Links,
    Finished,
}
pub struct Builder<'a> {
    output: &'a mut [u8],
    pos: usize,
    phase: Phase,
    count: u16,
    footer: usize,
}
impl<'a> Builder<'a> {
    pub fn new(output: &'a mut [u8]) -> Result<Self> {
        if output.len() < 2 {
            return Err(Error::NeedOutput(2));
        }
        output[..2].fill(0);
        Ok(Self {
            output,
            pos: 2,
            phase: Phase::Elements,
            count: 0,
            footer: 0,
        })
    }
    pub fn append(&mut self, x: i16, y: i16, content: Content<'_>) -> Result<()> {
        if self.phase != Phase::Elements || self.count == u16::MAX {
            return Err(Error::Invalid);
        }
        let mut size = Writer {
            data: None,
            pos: self.pos,
        };
        element(&mut size, x, y, &content)?;
        if size.pos > self.output.len() {
            return Err(Error::NeedOutput(size.pos));
        }
        let mut writer = Writer {
            data: Some(self.output),
            pos: self.pos,
        };
        element(&mut writer, x, y, &content)?;
        self.pos = writer.pos;
        self.count += 1;
        self.output[..2].copy_from_slice(&self.count.to_le_bytes());
        Ok(())
    }
    fn ensure(&self, extra: usize) -> Result<()> {
        let size = self.pos.checked_add(extra).ok_or(Error::Invalid)?;
        if size > self.output.len() {
            Err(Error::NeedOutput(size))
        } else {
            Ok(())
        }
    }
    pub fn footnote(&mut self, number: &[u8; 32], href: &[u8; 256]) -> Result<()> {
        if !matches!(self.phase, Phase::Elements | Phase::Footnotes) {
            return Err(Error::Invalid);
        }
        if self.phase == Phase::Footnotes && self.count == 16 {
            return Err(Error::Invalid);
        }
        self.ensure(288 + if self.phase == Phase::Elements { 2 } else { 0 })?;
        self.start_footnotes();
        self.output[self.pos..self.pos + 32].copy_from_slice(number);
        self.output[self.pos + 32..self.pos + 288].copy_from_slice(href);
        self.output[self.pos + 31] = 0;
        self.output[self.pos + 287] = 0;
        self.pos += 288;
        self.count += 1;
        self.output[self.footer..self.footer + 2].copy_from_slice(&self.count.to_le_bytes());
        Ok(())
    }
    fn start_footnotes(&mut self) {
        if self.phase == Phase::Elements {
            self.phase = Phase::Footnotes;
            self.footer = self.pos;
            self.output[self.pos..self.pos + 2].fill(0);
            self.pos += 2;
            self.count = 0;
        }
    }
    fn start_links(&mut self) {
        self.start_footnotes();
        if self.phase == Phase::Footnotes {
            self.phase = Phase::Links;
            self.footer = self.pos;
            self.output[self.pos..self.pos + 2].fill(0);
            self.pos += 2;
            self.count = 0;
        }
    }
    pub fn link(&mut self, link: super::Link<'_>) -> Result<()> {
        if self.phase == Phase::Finished
            || (self.phase == Phase::Links && self.count == 32)
            || link.href.len() != 256
            || link.href[0] == 0
            || c_len(link.href) == 256
            || link.width <= 0
            || link.height <= 0
        {
            return Err(Error::Invalid);
        }
        self.ensure(
            264 + match self.phase {
                Phase::Elements => 4,
                Phase::Footnotes => 2,
                _ => 0,
            },
        )?;
        self.start_links();
        self.output[self.pos..self.pos + 256].copy_from_slice(link.href);
        self.output[self.pos + 255] = 0;
        let mut w = Writer {
            data: Some(self.output),
            pos: self.pos + 256,
        };
        w.record(
            Kind::LinkGeometry,
            &[
                link.x as u32,
                link.y as u32,
                link.width as u32,
                link.height as u32,
            ],
        )?;
        self.pos = w.pos;
        self.count += 1;
        self.output[self.footer..self.footer + 2].copy_from_slice(&self.count.to_le_bytes());
        Ok(())
    }
    pub fn finish(&mut self) -> Result<&[u8]> {
        if self.phase != Phase::Finished {
            self.ensure(match self.phase {
                Phase::Elements => 4,
                Phase::Footnotes => 2,
                _ => 0,
            })?;
            self.start_links();
            self.phase = Phase::Finished;
        }
        Ok(&self.output[..self.pos])
    }
}
