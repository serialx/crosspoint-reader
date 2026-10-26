//! Package document passes, including manifest lookup and Print short counts.
extern crate alloc;
use super::{
    cache::{self, Io, Nodes, Scratch},
    opf, Parser, ParserKind, RecordKind, Result,
};
use alloc::vec::Vec;

pub trait Sink {
    fn io(&self) -> Option<&Io>;
    fn spine(&mut self, href: &[u8]) -> Result<()>;
    fn toc(&mut self, title: &[u8], href: &[u8], anchor: &[u8], level: u8) -> Result<()>;
}
pub struct Simple {
    pub parser: Parser,
    remaining: usize,
    encryption: bool,
    pub protection: bool,
    completed: bool,
    failed: bool,
}
impl Simple {
    pub fn new(kind: ParserKind, size: usize) -> Self {
        Self {
            parser: Parser::new(kind),
            remaining: size,
            encryption: kind == ParserKind::Encryption,
            protection: false,
            completed: false,
            failed: false,
        }
    }
    fn drain(&mut self, sink: &mut impl Sink) -> Result<()> {
        loop {
            let record = self.parser.next();
            match record.kind {
                RecordKind::NeedInput => return Ok(()),
                RecordKind::Done => {
                    self.completed = true;
                    return Ok(());
                }
                RecordKind::Error => return Err(record.error),
                RecordKind::Protection => self.protection = true,
                RecordKind::Toc => {
                    sink.toc(record.value, record.href, record.anchor, record.level)?
                }
                RecordKind::ContainerPath => {}
            }
        }
    }
    pub fn write(&mut self, bytes: &[u8], sink: &mut impl Sink) -> usize {
        if self.failed || (self.encryption && (self.completed || bytes.len() > i32::MAX as usize)) {
            return 0;
        }
        let mut offset = 0;
        while offset < bytes.len() {
            let count = if self.encryption {
                bytes.len() - offset
            } else {
                (bytes.len() - offset).min(1024)
            };
            let input = bytes.get(offset..offset + count).unwrap_or(&[]);
            let consumed = self
                .parser
                .feed(input, !self.encryption && self.remaining == count);
            if self.drain(sink).is_err() || consumed == 0 || (!self.encryption && consumed != count)
            {
                self.failed = true;
                return 0;
            }
            offset += consumed;
            self.remaining = self.remaining.wrapping_sub(consumed);
        }
        bytes.len()
    }
    pub fn finish(&mut self, sink: &mut impl Sink) -> bool {
        if self.failed || self.completed {
            return false;
        }
        self.parser.feed(&[], true);
        if self.drain(sink).is_err() {
            self.failed = true;
            return false;
        }
        true
    }
}
#[derive(Clone, Copy)]
pub(super) struct Item {
    hash: u32,
    len: u16,
    offset: u32,
}
#[inline(never)]
fn less(a: Item, b: Item) -> bool {
    (a.hash, a.len) < (b.hash, b.len)
}
fn hash(value: &[u8]) -> u32 {
    value.iter().fold(2166136261u32, |h, b| {
        (h ^ u32::from(*b)).wrapping_mul(16777619)
    })
}
pub struct Opf {
    pub parser: opf::Parser,
    remaining: usize,
    metadata_only: bool,
    complete: bool,
    failed: bool,
    indexed: bool,
    items: Nodes<Item>,
    pub css: Vec<Scratch>,
}
impl Opf {
    pub fn new(size: usize, metadata_only: bool, has_cache: bool) -> Self {
        Self {
            parser: opf::Parser::new(metadata_only, has_cache),
            remaining: size,
            metadata_only,
            complete: false,
            failed: false,
            indexed: false,
            items: Nodes::default(),
            css: Vec::new(),
        }
    }
    fn drain(&mut self, sink: &mut impl Sink) -> Result<()> {
        loop {
            let record = self.parser.next();
            match record.id {
                opf::NEED_INPUT | opf::DONE => return Ok(()),
                opf::ERROR => return Err(record.error),
                opf::COMPLETE => self.complete = true,
                opf::CSS => {
                    let mut value = Scratch::default();
                    value.set(record.href)?;
                    self.css.try_reserve(1).map_err(|_| 1u32)?;
                    self.css.push(value);
                }
                opf::MANIFEST_BEGIN => {
                    if let Some(io) = sink.io() {
                        io.control(cache::ITEMS, cache::OPEN_WRITE);
                    }
                }
                opf::SPINE_BEGIN => {
                    if let Some(io) = sink.io() {
                        io.control(cache::ITEMS, cache::OPEN_READ);
                    }
                    if !self.items.is_empty() {
                        self.items.sort(less)?;
                        self.indexed = true;
                    }
                }
                opf::GUIDE_BEGIN => {
                    if let Some(io) = sink.io() {
                        io.control(cache::ITEMS, cache::OPEN_READ);
                    }
                }
                opf::STORE_END => {
                    if let Some(io) = sink.io() {
                        if io.control(cache::ITEMS, cache::IS_OPEN) != 0 {
                            io.control(cache::ITEMS, cache::CLOSE);
                        }
                    }
                }
                opf::MANIFEST => {
                    if let Some(io) = sink.io() {
                        if io.control(cache::ITEMS, cache::IS_OPEN) != 0 {
                            self.items.push(Item {
                                hash: hash(record.value),
                                len: record.value.len() as u16,
                                offset: io.control(cache::ITEMS, cache::POSITION),
                            })?;
                            io.string(cache::ITEMS, record.value)?;
                            io.string(cache::ITEMS, record.href)?;
                        }
                    }
                }
                opf::SPINE => {
                    if let Some(io) = sink.io() {
                        let mut href = Scratch::default();
                        let mut found = false;
                        if self.indexed {
                            let target = Item {
                                hash: hash(record.value),
                                len: record.value.len() as u16,
                                offset: 0,
                            };
                            let mut at = self.items.lower_bound(target, less)?;
                            while let Ok(item) = self.items.get(at) {
                                if item.hash != target.hash {
                                    break;
                                }
                                io.seek(cache::ITEMS, item.offset)?;
                                let mut id = Scratch::default();
                                io.read_string(cache::ITEMS, &mut id)?;
                                if id.as_slice() == record.value {
                                    io.read_string(cache::ITEMS, &mut href)?;
                                    found = true;
                                    break;
                                }
                                at += 1;
                            }
                        } else {
                            io.seek(cache::ITEMS, 0)?;
                            let mut id = Scratch::default();
                            while io.control(cache::ITEMS, cache::POSITION)
                                < io.control(cache::ITEMS, cache::SIZE)
                            {
                                io.read_string(cache::ITEMS, &mut id)?;
                                io.read_string(cache::ITEMS, &mut href)?;
                                if id.as_slice() == record.value {
                                    found = true;
                                    break;
                                }
                            }
                        }
                        if found {
                            sink.spine(&href)?;
                        }
                    }
                }
                _ => {}
            }
        }
    }
    pub fn write(&mut self, bytes: &[u8], sink: &mut impl Sink) -> usize {
        if self.failed {
            return 0;
        }
        let mut offset = 0;
        while offset < bytes.len() {
            let count = (bytes.len() - offset).min(1024);
            let consumed = self.parser.feed(
                bytes.get(offset..offset + count).unwrap_or(&[]),
                self.remaining == count,
            );
            if self.drain(sink).is_err() || consumed != count {
                self.failed = true;
                return 0;
            }
            offset += count;
            self.remaining = self.remaining.wrapping_sub(count);
            if self.metadata_only && self.complete {
                return if offset < bytes.len() {
                    offset
                } else {
                    bytes.len() - 1
                };
            }
        }
        bytes.len()
    }
}
