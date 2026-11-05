//! Stable Chapter services; callback mutation is confined to disjoint interior fields.
extern crate alloc;
use super::{
    super::{
        abi::{XpEpubBuildPopup, XpEpubLeaves},
        book_owner::FileSlot,
        Text,
    },
    file::CacheFile,
    image,
    input::{self, Input},
    BuildRecords, PagePosition,
};
use crate::{app::abi, css::Css, section_cache};
use alloc::{
    alloc::{alloc, dealloc},
    boxed::Box,
    vec::Vec,
};
use core::{
    alloc::Layout,
    cell::{Cell, UnsafeCell},
    ffi::c_void,
    ptr::{self, NonNull},
};
use xp_core::{
    chapter::{
        assembly::{self, Diagnostic, ImageProbe, Position},
        services::Trace,
        table::UseReader,
        ReaderServices, State,
    },
    para::{paragraph::Metrics, Allocator},
};

#[derive(Clone, Copy)]
#[repr(C)]
pub(super) struct Fonts {
    pub metrics: &'static Metrics,
    pub allocator: &'static Allocator,
    pub variant: unsafe extern "C" fn(i32, f32) -> i32,
    pub line_height: unsafe extern "C" fn(i32, f32) -> i32,
    pub ready: unsafe extern "C" fn(i32, i32) -> i32,
    pub codepoint: unsafe extern "C" fn(i32, u32, u8, *mut i32, *mut i32) -> u8,
}

#[derive(Clone, Copy)]
pub(in crate::app::epub) struct Popup {
    pub call: XpEpubBuildPopup,
    pub context: *mut c_void,
}
impl Popup {
    pub(super) fn show(self) {
        unsafe { (self.call)(self.context) };
    }
}

pub(super) struct Paths {
    pub parse: Text,
    pub content: Text,
    pub image: Text,
    pub final_cache: Text,
    pub temporary_cache: Text,
    pub html: Text,
    pub temporary_html: Text,
}

pub(super) struct Init {
    pub book: NonNull<c_void>,
    pub resources: &'static abi::Resources,
    pub leaves: &'static XpEpubLeaves,
    pub fonts: Fonts,
    pub image: image::Physical,
    pub now: unsafe extern "C" fn() -> u32,
    pub popup: Option<Popup>,
    pub paths: Paths,
    pub css: Option<Box<Css>>,
    pub toc: Vec<Text>,
    pub language: i32,
    pub pages: u16,
    pub output: FileSlot,
}

pub(super) struct BuildServices {
    pub input: Input,
    pub paths: Paths,
    pub resources: &'static abi::Resources,
    pub leaves: &'static XpEpubLeaves,
    book: NonNull<c_void>,
    fonts: Fonts,
    image: image::Physical,
    pub now: unsafe extern "C" fn() -> u32,
    pub popup: Option<Popup>,
    css: UnsafeCell<Option<Box<Css>>>,
    toc: Vec<Text>,
    language: i32,
    output: UnsafeCell<FileSlot>,
    records: UnsafeCell<BuildRecords>,
    pending: UnsafeCell<Text>,
    images: UnsafeCell<image::Paths>,
    pub built: Cell<u16>,
    pub pages: Cell<u16>,
    pub stopped: Cell<bool>,
    pub started: Cell<u32>,
}

