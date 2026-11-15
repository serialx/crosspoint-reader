use super::super::{items::Items, log, Metadata, Text, Vec};
use super::{diagnostics, Document, Kind, Writer};
use core::{ffi::c_void, slice};
use xp_core::{
    book::{document as policy, residual::Span, residual_abi::*},
    epub::{decode_href, normalize_href, opf},
};

#[derive(Default)]
pub(in super::super) struct Paths {
    pub base: Text,
    pub ncx: Text,
    pub nav: Text,
    pub css: Vec<Text>,
}
struct Guide {
    html: Text,
    base: Text,
    image: Text,
}
struct Context<'a, 'source, 'path> {
    items: &'a Items<'source>,
    kind: Kind,
    paths: &'a mut Paths,
    metadata: &'a mut Metadata,
    writer: Option<&'a mut Writer<'path>>,
    parser: Option<Document<'a, 'path>>,
    opf_path: Text,
    nav_base: Text,
    size: usize,
    guide: Option<Guide>,
    failed: bool,
}

fn base(path: &[u8]) -> &[u8] {
    &path[..path
        .iter()
        .rposition(|&byte| byte == b'/')
        .map_or(0, |index| index + 1)]
}

pub(in super::super) fn parse<'source, 'path>(
    items: &Items<'source>,
    kind: Kind,
    paths: &mut Paths,
    metadata: &mut Metadata,
    writer: Option<&mut Writer<'path>>,
) -> bool {
    let mut context = Context {
        items,
        kind,
        paths,
        metadata,
        writer,
        parser: None,
        opf_path: Text::default(),
        nav_base: Text::default(),
        size: 0,
        guide: None,
        failed: false,
    };
    let ok = context.run();
    ok && !context.failed
}

