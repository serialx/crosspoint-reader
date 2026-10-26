//! Streaming package semantics. Storage and cache writes remain with the caller.
#![deny(clippy::indexing_slicing)]
extern crate alloc;

pub mod cache;
pub mod facade;
mod href;
pub mod opf;
mod simple;
use crate::xml::{Event, Kind, Parser as Xml};
use alloc::vec::Vec;
use simple::Simple;

pub type Result<T> = core::result::Result<T, u32>;
pub fn append(out: &mut Vec<u8>, bytes: &[u8]) -> Result<()> {
    out.try_reserve(bytes.len()).map_err(|_| 1u32)?;
    out.extend_from_slice(bytes);
    Ok(())
}
pub fn assign(out: &mut Vec<u8>, bytes: &[u8]) -> Result<()> {
    out.clear();
    append(out, bytes)
}
pub fn field<'a>(event: &'a Event<'_>, index: usize) -> &'a [u8] {
    event
        .fields
        .get(index)
        .and_then(|f| event.data.get(f.range.clone()))
        .unwrap_or(&[])
}
pub fn local(name: &[u8]) -> &[u8] {
    name.iter()
        .position(|&c| c == b':')
        .and_then(|i| name.get(i + 1..))
        .unwrap_or(name)
}

pub fn attr<'a>(event: &'a Event<'_>, name: &[u8], use_local: bool) -> Option<&'a [u8]> {
    for index in (1..event.fields.len()).step_by(2) {
        let key = field(event, index);
        if (if use_local { local(key) } else { key }) == name {
            return Some(field(event, index + 1));
        }
    }
    None
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum ParserKind {
    Container = 1,
    Ncx = 2,
    Nav = 3,
    Encryption = 4,
}
#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum RecordKind {
    NeedInput = 0,
    Done = 1,
    Error = 2,
    ContainerPath = 3,
    Toc = 4,
    Protection = 5,
}
pub struct Record<'a> {
    pub kind: RecordKind,
    pub error: u32,
    pub value: &'a [u8],
    pub href: &'a [u8],
    pub anchor: &'a [u8],
    pub level: u8,
}
pub struct Parser {
    xml: Xml,
    state: Simple,
    failed: bool,
}
impl Parser {
    pub const fn new(kind: ParserKind) -> Self {
        Self {
            xml: Xml::new(),
            state: Simple::new(kind),
            failed: false,
        }
    }
    pub fn value(&self) -> &[u8] {
        &self.state.label
    }
    pub fn set_base(&mut self, base: &[u8]) -> Result<()> {
        self.state.set_base(base)
    }
    pub fn feed(&mut self, bytes: &[u8], final_input: bool) -> usize {
        if self.failed {
            0
        } else {
            self.xml.feed(bytes, final_input)
        }
    }
    #[allow(clippy::should_implement_trait)]
    pub fn next(&mut self) -> Record<'_> {
        let (kind, error) = loop {
            if self.failed {
                break (RecordKind::Error, 1);
            }
            let event = self.xml.next();
            match event.kind {
                Kind::Error => {
                    self.failed = true;
                    break (RecordKind::Error, event.error);
                }
                Kind::NeedInput => break (RecordKind::NeedInput, 0),
                Kind::Done => break (RecordKind::Done, 0),
                Kind::Stopped => break (RecordKind::Error, 35),
                _ => match self.state.event(&event) {
                    Ok(Some(kind)) => break (kind, 0),
                    Ok(None) => {}
                    Err(error) => {
                        self.failed = true;
                        break (RecordKind::Error, error);
                    }
                },
            }
        };
        Record {
            kind,
            error,
            value: &self.state.label,
            href: &self.state.href,
            anchor: &self.state.anchor,
            level: self.state.depth,
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) enum Element {
    Unknown,
    Package,
    Metadata,
    Title,
    Creator,
    Language,
    Identifier,
    Meta,
    Manifest,
    Spine,
    Guide,
    Item,
    Itemref,
    Reference,
    Container,
    Rootfiles,
    Rootfile,
    Ncx,
    NavMap,
    NavPoint,
    NavLabel,
    Text,
    Content,
    Html,
    Body,
    Nav,
    Ol,
    Li,
    A,
    EncryptionMethod,
}
impl Element {
    fn parse(name: &[u8]) -> Self {
        const NAMES: &[(&[u8], Element)] = &[
            (b"package", Element::Package),
            (b"metadata", Element::Metadata),
            (b"title", Element::Title),
            (b"creator", Element::Creator),
            (b"language", Element::Language),
            (b"identifier", Element::Identifier),
            (b"meta", Element::Meta),
            (b"manifest", Element::Manifest),
            (b"spine", Element::Spine),
            (b"guide", Element::Guide),
            (b"item", Element::Item),
            (b"itemref", Element::Itemref),
            (b"reference", Element::Reference),
            (b"container", Element::Container),
            (b"rootfiles", Element::Rootfiles),
            (b"rootfile", Element::Rootfile),
            (b"ncx", Element::Ncx),
            (b"navMap", Element::NavMap),
            (b"navPoint", Element::NavPoint),
            (b"navLabel", Element::NavLabel),
            (b"text", Element::Text),
            (b"content", Element::Content),
            (b"html", Element::Html),
            (b"body", Element::Body),
            (b"nav", Element::Nav),
            (b"ol", Element::Ol),
            (b"li", Element::Li),
            (b"a", Element::A),
            (b"EncryptionMethod", Element::EncryptionMethod),
        ];
        NAMES
            .iter()
            .find(|(text, _)| *text == name)
            .map_or(Self::Unknown, |(_, tag)| *tag)
    }
}