impl BuildServices {
    // Every member is initialized directly in its final allocation; no callback runs here.
    pub(super) fn allocate(init: &mut Option<Init>) -> Option<NonNull<Self>> {
        let raw = NonNull::new(unsafe { alloc(Layout::new::<Self>()) }.cast::<Self>())?;
        let init = init.take().expect("pending build resources");
        let out = raw.as_ptr();
        unsafe {
            ptr::addr_of_mut!((*out).input).write(Input::new(init.resources));
            ptr::addr_of_mut!((*out).paths).write(init.paths);
            ptr::addr_of_mut!((*out).resources).write(init.resources);
            ptr::addr_of_mut!((*out).leaves).write(init.leaves);
            ptr::addr_of_mut!((*out).book).write(init.book);
            ptr::addr_of_mut!((*out).fonts).write(init.fonts);
            ptr::addr_of_mut!((*out).image).write(init.image);
            ptr::addr_of_mut!((*out).now).write(init.now);
            ptr::addr_of_mut!((*out).popup).write(init.popup);
            ptr::addr_of_mut!((*out).css).write(UnsafeCell::new(init.css));
            ptr::addr_of_mut!((*out).toc).write(init.toc);
            ptr::addr_of_mut!((*out).language).write(init.language);
            ptr::addr_of_mut!((*out).output).write(UnsafeCell::new(init.output));
            ptr::addr_of_mut!((*out).records)
                .write(UnsafeCell::new(BuildRecords::new(init.resources)));
            ptr::addr_of_mut!((*out).pending).write(UnsafeCell::new(Text::default()));
            ptr::addr_of_mut!((*out).images).write(UnsafeCell::new(image::Paths::default()));
            ptr::addr_of_mut!((*out).built).write(Cell::new(0));
            ptr::addr_of_mut!((*out).pages).write(Cell::new(init.pages));
            ptr::addr_of_mut!((*out).stopped).write(Cell::new(false));
            ptr::addr_of_mut!((*out).started).write(Cell::new(0));
        }
        Some(raw)
    }

    pub(super) unsafe fn destroy(raw: NonNull<Self>) {
        // The caller has already destroyed Chapter and ended all synchronous loans.
        unsafe {
            ptr::drop_in_place(raw.as_ptr());
            dealloc(raw.as_ptr().cast(), Layout::new::<Self>());
        }
    }

    pub(super) fn css(&self) -> *const Css {
        unsafe { &*self.css.get() }
            .as_deref()
            .map_or(ptr::null(), |css| css)
    }

    // No Chapter may exist while the CSS database is hydrated or cleared.
    pub(super) unsafe fn load_css(&self, cache: &Text) -> super::book::CssStatus {
        super::book::load_css(
            unsafe { &mut *self.css.get() },
            cache,
            self.resources,
            self.leaves,
        )
    }
    pub(super) unsafe fn clear_css(&self) {
        let previous = unsafe { self.css.get().replace(None) };
        drop(previous);
    }

