use super::{
    css,
    items::Items,
    log,
    package::{
        diagnostics,
        document::{self, Paths},
        Kind,
    },
    txt,
    writer::Writer,
    *,
};
use alloc::alloc::alloc;
use core::alloc::Layout;
use xp_core::{
    book::{self, Diagnostic as D, Operation as O},
    epub::opf,
};

pub(super) enum LoadError {
    Failed,
    OutOfMemory,
    Protection(OpenError),
}

// This transaction is unpublished. Parser/native sinks cannot borrow a ReaderBook.
struct Context<'a> {
    path: &'a Text,
    cache_path: &'a Text,
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    now: unsafe extern "C" fn() -> u32,
    metadata: Metadata,
    cache: BookCache,
    css: Option<Box<crate::css::Css>>,
    protection: Protection,
    paths: Paths,
    writer: Option<Writer<'a>>,
    work: Option<Metadata>,
    error: Option<LoadError>,
}

impl ReaderBook {
    // The caller constructs cache_path with the pinned native hash convention.
    // The source path moves only after successful load and checked owner allocation.
    pub(super) fn load(
        path: &mut Text,
        cache_path: Text,
        resources: &'static app_abi::Resources,
        leaves: &'static XpEpubLeaves,
        now: unsafe extern "C" fn() -> u32,
        build_if_missing: bool,
        skip_css: bool,
    ) -> Result<Box<Self>, LoadError> {
        let (metadata, cache, css, protection) = {
            let mut context = Context {
                path,
                cache_path: &cache_path,
                resources,
                leaves,
                now,
                metadata: Metadata::default(),
                cache: BookCache::new(leaves),
                css: None,
                protection: Protection::Plain,
                paths: Paths::default(),
                writer: None,
                work: None,
                error: None,
            };
            let services = book::Services {
                ctx: (&mut context as *mut Context<'_>).cast(),
                operation: Some(operation),
                millis: Some(millis),
                diagnostic: Some(diagnostic),
            };
            if !unsafe { book::load(&services, build_if_missing, skip_css) }
                || context.error.is_some()
            {
                return Err(context.error.take().unwrap_or(LoadError::Failed));
            }
            let Context {
                metadata,
                cache,
                css,
                protection,
                writer,
                work,
                paths,
                error,
                ..
            } = context;
            drop((writer, work, paths, error));
            (metadata, cache, css, protection)
        };
        let raw = unsafe { alloc(Layout::new::<Self>()) }.cast::<Self>();
        if raw.is_null() {
            return Err(LoadError::OutOfMemory);
        }
        unsafe {
            raw.write(Self {
                path: core::mem::take(path),
                cache_path,
                metadata,
                cache,
                css,
                protection,
            });
            Ok(Box::from_raw(raw))
        }
    }
}
impl<'a> Context<'a> {
    fn reset_metadata(&mut self) {
        self.writer = None;
        self.metadata = Metadata::default();
        self.cache = BookCache::new(self.leaves);
    }
    fn css(&mut self, action: css::Action) -> u32 {
        css::run(
            &mut self.css,
            self.cache_path,
            self.resources,
            self.leaves,
            action,
            true,
            None,
        )
    }
    fn operation(&mut self, operation: u32, argument: u32) -> Result<u32, ()> {
        let diagnostics = diagnostics::services(self.leaves);
        match operation {
            x if x == O::OpenProtection as u32 => match Protection::open(self.leaves, self.path) {
                Ok(protection) => {
                    if matches!(protection, Protection::Encrypted(_)) {
                        log::message(
                            self.leaves,
                            0,
                            b"EBP",
                            format_args!("protected content; on-read access path open"),
                        );
                    }
                    self.protection = protection;
                    Ok(1)
                }
                Err(error) => {
                    if let OpenError::Message(message) = &error {
                        log::with_bytes(
                            self.leaves,
                            1,
                            b"EBP",
                            &[b"protected content unavailable: ", message.bytes()],
                            format_args!(""),
                        );
                    }
                    self.error = Some(LoadError::Protection(error));
                    Ok(0)
                }
            },
            x if x == O::NewMetadata as u32 => {
                self.reset_metadata();
                Ok(1)
            }
            // CssParser was a small native wrapper; the Rust rule owner is created lazily.
            x if x == O::NewCss as u32 => {
                self.css = None;
                Ok(1)
            }
            x if x == O::LoadMetadata as u32 => {
                let mut path = Text::default();
                path.assign_parts(&[self.cache_path.bytes(), b"/book.bin"])
                    .map_err(|_| ())?;
                Ok(u32::from(
                    self.cache.load(&mut self.metadata, &path).is_ok(),
                ))
            }
            x if x == O::IsText as u32 => Ok(u32::from(txt::is_text(self.path.bytes()))),
            x if x == O::ValidateText as u32 => Ok(u32::from(txt::validate(
                self.path,
                self.cache_path,
                self.cache.cumulative(0),
                self.resources,
                self.leaves,
            ))),
            x if x == O::BuildText as u32 => Ok(u32::from(txt::build(
                self.path,
                self.cache_path,
                &mut self.cache,
                &mut self.metadata,
                self.resources,
                self.leaves,
                &diagnostics,
            ))),
            x if x == O::CssInspect as u32 => Ok(self.css(css::Action::Inspect)),
            x if x == O::CssLoad as u32 => Ok(self.css(css::Action::Load)),
            x if x == O::CssDelete as u32 => Ok(self.css(css::Action::Delete)),
            x if x == O::CopyMetadata as u32 => {
                let mut work = Metadata::default();
                for (field, bytes) in [
                    (opf::TITLE, &self.metadata.title),
                    (opf::AUTHOR, &self.metadata.author),
                    (opf::LANGUAGE, &self.metadata.language),
                    (opf::COVER, &self.metadata.cover),
                    (opf::TEXT, &self.metadata.text_reference),
                ] {
                    // A warm cache copy preserves bytes, including malformed legacy text.
                    let output = match field {
                        opf::TITLE => &mut work.title,
                        opf::AUTHOR => &mut work.author,
                        opf::LANGUAGE => &mut work.language,
                        opf::COVER => &mut work.cover,
                        _ => &mut work.text_reference,
                    };
                    output.try_reserve_exact(bytes.len()).map_err(|_| ())?;
                    output.extend_from_slice(bytes);
                }
                self.work = Some(work);
                Ok(0)
            }
            x if x == O::ParseOpf as u32 || x == O::ParseNav as u32 || x == O::ParseNcx as u32 => {
                let kind = if x == O::ParseOpf as u32 {
                    Kind::Opf
                } else if x == O::ParseNav as u32 {
                    Kind::Nav
                } else {
                    Kind::Ncx
                };
                let items = Items {
                    path: self.path,
                    protection: &self.protection,
                    resources: self.resources,
                    leaves: self.leaves,
                    now: self.now,
                };
                let writer = if kind != Kind::Opf || argument != 0 {
                    self.writer.as_mut()
                } else {
                    None
                };
                Ok(u32::from(document::parse(
                    &items,
                    kind,
                    &mut self.paths,
                    self.work.get_or_insert_with(Metadata::default),
                    writer,
                )))
            }
            x if x == O::DiscoverCss as u32 => {
                let items = Items {
                    path: self.path,
                    protection: &self.protection,
                    resources: self.resources,
                    leaves: self.leaves,
                    now: self.now,
                };
                css::discover(&items, self.paths.base.bytes(), &mut self.paths.css)?;
                Ok(0)
            }
            x if x == O::ReleaseMetadata as u32 => {
                self.reset_metadata();
                Ok(0)
            }
            x if x == O::ParseCss as u32 => {
                let items = Items {
                    path: self.path,
                    protection: &self.protection,
                    resources: self.resources,
                    leaves: self.leaves,
                    now: self.now,
                };
                Ok(css::parse_files(
                    &items,
                    &self.paths.css,
                    self.cache_path,
                    &mut self.css,
                    argument == 2,
                ))
            }
            x if x == O::RemoveSections as u32 => {
                let mut path = Text::default();
                path.assign_parts(&[self.cache_path.bytes(), b"/sections"])
                    .map_err(|_| ())?;
                unsafe { (self.leaves.path_remove_tree)(path.wire()) };
                Ok(0)
            }
            x if x == O::CssClear as u32 => {
                self.css = None;
                Ok(0)
            }
            x if x == O::SetupCache as u32 => {
                if unsafe { (self.resources.path_exists)(self.cache_path.wire()) } == 0 {
                    unsafe { (self.leaves.path_mkdir)(self.cache_path.wire()) };
                }
                Ok(0)
            }
            x if x == O::BeginWrite as u32 => {
                self.cache.spine_count = 0;
                self.cache.toc_count = 0;
                self.writer = Some(Writer::new(
                    self.path,
                    self.cache_path,
                    self.resources,
                    self.leaves,
                ));
                Ok(1)
            }
            x if x == O::BeginOpf as u32 => Ok(u32::from(
                self.writer
                    .as_mut()
                    .is_some_and(|writer| writer.begin_opf().is_ok()),
            )),
            x if x == O::EndOpf as u32 => Ok(u32::from(
                self.writer
                    .as_mut()
                    .is_some_and(|writer| writer.end_opf().is_ok()),
            )),
            x if x == O::BeginToc as u32 => {
                Ok(u32::from(self.writer.as_mut().is_some_and(|writer| {
                    writer.begin_toc(&diagnostics).is_ok()
                })))
            }
            x if x == O::EndToc as u32 => Ok(u32::from(
                self.writer
                    .as_mut()
                    .is_some_and(|writer| writer.end_toc().is_ok()),
            )),
            x if x == O::EndWrite as u32 => {
                Ok(u32::from(self.writer.as_mut().is_some_and(Writer::end)))
            }
            x if x == O::HasNav as u32 => Ok(u32::from(!self.paths.nav.is_empty())),
            x if x == O::HasNcx as u32 => Ok(u32::from(!self.paths.ncx.is_empty())),
            x if x == O::BuildBook as u32 => {
                let Some(writer) = self.writer.as_mut() else {
                    return Ok(0);
                };
                let Some(metadata) = self.work.as_ref() else {
                    return Ok(0);
                };
                self.cache.spine_count = writer.cache.spine_count;
                self.cache.toc_count = writer.cache.toc_count;
                Ok(u32::from(writer.build(metadata, &diagnostics).is_ok()))
            }
            x if x == O::CleanTmp as u32 => {
                txt::cleanup(self.cache_path, self.resources, self.leaves)?;
                Ok(1)
            }
            x if x == O::ReleaseWorkMetadata as u32 => {
                self.work = None;
                Ok(0)
            }
            _ => Ok(0),
        }
    }
}
unsafe extern "C" fn operation(raw: *mut c_void, operation: u32, argument: u32) -> u32 {
    let context = unsafe { &mut *raw.cast::<Context<'_>>() };
    match context.operation(operation, argument) {
        Ok(value) => value,
        Err(()) => {
            context.error = Some(LoadError::OutOfMemory);
            0
        }
    }
}
unsafe extern "C" fn millis(raw: *mut c_void) -> u32 {
    let context = unsafe { &*raw.cast::<Context<'_>>() };
    unsafe { (context.now)() }
}
unsafe extern "C" fn diagnostic(raw: *mut c_void, event: u32, elapsed: u32) {
    let context = unsafe { &*raw.cast::<Context<'_>>() };
    let leaves = context.leaves;
    macro_rules! emit { ($level:expr, $($arg:tt)*) => { log::message(leaves, $level, b"EBP", format_args!($($arg)*)) }; }
    let path = || {
        let bytes = context.path.bytes();
        &bytes[..bytes
            .iter()
            .position(|&byte| byte == 0)
            .unwrap_or(bytes.len())]
    };
    let named =
        |prefix: &[u8]| log::with_bytes(leaves, 0, b"EBP", &[prefix, path()], format_args!(""));
    match event {
        x if x == D::Loading as u32 => named(b"Loading ePub: "),
        x if x == D::OomCacheCss as u32 => emit!(1, "OOM: metadata cache or CSS parser"),
        x if x == D::TextInvalid as u32 => named(b"TXT/MD cache invalid or outdated: "),
        x if x == D::OomTextCache as u32 => emit!(1, "OOM: TXT/MD metadata cache"),
        x if x == D::TextLoaded as u32 => named(b"Loaded TXT/MD from cache: "),
        x if x == D::CssLowMemory as u32 => emit!(
            1,
            "Insufficient heap to load CSS cache; keeping it for a later retry"
        ),
        x if x == D::CssRebuild as u32 => emit!(
            0,
            "CSS cache missing, partial, or invalid; attempting to parse source stylesheets"
        ),
        x if x == D::CssOpfFailed as u32 => emit!(
            1,
            "Could not parse content.opf from cached bookMetadata for CSS files"
        ),
        x if x == D::CssReloadFailed as u32 => emit!(1, "Failed to reload cache after CSS rebuild"),
        x if x == D::Loaded as u32 => named(b"Loaded ePub: "),
        x if x == D::Building as u32 => emit!(0, "Cache not found, building spine/TOC cache"),
        x if x == D::BeginWriteFailed as u32 => emit!(1, "Could not begin writing cache"),
        x if x == D::BeginOpfFailed as u32 => emit!(1, "Could not begin writing content.opf pass"),
        x if x == D::OpfFailed as u32 => emit!(1, "Could not parse content.opf"),
        x if x == D::EndOpfFailed as u32 => emit!(1, "Could not end writing content.opf pass"),
        x if x == D::OpfTime as u32 => emit!(0, "OPF pass completed in {} ms", elapsed),
        x if x == D::BeginTocFailed as u32 => emit!(1, "Could not begin writing toc pass"),
        x if x == D::NavAttempt as u32 => emit!(0, "Attempting to parse EPUB 3 nav document"),
        x if x == D::NcxFallback as u32 => emit!(0, "Falling back to NCX TOC"),
        x if x == D::TocFailed as u32 => emit!(1, "Warning: Could not parse any TOC format"),
        x if x == D::EndTocFailed as u32 => emit!(1, "Could not end writing toc pass"),
        x if x == D::TocTime as u32 => emit!(0, "TOC pass completed in {} ms", elapsed),
        x if x == D::EndWriteFailed as u32 => emit!(1, "Could not end writing cache"),
        x if x == D::BuildFailed as u32 => emit!(1, "Could not update mappings and sizes"),
        x if x == D::BuildTime as u32 => emit!(0, "buildBookBin completed in {} ms", elapsed),
        x if x == D::TotalTime as u32 => emit!(0, "Total indexing completed in {} ms", elapsed),
        x if x == D::CleanTmpFailed as u32 => emit!(0, "Could not cleanup tmp files - ignoring"),
        x if x == D::ReloadFailed as u32 => emit!(1, "Failed to reload cache after writing"),
        _ => (),
    }
}