impl Context<'_, '_, '_> {
    fn run(&mut self) -> bool {
        let services = policy::Services {
            ctx: (self as *mut Self).cast(),
            call,
            value,
            assign,
            diagnostic: Some(diagnostic),
        };
        unsafe {
            match self.kind {
                Kind::Container => policy::container(&services),
                Kind::Opf => policy::opf(&services, false),
                Kind::Nav => policy::toc(&services, true),
                Kind::Ncx => policy::toc(&services, false),
                Kind::Encryption => false,
            }
        }
    }
    fn path(&self) -> &[u8] {
        match self.kind {
            Kind::Container => b"META-INF/container.xml",
            Kind::Nav => self.paths.nav.bytes(),
            Kind::Ncx => self.paths.ncx.bytes(),
            _ => self.opf_path.bytes(),
        }
    }
    fn call(&mut self, operation: u32, chunk: usize) -> Result<usize, ()> {
        match operation {
            XP_BOOK_DOCUMENT_FIND => {
                let mut paths = Paths::default();
                let mut metadata = Metadata::default();
                let mut container = Context {
                    items: self.items,
                    kind: Kind::Container,
                    paths: &mut paths,
                    metadata: &mut metadata,
                    writer: None,
                    parser: None,
                    opf_path: Text::default(),
                    nav_base: Text::default(),
                    size: 0,
                    guide: None,
                    failed: false,
                };
                let ok = container.run() && !container.failed;
                self.opf_path = core::mem::take(&mut container.opf_path);
                Ok(usize::from(ok))
            }
            XP_BOOK_DOCUMENT_BASE => {
                if self.kind == Kind::Opf {
                    self.paths
                        .base
                        .assign(base(self.opf_path.bytes()))
                        .map_err(|_| ())?;
                } else if self.kind == Kind::Nav {
                    self.nav_base
                        .assign(base(self.paths.nav.bytes()))
                        .map_err(|_| ())?;
                }
                Ok(1)
            }
            XP_BOOK_DOCUMENT_SIZE => {
                let Some(size) = self.items.size(self.path()) else {
                    return Ok(0);
                };
                self.size = size as usize;
                Ok(1)
            }
            XP_BOOK_DOCUMENT_SETUP => {
                let base = match self.kind {
                    Kind::Container => &[][..],
                    Kind::Nav => self.nav_base.bytes(),
                    _ => self.paths.base.bytes(),
                };
                self.parser = Document::new(
                    self.items.leaves,
                    self.kind,
                    base,
                    self.size,
                    false,
                    self.writer.take(),
                )
                .ok();
                Ok(usize::from(self.parser.is_some()))
            }
            XP_BOOK_DOCUMENT_READ => {
                let path = match self.kind {
                    Kind::Container => b"META-INF/container.xml".as_slice(),
                    Kind::Nav => self.paths.nav.bytes(),
                    Kind::Ncx => self.paths.ncx.bytes(),
                    _ => self.opf_path.bytes(),
                };
                let parser = self.parser.as_mut().ok_or(())?;
                unsafe extern "C" fn write(
                    raw: *mut c_void,
                    bytes: *const u8,
                    length: usize,
                ) -> u8 {
                    let parser = unsafe { &mut *raw.cast::<Document<'_, '_>>() };
                    let bytes = if length == 0 {
                        &[]
                    } else {
                        unsafe { slice::from_raw_parts(bytes, length) }
                    };
                    let diagnostics = diagnostics::services(parser.leaves);
                    u8::from(parser.write(bytes, &diagnostics) == length)
                }
                Ok(usize::from(unsafe {
                    self.items.stream(
                        path,
                        chunk,
                        false,
                        write,
                        (parser as *mut Document<'_, '_>).cast(),
                    )
                }))
            }
            XP_BOOK_DOCUMENT_GUIDE_LOAD => {
                let path = self.parser.as_ref().ok_or(())?.value(opf::GUIDE);
                let Some(contents) = self.items.memory(path, true) else {
                    return Ok(0);
                };
                let html = Text::copy(contents.bytes()).map_err(|_| ())?;
                drop(contents);
                self.guide = Some(Guide {
                    html,
                    base: Text::copy(base(path)).map_err(|_| ())?,
                    image: Text::default(),
                });
                Ok(1)
            }
            XP_BOOK_DOCUMENT_GUIDE_ASSIGN => {
                let guide = self.guide.as_ref().ok_or(())?;
                let mut joined = Text::default();
                joined
                    .assign_parts(&[guide.base.bytes(), guide.image.bytes()])
                    .map_err(|_| ())?;
                let mut decoded = Vec::new();
                decode_href(&mut decoded, joined.bytes()).map_err(|_| ())?;
                normalize_href(&mut self.metadata.cover, &decoded).map_err(|_| ())?;
                Ok(1)
            }
            XP_BOOK_DOCUMENT_GUIDE_RELEASE => {
                self.guide = None;
                Ok(1)
            }
            XP_BOOK_DOCUMENT_CSS => {
                let parser = self.parser.as_ref().ok_or(())?;
                if parser.css_count() != 0 {
                    let mut paths = Vec::new();
                    paths
                        .try_reserve_exact(parser.css_count())
                        .map_err(|_| ())?;
                    for index in 0..parser.css_count() {
                        paths.push(Text::copy(parser.value(32 + index as u32)).map_err(|_| ())?);
                    }
                    self.paths.css = paths;
                }
                Ok(1)
            }
            _ => Ok(0),
        }
    }
}
unsafe extern "C" fn call(raw: *mut c_void, operation: u32, chunk: usize) -> usize {
    let context = unsafe { &mut *raw.cast::<Context<'_, '_, '_>>() };
    match context.call(operation, chunk) {
        Ok(value) => value,
        Err(()) => {
            context.failed = true;
            0
        }
    }
}
unsafe extern "C" fn value(raw: *mut c_void, field: u32) -> Span {
    let context = unsafe { &*raw.cast::<Context<'_, '_, '_>>() };
    let bytes = if field == XP_BOOK_DOCUMENT_AUXILIARY {
        context
            .guide
            .as_ref()
            .map_or_else(|| context.path(), |guide| guide.html.bytes())
    } else {
        context
            .parser
            .as_ref()
            .map_or(&[][..], |parser| parser.value(field))
    };
    Span::new(bytes)
}
unsafe extern "C" fn assign(raw: *mut c_void, field: u32, value: Span) {
    let context = unsafe { &mut *raw.cast::<Context<'_, '_, '_>>() };
    let bytes = unsafe { value.bytes() };
    let result = if context.kind == Kind::Container {
        context.opf_path.assign(bytes).map_err(|_| ())
    } else if field == XP_BOOK_DOCUMENT_AUXILIARY {
        context
            .guide
            .as_mut()
            .ok_or(())
            .and_then(|guide| guide.image.assign(bytes).map_err(|_| ()))
    } else {
        match field {
            opf::NCX => context.paths.ncx.assign(bytes).map_err(|_| ()),
            opf::NAV => context.paths.nav.assign(bytes).map_err(|_| ()),
            _ => context.metadata.assign(field, bytes),
        }
    };
    context.failed |= result.is_err();
}
unsafe extern "C" fn diagnostic(raw: *mut c_void, event: u32) {
    let context = unsafe { &*raw.cast::<Context<'_, '_, '_>>() };
    let (level, message): (u8, &[u8]) = match event {
        XP_BOOK_DOCUMENT_LOG_CONTAINER_SIZE => {
            (1, b"Could not find or size META-INF/container.xml")
        }
        XP_BOOK_DOCUMENT_LOG_CONTAINER_READ => (1, b"Could not read META-INF/container.xml"),
        XP_BOOK_DOCUMENT_LOG_CONTAINER_ROOT => {
            (1, b"Could not find valid rootfile in container.xml")
        }
        XP_BOOK_DOCUMENT_LOG_FIND_OPF => (1, b"Could not find content.opf in zip"),
        XP_BOOK_DOCUMENT_LOG_PARSING_OPF => (0, b"Parsing content.opf: "),
        XP_BOOK_DOCUMENT_LOG_OPF_SIZE => (1, b"Could not get size of content.opf"),
        XP_BOOK_DOCUMENT_LOG_OPF_SETUP => (1, b"Could not setup content.opf parser"),
        XP_BOOK_DOCUMENT_LOG_OPF_READ => (1, b"Could not read content.opf"),
        XP_BOOK_DOCUMENT_LOG_METADATA_PARSED => (0, b"Successfully parsed package metadata"),
        XP_BOOK_DOCUMENT_LOG_GUIDE_FALLBACK => {
            (0, b"No cover from metadata, trying guide cover page: ")
        }
        XP_BOOK_DOCUMENT_LOG_GUIDE_FOUND => (0, b"Found cover image from guide: "),
        XP_BOOK_DOCUMENT_LOG_OPF_PARSED => (0, b"Successfully parsed content.opf"),
        XP_BOOK_DOCUMENT_LOG_NO_NCX => (0, b"No ncx file specified"),
        XP_BOOK_DOCUMENT_LOG_PARSING_NCX => (0, b"Parsing toc ncx file: "),
        XP_BOOK_DOCUMENT_LOG_NCX_SIZE => (1, b"Could not get size of toc ncx file"),
        XP_BOOK_DOCUMENT_LOG_NCX_SETUP => (1, b"Could not setup toc ncx parser"),
        XP_BOOK_DOCUMENT_LOG_NCX_READ => (1, b"Could not read toc ncx file"),
        XP_BOOK_DOCUMENT_LOG_NO_NAV => (0, b"No nav file specified"),
        XP_BOOK_DOCUMENT_LOG_PARSING_NAV => (0, b"Parsing toc nav file: "),
        XP_BOOK_DOCUMENT_LOG_NAV_SIZE => (1, b"Could not get size of toc nav file"),
        XP_BOOK_DOCUMENT_LOG_NAV_SETUP => (1, b"Could not setup toc nav parser"),
        XP_BOOK_DOCUMENT_LOG_NAV_READ => (1, b"Could not read toc nav file"),
        XP_BOOK_DOCUMENT_LOG_NCX_PARSED => (0, b"Parsed TOC items"),
        XP_BOOK_DOCUMENT_LOG_NAV_PARSED => (0, b"Parsed TOC nav items"),
        _ => return,
    };
    let path = match event {
        XP_BOOK_DOCUMENT_LOG_PARSING_OPF => context.opf_path.bytes(),
        XP_BOOK_DOCUMENT_LOG_PARSING_NCX => context.paths.ncx.bytes(),
        XP_BOOK_DOCUMENT_LOG_PARSING_NAV => context.paths.nav.bytes(),
        XP_BOOK_DOCUMENT_LOG_GUIDE_FALLBACK => context
            .parser
            .as_ref()
            .map_or(&[][..], |parser| parser.value(opf::GUIDE)),
        XP_BOOK_DOCUMENT_LOG_GUIDE_FOUND => &context.metadata.cover,
        _ => &[],
    };
    let path = &path[..path
        .iter()
        .position(|&byte| byte == 0)
        .unwrap_or(path.len())];
    log::with_bytes(
        context.items.leaves,
        level,
        b"EBP",
        &[message, path],
        format_args!(""),
    );
}