    pub(super) fn writer(&self, operation: impl FnOnce(&mut CacheFile<'_>)) {
        let mut io = CacheFile::writing(
            &self.paths.final_cache,
            &self.paths.temporary_cache,
            unsafe { &mut *self.output.get() },
            self.resources,
            self.leaves,
        );
        operation(&mut io);
    }

    pub(super) fn commit(&self, version: u8, consumed: u32, total: u32) -> bool {
        let mut result = false;
        self.writer(|io| {
            result = section_cache::commit(
                io,
                unsafe { &*self.records.get() },
                version,
                self.built.get(),
                consumed,
                total,
            )
        });
        result
    }

    pub(super) fn records(&self) -> &BuildRecords {
        unsafe { &*self.records.get() }
    }

    pub(super) fn output<R>(&self, operation: impl FnOnce(&mut FileSlot) -> R) -> R {
        operation(unsafe { &mut *self.output.get() })
    }

    // Configuration finishes before Chapter can borrow these members.
    pub(super) fn configure(&mut self, toc: Vec<Text>, language: i32) {
        self.toc = toc;
        self.language = language;
    }
}

// Only the interior member needed by each operation is borrowed. Chapter and its
// encoded page storage share an allocation disjoint from these services; physical
// calls cannot reenter Chapter.
unsafe impl ReaderServices for BuildServices {
    fn metrics(&self) -> &Metrics {
        self.fonts.metrics
    }
    fn allocator(&self) -> &Allocator {
        self.fonts.allocator
    }
    fn language(&self) -> i32 {
        self.language
    }
    fn entity_lookup(&self, spelling: &[u8]) -> Option<&[u8]> {
        crate::book_policy::entity(spelling).map(core::ffi::CStr::to_bytes)
    }
    fn entity_buffer(&self, minimum: u32) -> Option<NonNull<[u8]>> {
        self.input.replace_buffer(minimum)
    }
    fn font_variant(&self, font: i32, scale: f32) -> i32 {
        unsafe { (self.fonts.variant)(font, scale) }
    }
    fn line_height(&self, font: i32, compression: f32) -> i32 {
        unsafe { (self.fonts.line_height)(font, compression) }
    }
    fn codepoint(&self, font: i32, codepoint: u32, style: u8) -> Option<(i32, i32)> {
        let (mut advance, mut top) = (0, 0);
        (unsafe { (self.fonts.codepoint)(font, codepoint, style, &mut advance, &mut top) } != 0)
            .then_some((advance, top))
    }
    fn font_ready(&self, font: i32, fallback: i32) -> i32 {
        unsafe { (self.fonts.ready)(font, fallback) }
    }
    fn complete(&self, bytes: &[u8], position: Position) -> assembly::Result {
        // Reserve before writing/publishing. A failed write still records its zero offset.
        unsafe { (&mut *self.records.get()).pages.reserve_one() }
            .map_err(|_| assembly::Error::Allocation)?;
        let (mut built, mut pages, mut offset) = (self.built.get(), self.pages.get(), 0);
        self.writer(|io| offset = section_cache::append(io, bytes, &mut built, &mut pages));
        self.built.set(built);
        self.pages.set(pages);
        unsafe {
            (&mut *self.records.get())
                .pages
                .push_reserved(PagePosition {
                    offset,
                    paragraph: position.paragraph,
                    list: position.list,
                    visible: position.visible,
                })
        };
        if self.stopped.get() {
            Err(assembly::Error::Stopped)
        } else {
            Ok(())
        }
    }
    fn probe_image(
        &self,
        source: &[u8],
        counter: &mut i32,
        popup: &mut bool,
    ) -> assembly::Result<ImageProbe<()>> {
        let show = || self.popup.unwrap().show();
        let (result, paths) = unsafe {
            image::probe(
                self.book,
                self.leaves,
                &self.image,
                &self.paths.content,
                &self.paths.image,
                source,
                counter,
                popup,
                self.popup.map(|_| &show as &dyn Fn()),
            )
        }?;
        if let Some(paths) = paths {
            let previous = unsafe { self.images.get().replace(paths) };
            drop(previous);
        }
        Ok(result)
    }
    fn image_paths(&self) -> (&[u8], &[u8]) {
        let paths = unsafe { &*self.images.get() };
        (paths.cache.bytes(), paths.source.bytes())
    }
    fn pending_anchor(&self) -> &[u8] {
        unsafe { &*self.pending.get() }.bytes()
    }
    fn defer_anchor(&self, text: &[u8]) -> assembly::Result {
        unsafe { (&mut *self.pending.get()).assign(text) }.map_err(|_| assembly::Error::Allocation)
    }
    fn toc_anchor(&self, index: u32) -> Option<&[u8]> {
        self.toc.get(index as usize).map(Text::bytes)
    }
    fn anchor_count(&self) -> u32 {
        unsafe { &*self.records.get() }.anchors.len() as u32
    }
    fn record_anchor(&self, page: u16) -> assembly::Result {
        unsafe { (&mut *self.records.get()).push_anchor(&mut *self.pending.get(), page) }
            .map_err(|_| assembly::Error::Allocation)
    }
    unsafe fn table_file(&self, state: *mut State, visitor: UseReader) {
        unsafe { input::table(self.leaves, &self.paths.parse, state, visitor) };
    }
    fn trace(&self, _record: &Trace) {}
    fn trace_bytes(&self, _bytes: &[u8]) {}
    fn diagnostic(&self, event: Diagnostic, a: u32, b: u32, c: u32, text: &[u8]) {
        super::diagnostics::chapter(self.leaves, event, a, b, c, text);
    }
}
