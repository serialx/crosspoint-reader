extern crate alloc;

use super::{writer::Writer, XpEpubLeaves};
use alloc::{alloc::alloc, boxed::Box};
use core::alloc::Layout;
use xp_core::epub::{
    cache::{self, Cache, Io},
    diagnostic::{Diagnostics, Error},
    facade::{self, Sink},
    ParserKind,
};

pub(super) mod diagnostics;
pub(super) mod document;

#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) enum Kind {
    Container,
    Opf,
    Ncx,
    Nav,
    Encryption,
}

enum Pass {
    Simple(Box<facade::Simple>),
    Opf(Box<facade::Opf>),
}

// Parsers and their temporary item file are load-transaction resources. The book
// is not borrowed while parser sinks call storage, ZIP or protection leaves.
pub(super) struct Document<'writer, 'path> {
    pass: Pass,
    kind: Kind,
    leaves: &'static XpEpubLeaves,
    writer: Option<&'writer mut Writer<'path>>,
    metadata_only: bool,
    pub(super) error: Error,
}

struct WriterSink<'a> {
    cache: Option<&'a mut Cache>,
    items: Option<Io>,
    records: Option<Io>,
}

impl Sink for WriterSink<'_> {
    fn io(&self) -> Option<&Io> {
        self.items.as_ref()
    }
    fn spine(&mut self, href: &[u8], diagnostics: &Diagnostics) -> Result<(), u32> {
        match (&mut self.cache, &self.records) {
            (Some(cache), Some(io)) => cache.create_spine(io, href, diagnostics),
            _ => Ok(()),
        }
    }
    fn toc(
        &mut self,
        title: &[u8],
        href: &[u8],
        anchor: &[u8],
        level: u8,
        diagnostics: &Diagnostics,
    ) -> Result<(), u32> {
        match (&mut self.cache, &self.records) {
            (Some(cache), Some(io)) => {
                cache.create_toc(io, title, href, anchor, level, diagnostics)
            }
            _ => Ok(()),
        }
    }
}

impl<'writer, 'path> Document<'writer, 'path> {
    pub(super) fn new(
        leaves: &'static XpEpubLeaves,
        kind: Kind,
        base: &[u8],
        size: usize,
        metadata_only: bool,
        writer: Option<&'writer mut Writer<'path>>,
    ) -> Result<Self, ()> {
        let result = Self::allocate(leaves, kind, base, size, metadata_only, writer);
        if result.is_err() {
            diagnostics::allocation(leaves, kind);
        }
        result
    }

    fn allocate(
        leaves: &'static XpEpubLeaves,
        kind: Kind,
        base: &[u8],
        size: usize,
        metadata_only: bool,
        writer: Option<&'writer mut Writer<'path>>,
    ) -> Result<Self, ()> {
        // Construct the same parser directly in checked heap storage. No parser-sized
        // local or second native PackageParser owns this state.
        let pass = if kind == Kind::Opf {
            let raw = unsafe { alloc(Layout::new::<facade::Opf>()) }.cast::<facade::Opf>();
            if raw.is_null() {
                return Err(());
            }
            unsafe { raw.write(facade::Opf::new(size, metadata_only, writer.is_some())) };
            Pass::Opf(unsafe { Box::from_raw(raw) })
        } else {
            let parser_kind = match kind {
                Kind::Container => ParserKind::Container,
                Kind::Ncx => ParserKind::Ncx,
                Kind::Nav => ParserKind::Nav,
                Kind::Encryption => ParserKind::Encryption,
                Kind::Opf => unreachable!(),
            };
            let raw = unsafe { alloc(Layout::new::<facade::Simple>()) }.cast::<facade::Simple>();
            if raw.is_null() {
                return Err(());
            }
            unsafe { raw.write(facade::Simple::new(parser_kind, size)) };
            Pass::Simple(unsafe { Box::from_raw(raw) })
        };
        let mut document = Self {
            pass,
            kind,
            leaves,
            writer,
            metadata_only,
            error: Error::default(),
        };
        match &mut document.pass {
            Pass::Simple(pass) => pass.parser.set_base(base).map_err(|_| ())?,
            Pass::Opf(pass) => pass.parser.set_base(base).map_err(|_| ())?,
        }
        Ok(document)
    }

    pub(super) fn write(&mut self, bytes: &[u8], diagnostics: &Diagnostics) -> usize {
        self.error = Error::default();
        let io = self.writer.as_mut().map(|writer| writer.streams.io());
        let mut sink = WriterSink {
            cache: self.writer.as_mut().map(|writer| &mut writer.cache),
            items: io.filter(|_| matches!(self.pass, Pass::Opf(_)) && !self.metadata_only),
            records: io,
        };
        let count = match &mut self.pass {
            Pass::Simple(pass) => pass.write(bytes, &mut sink, diagnostics, &mut self.error),
            Pass::Opf(pass) => pass.write(bytes, &mut sink, diagnostics, &mut self.error),
        };
        diagnostics::error(self.leaves, self.kind, &self.error);
        count
    }

    #[cfg(feature = "app-test")]
    pub(super) fn finish(&mut self, diagnostics: &Diagnostics) -> bool {
        self.error = Error::default();
        let io = self.writer.as_mut().map(|writer| writer.streams.io());
        let mut sink = WriterSink {
            cache: self.writer.as_mut().map(|writer| &mut writer.cache),
            items: None,
            records: io,
        };
        let ok = match &mut self.pass {
            Pass::Simple(pass) => pass.finish(&mut sink, diagnostics, &mut self.error),
            Pass::Opf(_) => false,
        };
        diagnostics::error(self.leaves, self.kind, &self.error);
        ok
    }

    pub(super) fn value(&self, field: u32) -> &[u8] {
        match &self.pass {
            Pass::Simple(pass) => pass.parser.value(),
            Pass::Opf(pass) => {
                if field >= 32 {
                    pass.css
                        .get((field - 32) as usize)
                        .map_or(&[], |value| value.as_slice())
                } else {
                    pass.parser.value(field)
                }
            }
        }
    }

    pub(super) fn css_count(&self) -> usize {
        match &self.pass {
            Pass::Opf(pass) => pass.css.len(),
            Pass::Simple(_) => 0,
        }
    }
}

impl Drop for Document<'_, '_> {
    fn drop(&mut self) {
        if matches!(self.pass, Pass::Opf(_)) && !self.metadata_only {
            if let Some(writer) = &mut self.writer {
                let io = writer.streams.io();
                if io.control(cache::ITEMS, cache::IS_OPEN) != 0 {
                    io.control(cache::ITEMS, cache::CLOSE);
                }
                io.control(cache::ITEMS, cache::REMOVE);
            }
        }
    }
}

#[cfg(feature = "app-test")]
#[path = "../test_support/book_package.rs"]
mod tests;
