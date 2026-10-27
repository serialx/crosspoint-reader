//! Build-owned storage for encoded lines/images; handles survive compaction and page carry.
use super::alloc::vec::Vec;
use super::{model::*, runtime::Payload};
use crate::{
    page::builder::{self, Content, PackedLine, Rubies, Writer},
    para::{paragraph::Span, paragraph_extract::Output},
    section::{Kind, Record},
};
use core::slice;

#[derive(Default)]
pub struct Storage {
    bytes: Vec<u8>,
    slots: Vec<Slot>,
    links: Vec<LineLink>,
}
struct Slot {
    start: usize,
    size: usize,
    links_start: usize,
    links_count: usize,
    live: bool,
    pub ruby: bool,
    margin: usize,
}

// Growth is build-scoped and fallible; capacity is retained across page completions.
fn grow<T>(v: &mut Vec<T>, needed: usize) -> Result {
    if needed > v.capacity() {
        let target = needed;
        v.try_reserve_exact(target - v.len())
            .map_err(|_| Error::Allocation)?;
    }
    Ok(())
}
impl Storage {
    pub fn encode(&mut self, page: &Page<Payload>) -> Result<&[u8]> {
        if page.elements.len() > u16::MAX as usize {
            return Err(Error::Allocation);
        }
        self.compact();
        let footer = 4 + page.links.len() * 264 + page.footnotes.len() * 288;
        let mut extra = 2 + footer;
        for e in &page.elements {
            if !matches!(
                e.content,
                super::Content::Line(_) | super::Content::Image(_)
            ) {
                let mut w = Writer { data: None, pos: 0 };
                builder::element(&mut w, e.x, e.y, &content(&e.content))
                    .map_err(|_| Error::Allocation)?;
                extra += w.pos;
            }
        }
        let needed = self
            .bytes
            .len()
            .checked_add(extra)
            .ok_or(Error::Allocation)?;
        grow(&mut self.bytes, needed)?;
        self.insert(0, 2);
        let mut cursor = 2;
        for e in &page.elements {
            match &e.content {
                super::Content::Line(p) | super::Content::Image(p) => {
                    let start = self.slots[p.index].start;
                    let size = self.slots[p.index].size;
                    self.bytes[cursor..start + size].rotate_right(size);
                    for (i, s) in self.slots.iter_mut().enumerate() {
                        if !s.live {
                            continue;
                        }
                        if i == p.index {
                            s.live = false;
                        } else if s.start >= cursor && s.start < start {
                            s.start += size;
                        }
                    }
                    self.bytes[cursor + 1..cursor + 3].copy_from_slice(&e.x.to_le_bytes());
                    self.bytes[cursor + 3..cursor + 5].copy_from_slice(&e.y.to_le_bytes());
                    cursor += size;
                }
                _ => {
                    let c = content(&e.content);
                    let mut size = Writer { data: None, pos: 0 };
                    builder::element(&mut size, e.x, e.y, &c).map_err(|_| Error::Allocation)?;
                    self.insert(cursor, size.pos);
                    let mut w = Writer {
                        data: Some(&mut self.bytes[cursor..cursor + size.pos]),
                        pos: 0,
                    };
                    builder::element(&mut w, e.x, e.y, &c).map_err(|_| Error::Allocation)?;
                    cursor += size.pos;
                }
            }
        }
        self.insert(cursor, footer);
        let length = cursor + footer;
        let mut builder = builder::Builder::from_elements(
            &mut self.bytes[..length],
            cursor,
            page.elements.len() as u16,
        );
        for note in &page.footnotes {
            builder
                .footnote(&note.number, &note.href)
                .map_err(|_| Error::Allocation)?;
        }
        for l in &page.links {
            builder
                .link(crate::page::Link {
                    href: &l.href,
                    x: l.x,
                    y: l.y,
                    width: l.width,
                    height: l.height,
                })
                .map_err(|_| Error::Allocation)?;
        }
        builder.finish().map_err(|_| Error::Allocation)?;
        Ok(&self.bytes[..length])
    }
    // Capacity is checked before any record is moved.
    fn insert(&mut self, at: usize, count: usize) {
        let len = self.bytes.len();
        self.bytes.resize(len + count, 0);
        self.bytes.copy_within(at..len, at + count);
        for s in &mut self.slots {
            if s.live && s.start >= at {
                s.start += count;
            }
        }
    }
    pub fn retire_page(&mut self, length: usize) {
        self.bytes.copy_within(length.., 0);
        self.bytes.truncate(self.bytes.len() - length);
        for s in &mut self.slots {
            if s.live {
                s.start -= length;
            }
        }
    }
    pub fn release_links(&mut self, index: usize) {
        self.slots[index].links_count = 0;
    }
    pub fn release(&mut self, index: usize) {
        self.slots[index].live = false;
    }
    fn compact(&mut self) {
        let (mut read, mut write, mut links) = (0, 0, 0);
        while let Some(id) = self
            .slots
            .iter()
            .enumerate()
            .filter(|(_, s)| s.live && s.start >= read)
            .min_by_key(|(_, s)| s.start)
            .map(|(i, _)| i)
        {
            let s = &mut self.slots[id];
            read = s.start + s.size;
            self.bytes.copy_within(s.start..read, write);
            self.links
                .copy_within(s.links_start..s.links_start + s.links_count, links);
            s.start = write;
            s.links_start = links;
            write += s.size;
            links += s.links_count;
        }
        self.bytes.truncate(write);
        self.links.truncate(links);
    }
    pub fn font_data(&self, p: &Payload) -> (i32, Option<[i32; 3]>, u32, bool, i16) {
        let s = &self.slots[p.index];
        let wire = &self.bytes[s.start..s.start + s.size];
        let style = s.margin - 6;
        let word_count = read16(wire, 5) as u32;
        let font = read32(wire, style + 24) as i32;
        let flags = style + Kind::BlockStyle.size();
        let fonts = if wire[flags] == 0 {
            None
        } else {
            Some([
                read32(wire, flags + 1) as i32,
                read32(wire, flags + 5) as i32,
                read32(wire, flags + 9) as i32,
            ])
        };
        let left = (read16(wire, style + 6) as i16).wrapping_add(read16(wire, style + 14) as i16);
        (font, fonts, word_count, s.ruby, left)
    }
    pub fn set_margin(&mut self, p: &Payload, left: i16) {
        let s = &self.slots[p.index];
        self.bytes[s.start + s.margin..s.start + s.margin + 2].copy_from_slice(&left.to_le_bytes());
    }
    pub fn links(&self, p: &Payload) -> &[LineLink] {
        let s = &self.slots[p.index];
        &self.links[s.links_start..s.links_start + s.links_count]
    }
    fn append(
        &mut self,
        content: Content<'_>,
        links: &[LineLink],
        mut slot: Slot,
    ) -> Result<usize> {
        let mut size = Writer { data: None, pos: 0 };
        builder::element(&mut size, 0, 0, &content).map_err(|_| Error::Allocation)?;
        let size = size.pos;
        if size > u32::MAX as usize {
            return Err(Error::Allocation);
        }
        if self.bytes.len() + size > self.bytes.capacity()
            || self.links.len() + links.len() > self.links.capacity()
        {
            self.compact();
        }
        let id = self
            .slots
            .iter()
            .position(|s| !s.live)
            .unwrap_or(self.slots.len());
        if id == self.slots.len() {
            grow(&mut self.slots, id + 1)?;
        }
        let end = self
            .bytes
            .len()
            .checked_add(size)
            .ok_or(Error::Allocation)?;
        grow(&mut self.bytes, end)?;
        let link_len = self.links.len() + links.len();
        grow(&mut self.links, link_len)?;
        let start = self.bytes.len();
        self.bytes.resize(end, 0);
        let mut writer = Writer {
            data: Some(&mut self.bytes[start..end]),
            pos: 0,
        };
        builder::element(&mut writer, 0, 0, &content).map_err(|_| Error::Allocation)?;
        slot.start = start;
        slot.size = size;
        slot.links_start = self.links.len();
        slot.links_count = links.len();
        self.links.extend_from_slice(links);
        if id == self.slots.len() {
            self.slots.push(slot);
        } else {
            self.slots[id] = slot;
        }
        Ok(id)
    }
    pub fn image(&mut self, path: &[u8], source: &[u8], width: i16, height: i16) -> Result<usize> {
        self.append(
            Content::Image {
                path,
                source,
                width,
                height,
            },
            &[],
            Slot::empty(),
        )
    }
    /// # Safety
    /// The packed graph is valid, aligned and disjoint from storage. It is borrowed from paragraph extraction until this call returns.
    pub unsafe fn line(&mut self, l: &Output) -> Result<usize> {
        let n = l.count as usize;
        let b = &*l.block;
        let ruby = RubySpans(view(l.ruby, n));
        let mut block = Record {
            kind: Kind::BlockStyle as u32,
            ..Record::default()
        };
        block.values[..16].copy_from_slice(&[
            b.alignment.into(),
            b.text_align_defined.into(),
            b.margin_top as u32,
            b.margin_bottom as u32,
            b.margin_left as u32,
            b.margin_right as u32,
            b.padding_top as u32,
            b.padding_bottom as u32,
            b.padding_left as u32,
            b.padding_right as u32,
            b.text_indent as u32,
            b.text_indent_defined.into(),
            b.rtl.into(),
            b.direction_defined.into(),
            b.character_spacing as u32,
            b.font_id as u32,
        ]);
        let fonts = if l.font_slots.is_null() {
            None
        } else {
            Some([*l.fonts, *l.fonts.add(1), *l.fonts.add(2)])
        };
        let packed = PackedLine {
            text: view(l.text.data, l.text.len as usize),
            offsets: view(l.offsets, n),
            positions: view(l.positions, n),
            styles: view(l.styles, n),
            boundaries: view(l.boundaries, if l.boundaries.is_null() { 0 } else { n }),
            suffixes: view(l.suffixes, if l.boundaries.is_null() { 0 } else { n }),
            sources: view(l.sources, n * 2),
            ruby: &ruby,
            paragraph_start: l.paragraph_start,
            block,
            fonts: fonts.map(|f| (f, view(l.font_slots, n))),
        };
        let mut slot = Slot::empty();
        slot.ruby = ruby.0.iter().any(|s| s.len != 0);
        let id = self.append(
            Content::Line(&packed),
            view(l.links.cast(), l.link_count as usize),
            slot,
        )?;
        // Cached style: alignment/defined, top/bottom, then the left margin.
        self.slots[id].margin = self.slots[id].size
            - (if fonts.is_some() { 13 + n } else { 1 })
            - Kind::BlockStyle.size()
            + 6;
        Ok(id)
    }
}
impl Slot {
    fn empty() -> Self {
        Self {
            start: 0,
            size: 0,
            links_start: 0,
            links_count: 0,
            live: true,
            ruby: false,
            margin: 0,
        }
    }
}
unsafe fn view<'a, T>(p: *const T, n: usize) -> &'a [T] {
    if n == 0 {
        &[]
    } else {
        slice::from_raw_parts(p, n)
    }
}
struct RubySpans<'a>(&'a [Span]);
impl Rubies for RubySpans<'_> {
    fn len(&self) -> usize {
        self.0.len()
    }
    fn get(&self, i: usize) -> &[u8] {
        unsafe { view(self.0[i].data, self.0[i].len as usize) }
    }
}

