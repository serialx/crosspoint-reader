//! Bounded sizing pass for file-backed pages. Full validation follows the final read.
use super::{Error, Info, Result, ELEMENT, HEADER};
use crate::section::{self, Diagnostic, DiagnosticEvent as D, Kind};

struct Probe<'d, R> {
    detail: &'d mut Diagnostic,
    read: R,
    size: usize,
    pos: usize,
    start: usize,
    valid: usize,
    cache: [u8; 128],
}
impl<R: FnMut(usize, &mut [u8]) -> bool> Probe<'_, R> {
    fn skip(&mut self, n: usize) -> Result<()> {
        self.pos = self
            .pos
            .checked_add(n)
            .filter(|&v| v <= self.size)
            .ok_or(Error::NeedInput)?;
        Ok(())
    }
    fn record(&mut self, kind: Kind) -> Result<section::Record> {
        let n = kind.size();
        if n > self.size.saturating_sub(self.pos) {
            return Err(Error::NeedInput);
        }
        if self.pos < self.start || self.pos + n > self.start + self.valid {
            self.start = self.pos;
            self.valid = self.cache.len().min(self.size - self.pos);
            if !(self.read)(self.pos, &mut self.cache[..self.valid]) {
                return Err(Error::NeedInput);
            }
        }
        let offset = self.pos - self.start;
        self.pos += n;
        section::decode_detailed(kind, &self.cache[offset..offset + n], self.detail)
            .map(|v| v.0)
            .map_err(|_| Error::Invalid)
    }
    fn string(&mut self) -> Result<usize> {
        let n = self.record(Kind::U32)?.values[0] as usize;
        self.skip(n)?;
        Ok(n)
    }
}

/// Sizing only: no payload loans, allocations or writes. The final byte span
/// must still pass `decode_in_place`, including if the source changed meanwhile.
pub fn probe(size: usize, read: impl FnMut(usize, &mut [u8]) -> bool) -> Result<Info> {
    probe_detailed(size, read, &mut Diagnostic::default())
}

pub fn probe_detailed(
    size: usize,
    read: impl FnMut(usize, &mut [u8]) -> bool,
    detail: &mut Diagnostic,
) -> Result<Info> {
    if size > u32::MAX as usize || size > isize::MAX as usize {
        return Err(Error::Invalid);
    }
    let mut r = Probe {
        detail,
        read,
        size,
        pos: 0,
        start: 0,
        valid: 0,
        cache: [0; 128],
    };
    let count = r.record(Kind::U16)?.values[0] as usize;
    let mut extra = HEADER + count * ELEMENT;
    for _ in 0..count {
        let tag = r.record(Kind::ElementTag)?.values[0];
        (|| {
            match tag {
                1 => {
                    r.skip(Kind::Position.size())?;
                    let h = r.record(Kind::TextHeader)?.values;
                    let n = h[0] as u16;
                    let len = section::arena_size(n, h[1] != 0, h[2] as u16);
                    r.skip(len)
                        .inspect_err(|_| r.detail.set(D::ArenaRead, len as u32, 0))?;
                    let mut ruby_bytes = 0usize;
                    for _ in 0..n {
                        ruby_bytes = ruby_bytes.checked_add(r.string()?).ok_or(Error::Invalid)?;
                    }
                    if ruby_bytes != 0 {
                        extra = extra
                            .checked_add(ruby_bytes)
                            .and_then(|v| v.checked_add(usize::from(n) * 9))
                            .ok_or(Error::Invalid)?;
                    }
                    r.skip(Kind::BlockStyle.size())?;
                    if r.record(Kind::WordFontFlag)?.values[0] != 0 {
                        r.skip(12 + usize::from(n))
                            .inspect_err(|_| r.detail.set(D::WordFonts, 0, 0))?;
                    }
                }
                2 => {
                    r.skip(Kind::Position.size())?;
                    for _ in 0..2 {
                        extra = extra
                            .checked_add(r.string()?)
                            .and_then(|v| v.checked_add(5))
                            .ok_or(Error::Invalid)?;
                    }
                    r.skip(Kind::ImageSize.size())?;
                }
                3 => r.skip(Kind::Rule.size())?,
                4 => {
                    let n = r.record(Kind::DropCap)?.values[5] as usize;
                    r.skip(n).inspect_err(|_| r.detail.set(D::DropCap, 0, 0))?;
                }
                5 => r.skip(Kind::Border.size())?,
                _ => return Err(Error::Invalid),
            }
            Ok(())
        })()
        .inspect_err(|_| {
            if tag == 1 {
                r.detail.element = tag;
            }
        })?;
    }
    let footnotes = r.record(Kind::FootnoteCount)?.values[0] as u16;
    r.skip(usize::from(footnotes) * 288).inspect_err(|_| {
        r.detail.set(
            D::FootnoteRead,
            (r.size.saturating_sub(r.pos) / 288) as u32,
            0,
        );
    })?;
    let links = r.record(Kind::LinkCount)?.values[0] as u16;
    r.skip(usize::from(links) * 264).inspect_err(|_| {
        let remaining = r.size.saturating_sub(r.pos);
        let event = if remaining % 264 < 256 {
            D::LinkRead
        } else {
            D::LinkGeometry
        };
        r.detail.set(event, (remaining / 264) as u32, 0);
    })?;
    let arena_size = extra
        .checked_add(r.pos)
        .filter(|&v| v <= u32::MAX as usize && v <= isize::MAX as usize)
        .ok_or(Error::Invalid)?;
    Ok(Info {
        arena_size,
        consumed: r.pos,
        elements: count as u16,
        footnotes,
        links,
    })
}
