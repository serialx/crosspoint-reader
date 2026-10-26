//! OPF semantic state; the caller retains the SD manifest store and its index.
extern crate alloc;
use super::Element;
use super::{append, assign, attr, field, href, local, Event, Kind, Result};
use crate::xml::Parser as Xml;
use alloc::vec::Vec;

pub const TITLE: u32 = 0;
pub const AUTHOR: u32 = 1;
pub const LANGUAGE: u32 = 2;
pub const ISBN: u32 = 3;
pub const ASIN: u32 = 4;
pub const SERIES: u32 = 5;
pub const NCX: u32 = 6;
pub const NAV: u32 = 7;
pub const COVER: u32 = 8;
pub const GUIDE: u32 = 9;
pub const TEXT: u32 = 10;
pub const MANIFEST: u32 = 20;
pub const SPINE: u32 = 21;
pub const MANIFEST_BEGIN: u32 = 22;
pub const SPINE_BEGIN: u32 = 23;
pub const GUIDE_BEGIN: u32 = 24;
pub const STORE_END: u32 = 25;
pub const CSS: u32 = 26;
pub const COMPLETE: u32 = 27;
pub const NEED_INPUT: u32 = 28;
pub const DONE: u32 = 29;
pub const ERROR: u32 = 30;

#[derive(Clone, Copy, PartialEq, Eq)]
enum State {
    Start,
    Package,
    Metadata,
    Title,
    Author,
    Language,
    Identifier,
    MetaText,
    Manifest,
    Spine,
    Guide,
}
struct Collection {
    id: Vec<u8>,
    title: Vec<u8>,
    index: Option<f32>,
    series: bool,
}
#[derive(Clone, Copy, PartialEq, Eq)]
enum MetaProperty {
    Unknown,
    Collection,
    Type,
    Position,
}
impl MetaProperty {
    fn parse(value: &[u8]) -> Self {
        const VALUES: &[(&[u8], MetaProperty)] = &[
            (b"belongs-to-collection", MetaProperty::Collection),
            (b"collection-type", MetaProperty::Type),
            (b"group-position", MetaProperty::Position),
        ];
        VALUES
            .iter()
            .find(|(name, _)| equal_ci(value, name))
            .map_or(Self::Unknown, |(_, kind)| *kind)
    }
}
struct Metadata {
    values: [Vec<u8>; 11],
    index: Option<f32>,
    cover_id: Vec<u8>,
    identifier: Vec<u8>,
    isbn_scheme: bool,
    asin_scheme: bool,
    meta_text: Vec<u8>,
    property: MetaProperty,
    meta_id: Vec<u8>,
    calibre: Vec<u8>,
    calibre_index: Option<f32>,
    collections: Vec<Collection>,
    space: bool,
    separator: bool,
}
impl Metadata {
    const fn new() -> Self {
        Self {
            // Explicit elements avoid repeated-array temporaries on the C3 stack.
            values: [
                Vec::new(),
                Vec::new(),
                Vec::new(),
                Vec::new(),
                Vec::new(),
                Vec::new(),
                Vec::new(),
                Vec::new(),
                Vec::new(),
                Vec::new(),
                Vec::new(),
            ],
            index: None,
            cover_id: Vec::new(),
            identifier: Vec::new(),
            isbn_scheme: false,
            asin_scheme: false,
            meta_text: Vec::new(),
            property: MetaProperty::Unknown,
            meta_id: Vec::new(),
            calibre: Vec::new(),
            calibre_index: None,
            collections: Vec::new(),
            space: false,
            separator: false,
        }
    }
    fn get(&self, id: u32) -> &[u8] {
        self.values
            .get(id as usize)
            .map(Vec::as_slice)
            .unwrap_or(&[])
    }
    fn put(&mut self, id: u32, value: &[u8]) -> Result<()> {
        assign(self.values.get_mut(id as usize).ok_or(4u32)?, value)
    }
}
fn bounded(bytes: &[u8]) -> &[u8] {
    bytes.get(..bytes.len().min(512)).unwrap_or(&[])
}
#[inline(never)]
fn equal_ci(value: &[u8], other: &[u8]) -> bool {
    value.eq_ignore_ascii_case(other)
}
fn contains_ci(value: &[u8], part: &[u8]) -> bool {
    value.windows(part.len()).any(|w| equal_ci(w, part))
}
fn prefix_offset(value: &[u8], name: &[u8]) -> Option<usize> {
    if value.get(..name.len()).is_some_and(|v| equal_ci(v, name)) {
        return Some(name.len());
    }
    if value.get(..4).is_some_and(|v| equal_ci(v, b"urn:"))
        && value
            .get(4..4 + name.len())
            .is_some_and(|v| equal_ci(v, name))
        && value.get(4 + name.len()) == Some(&b':')
    {
        return Some(name.len() + 5);
    }
    None
}
fn stripped<'a>(value: &'a [u8], name: &[u8]) -> &'a [u8] {
    let Some(offset) = prefix_offset(value, name) else {
        return value;
    };
    let tail = value.get(offset..).unwrap_or(&[]);
    let start = tail
        .iter()
        .position(|c| !matches!(c, b':' | b' ' | b'\t'))
        .unwrap_or(tail.len());
    tail.get(start..).unwrap_or(&[])
}
fn metadata_text(
    out: &mut Vec<u8>,
    text: &[u8],
    space: &mut bool,
    separator: &mut bool,
) -> Result<()> {
    if out.len() >= 512 {
        return Ok(());
    }
    for &c in text {
        if matches!(c, b' ' | b'\t' | b'\r' | b'\n') {
            *space = true;
            continue;
        }
        if out.len() >= 512 {
            break;
        }
        if *separator {
            append(out, b", ")?;
            *separator = false;
            *space = false;
        } else if *space && !out.is_empty() {
            append(out, b" ")?;
        }
        *space = false;
        append(out, &[c])?;
    }
    Ok(())
}
fn float(value: &[u8]) -> Result<Option<f32>> {
    if value.is_empty() {
        return Ok(None);
    }
    let mut bytes = Vec::new();
    append(&mut bytes, value)?;
    append(&mut bytes, &[0])?;
    unsafe extern "C" {
        fn strtof(s: *const core::ffi::c_char, end: *mut *mut core::ffi::c_char) -> f32;
    }
    let mut end = core::ptr::null_mut();
    // Match the firmware C runtime, including hexadecimal floats and full consumption.
    let parsed = unsafe { strtof(bytes.as_ptr().cast(), &mut end) };
    let start = bytes.as_ptr().cast::<core::ffi::c_char>();
    let valid =
        end != start.cast_mut() && !end.is_null() && unsafe { *end == 0 } && parsed.is_finite();
    Ok(valid.then_some(parsed))
}
fn property_has(value: &[u8], word: &[u8]) -> bool {
    value == word
        || (value.starts_with(word) && value.get(word.len()) == Some(&b' '))
        || value
            .windows(word.len() + 1)
            .any(|w| w.first() == Some(&b' ') && w.get(1..) == Some(word))
}
pub struct Record<'a> {
    pub id: u32,
    pub value: &'a [u8],
    pub href: &'a [u8],
    pub index: Option<f32>,
    pub error: u32,
}
pub struct Parser {
    xml: Xml,
    semantics: Semantics,
}
struct Semantics {
    state: State,
    metadata: Metadata,
    base: Vec<u8>,
    item: Vec<u8>,
    href: Vec<u8>,
    scratch: Vec<u8>,
    decoded: Vec<u8>,
    pending: u32,
    metadata_only: bool,
    complete: bool,
    has_cache: bool,
    explicit_start: bool,
    error: u32,
}
impl Parser {
    pub const fn new(metadata_only: bool, has_cache: bool) -> Self {
        Self {
            xml: Xml::new(),
            semantics: Semantics {
                state: State::Start,
                metadata: Metadata::new(),
                base: Vec::new(),
                item: Vec::new(),
                href: Vec::new(),
                scratch: Vec::new(),
                decoded: Vec::new(),
                pending: 0,
                metadata_only,
                complete: false,
                has_cache,
                explicit_start: false,
                error: 0,
            },
        }
    }
    pub fn set_base(&mut self, base: &[u8]) -> Result<()> {
        assign(&mut self.semantics.base, base)
    }
    pub fn feed(&mut self, bytes: &[u8], final_input: bool) -> usize {
        if self.semantics.error != 0 {
            0
        } else {
            self.xml.feed(bytes, final_input)
        }
    }
    #[allow(clippy::should_implement_trait)]
    pub fn next(&mut self) -> Record<'_> {
        let id = loop {
            if self.semantics.pending != 0 {
                let id = self.semantics.pending.trailing_zeros();
                self.semantics.pending &= !(1u32 << id);
                break id;
            }
            if self.semantics.error != 0 {
                break ERROR;
            }
            let event = self.xml.next();
            match event.kind {
                Kind::NeedInput => break NEED_INPUT,
                Kind::Done => break DONE,
                Kind::Error => {
                    self.semantics.error = event.error;
                    break ERROR;
                }
                Kind::Stopped => {
                    self.semantics.error = 35;
                    break ERROR;
                }
                _ => {
                    if self.semantics.metadata_only && self.semantics.complete {
                        continue;
                    }
                    if let Err(error) = self.semantics.event(&event) {
                        self.semantics.error = error;
                    }
                }
            }
        };
        let value = if id <= TEXT {
            self.semantics.metadata.get(id)
        } else {
            &self.semantics.item
        };
        Record {
            id,
            value,
            href: &self.semantics.href,
            index: self.semantics.metadata.index,
            error: self.semantics.error,
        }
    }
}
impl Semantics {
    fn event(&mut self, event: &Event<'_>) -> Result<()> {
        let Self {
            state,
            metadata: m,
            base,
            item,
            href: path,
            scratch,
            decoded,
            pending,
            metadata_only,
            complete,
            has_cache,
            explicit_start,
            error: _,
        } = self;
        let metadata_only = *metadata_only;
        let has_cache = *has_cache;
        let name = if matches!(event.kind, Kind::Start | Kind::End) {
            Element::parse(local(field(event, 0)))
        } else {
            Element::Unknown
        };
        let emit = |p: &mut u32, id: u32| {
            *p |= 1 << id;
        };
        if event.kind == Kind::Text {
            let mut no_separator = false;
            let out = match state {
                State::Title => Some(TITLE),
                State::Author => Some(AUTHOR),
                State::Language => Some(LANGUAGE),
                _ => None,
            };
            if let Some(id) = out {
                let target = m.values.get_mut(id as usize).ok_or(4u32)?;
                metadata_text(
                    target,
                    field(event, 0),
                    &mut m.space,
                    if id == AUTHOR {
                        &mut m.separator
                    } else {
                        &mut no_separator
                    },
                )?;
                emit(pending, id);
            } else if *state == State::Identifier {
                metadata_text(
                    &mut m.identifier,
                    field(event, 0),
                    &mut m.space,
                    &mut no_separator,
                )?;
            } else if *state == State::MetaText {
                metadata_text(
                    &mut m.meta_text,
                    field(event, 0),
                    &mut m.space,
                    &mut no_separator,
                )?;
            }
            return Ok(());
        }
        if event.kind == Kind::Start {
            if metadata_only && matches!(name, Element::Manifest | Element::Spine | Element::Guide)
            {
                *complete = true;
                emit(pending, COMPLETE);
                return Ok(());
            }
            match (*state, name) {
                (State::Start, Element::Package) => *state = State::Package,
                (State::Package, Element::Metadata) => *state = State::Metadata,
                (State::Metadata, Element::Title) => {
                    if m.get(TITLE).is_empty() {
                        *state = State::Title;
                        m.space = false;
                    }
                }
                (State::Metadata, Element::Creator) => {
                    *state = State::Author;
                    m.space = false;
                    m.separator = !m.get(AUTHOR).is_empty();
                }
                (State::Metadata, Element::Language) => {
                    *state = State::Language;
                    m.space = false;
                }
                (State::Metadata, Element::Identifier) => {
                    *state = State::Identifier;
                    m.identifier.clear();
                    m.isbn_scheme = false;
                    m.asin_scheme = false;
                    m.space = false;
                    for i in (1..event.fields.len()).step_by(2) {
                        if local(field(event, i)) == b"scheme" {
                            let scheme = bounded(field(event, i + 1));
                            m.isbn_scheme = contains_ci(scheme, b"isbn");
                            m.asin_scheme =
                                contains_ci(scheme, b"asin") || equal_ci(scheme, b"amazon");
                        }
                    }
                }
                (State::Metadata, Element::Meta) => {
                    let mut meta_name = &[][..];
                    let mut content = &[][..];
                    m.property = MetaProperty::Unknown;
                    let mut refines = &[][..];
                    let mut meta_id = &[][..];
                    m.meta_text.clear();
                    for i in (1..event.fields.len()).step_by(2) {
                        let value = bounded(field(event, i + 1));
                        match local(field(event, i)) {
                            b"name" => meta_name = value,
                            b"content" => content = value,
                            b"property" => m.property = MetaProperty::parse(value),
                            b"id" => meta_id = value,
                            b"refines" => refines = value.strip_prefix(b"#").unwrap_or(value),
                            _ => {}
                        }
                    }
                    if equal_ci(meta_name, b"cover") {
                        assign(&mut m.cover_id, content)?;
                    }
                    if equal_ci(meta_name, b"calibre:series") && m.calibre.is_empty() {
                        assign(&mut m.calibre, content)?;
                    }
                    if equal_ci(meta_name, b"calibre:series_index") && m.calibre_index.is_none() {
                        m.calibre_index = float(content)?;
                    }
                    if m.property != MetaProperty::Unknown {
                        assign(
                            &mut m.meta_id,
                            if m.property == MetaProperty::Collection {
                                meta_id
                            } else {
                                refines
                            },
                        )?;
                        *state = State::MetaText;
                        m.space = false;
                    }
                }
                (State::Package, Element::Manifest) => {
                    *state = State::Manifest;
                    emit(pending, MANIFEST_BEGIN);
                }
                (State::Package, Element::Spine) => {
                    *state = State::Spine;
                    emit(pending, SPINE_BEGIN);
                }
                (State::Package, Element::Guide) => {
                    *state = State::Guide;
                    emit(pending, GUIDE_BEGIN);
                }
                (State::Manifest, Element::Item) => {
                    assign(item, attr(event, b"id", false).unwrap_or(&[]))?;
                    path.clear();
                    if let Some(value) = attr(event, b"href", false) {
                        scratch.clear();
                        append(scratch, base)?;
                        append(scratch, value)?;
                        href::decode(decoded, scratch)?;
                        href::normalize(path, decoded)?;
                    }
                    let media = attr(event, b"media-type", false).unwrap_or(&[]);
                    let properties = attr(event, b"properties", false).unwrap_or(&[]);
                    emit(pending, MANIFEST);
                    if *item == m.cover_id && media.get(..6).is_some_and(|v| equal_ci(v, b"image/"))
                    {
                        m.put(COVER, path)?;
                        emit(pending, COVER);
                    }
                    if media == b"application/x-dtbncx+xml" && m.get(NCX).is_empty() {
                        m.put(NCX, path)?;
                        emit(pending, NCX);
                    }
                    if media == b"text/css" {
                        emit(pending, CSS);
                    }
                    if m.get(NAV).is_empty() && property_has(properties, b"nav") {
                        m.put(NAV, path)?;
                        emit(pending, NAV);
                    }
                    if m.get(COVER).is_empty() && property_has(properties, b"cover-image") {
                        m.put(COVER, path)?;
                        emit(pending, COVER);
                    }
                }
                (State::Spine, Element::Itemref) if has_cache => {
                    if let Some(id) = attr(event, b"idref", false) {
                        assign(item, id)?;
                        emit(pending, SPINE);
                    }
                }
                (State::Guide, Element::Reference) => {
                    path.clear();
                    if let Some(value) = attr(event, b"href", false) {
                        scratch.clear();
                        append(scratch, base)?;
                        append(scratch, value)?;
                        href::decode(decoded, scratch)?;
                        href::normalize(path, decoded)?;
                    }
                    let kind = attr(event, b"type", false).unwrap_or(&[]);
                    if !path.is_empty() {
                        if kind == b"start" && !*explicit_start {
                            m.put(TEXT, path)?;
                            *explicit_start = true;
                            emit(pending, TEXT);
                        } else if matches!(kind, b"cover" | b"cover-page")
                            && m.get(GUIDE).is_empty()
                        {
                            m.put(GUIDE, path)?;
                            emit(pending, GUIDE);
                        }
                    }
                }
                _ => {}
            }
            return Ok(());
        }
        if event.kind != Kind::End {
            return Ok(());
        }
        match (*state, name) {
            (State::Manifest, Element::Manifest)
            | (State::Spine, Element::Spine)
            | (State::Guide, Element::Guide) => {
                *state = State::Package;
                emit(pending, STORE_END);
            }
            (State::Title, Element::Title)
            | (State::Author, Element::Creator)
            | (State::Language, Element::Language) => *state = State::Metadata,
            (State::Identifier, Element::Identifier) => {
                if m.get(ISBN).is_empty()
                    && (m.isbn_scheme || prefix_offset(&m.identifier, b"isbn").is_some())
                {
                    assign(
                        m.values.get_mut(ISBN as usize).ok_or(4u32)?,
                        stripped(&m.identifier, b"isbn"),
                    )?;
                    emit(pending, ISBN);
                }
                if m.get(ASIN).is_empty()
                    && (m.asin_scheme || prefix_offset(&m.identifier, b"asin").is_some())
                {
                    assign(
                        m.values.get_mut(ASIN as usize).ok_or(4u32)?,
                        stripped(&m.identifier, b"asin"),
                    )?;
                    emit(pending, ASIN);
                }
                *state = State::Metadata;
            }
            (State::MetaText, Element::Meta) => {
                let belongs = m.property == MetaProperty::Collection;
                let collection_type = m.property == MetaProperty::Type;
                let position = m.property == MetaProperty::Position;
                let id = &m.meta_id;
                if !id.is_empty() && (belongs || collection_type || position) {
                    let mut index = m.collections.iter().position(|c| c.id == *id);
                    if index.is_none() && m.collections.len() < 8 {
                        let mut candidate = Collection {
                            id: Vec::new(),
                            title: Vec::new(),
                            index: None,
                            series: false,
                        };
                        append(&mut candidate.id, id)?;
                        m.collections.try_reserve(1).map_err(|_| 1u32)?;
                        index = Some(m.collections.len());
                        m.collections.push(candidate);
                    }
                    if let Some(candidate) = index.and_then(|i| m.collections.get_mut(i)) {
                        if belongs {
                            assign(&mut candidate.title, &m.meta_text)?;
                        } else if collection_type && equal_ci(&m.meta_text, b"series") {
                            candidate.series = true;
                        } else if position {
                            candidate.index = float(&m.meta_text)?;
                        }
                    }
                }
                *state = State::Metadata;
            }
            (State::Metadata, Element::Metadata) => {
                if !m.calibre.is_empty() {
                    assign(m.values.get_mut(SERIES as usize).ok_or(4u32)?, &m.calibre)?;
                    m.index = m.calibre_index;
                    emit(pending, SERIES);
                } else if let Some(candidate) = m
                    .collections
                    .iter()
                    .find(|c| c.series && !c.title.is_empty())
                {
                    assign(
                        m.values.get_mut(SERIES as usize).ok_or(4u32)?,
                        &candidate.title,
                    )?;
                    m.index = candidate.index;
                    emit(pending, SERIES);
                }
                *state = State::Package;
                *complete = true;
                emit(pending, COMPLETE);
            }
            (State::Package, Element::Package) => *state = State::Start,
            _ => {}
        }
        Ok(())
    }
}