fn content(c: &super::Content<Payload>) -> Content<'_> {
    match c {
        super::Content::Rule { width, thickness } => Content::Rule {
            width: *width,
            thickness: *thickness,
        },
        super::Content::DropCap {
            font,
            scale256,
            style,
            text,
        } => Content::DropCap {
            font: *font,
            scale: *scale256,
            style: *style,
            text,
        },
        super::Content::Box {
            width,
            height,
            sides,
            shaded,
        } => Content::Border {
            width: *width,
            height: *height,
            sides: [
                sides[0].width,
                sides[0].style,
                sides[1].width,
                sides[1].style,
                sides[2].width,
                sides[2].style,
                sides[3].width,
                sides[3].style,
            ],
            shaded: *shaded,
        },
        _ => unreachable!(),
    }
}

fn read16(bytes: &[u8], at: usize) -> u16 {
    u16::from_le_bytes([bytes[at], bytes[at + 1]])
}
fn read32(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]])
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::page::{self, builder::Builder};

    fn image(s: &mut Storage, name: &[u8]) -> Payload {
        let index = s.image(name, b"source", 10, 20).unwrap();
        Payload { storage: s, index }
    }
    #[test]
    fn permutes_records_while_preserving_carried_payloads() {
        let mut s = Storage::default();
        let first = image(&mut s, b"first");
        let carried = image(&mut s, b"carried");
        let last = image(&mut s, b"last");
        let mut p = Page::default();
        p.elements.push(Element {
            x: 3,
            y: 4,
            content: super::super::Content::Image(last),
        });
        p.elements.push(Element {
            x: 5,
            y: 6,
            content: super::super::Content::Rule {
                width: 20,
                thickness: 2,
            },
        });
        p.elements.push(Element {
            x: 7,
            y: 8,
            content: super::super::Content::Image(first),
        });
        let mut expected = [0; 256];
        let mut b = Builder::new(&mut expected).unwrap();
        b.append(
            3,
            4,
            Content::Image {
                path: b"last",
                source: b"source",
                width: 10,
                height: 20,
            },
        )
        .unwrap();
        b.append(
            5,
            6,
            Content::Rule {
                width: 20,
                thickness: 2,
            },
        )
        .unwrap();
        b.append(
            7,
            8,
            Content::Image {
                path: b"first",
                source: b"source",
                width: 10,
                height: 20,
            },
        )
        .unwrap();
        let actual = s.encode(&p).unwrap();
        assert_eq!(actual, b.finish().unwrap());
        let n = actual.len();
        s.retire_page(n);
        drop(p);
        let index = carried.index;
        assert!(s.slots[index].live);
        let mut p = Page::default();
        p.elements.push(Element {
            x: 0,
            y: 0,
            content: super::super::Content::Image(carried),
        });
        let actual = s.encode(&p).unwrap();
        let info = page::inspect(actual).unwrap();
        assert_eq!(info.elements, 1);
        let n = actual.len();
        s.retire_page(n);
        drop(p);
        assert!(s.slots.iter().all(|s| !s.live));
        assert!(s.bytes.is_empty());
    }
    #[test]
    fn reuses_capacity_across_pages_and_abandoned_payloads() {
        let mut s = Storage::default();
        let mut capacity = None;
        for _ in 0..100 {
            let dead = image(&mut s, b"discard");
            let line = image(&mut s, b"keep");
            drop(dead);
            let mut p = Page::default();
            p.elements.push(Element {
                x: 0,
                y: 0,
                content: super::super::Content::Image(line),
            });
            let length = s.encode(&p).unwrap().len();
            s.retire_page(length);
            drop(p);
            let current = (s.bytes.capacity(), s.slots.capacity(), s.links.capacity());
            assert_eq!(*capacity.get_or_insert(current), current);
        }
    }
}
