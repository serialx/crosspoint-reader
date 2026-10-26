//! Synchronous page sink and the owned paragraph used by chapter assembly.
use super::*;
use crate::{
    chapter::services::{Span, Trace},
    para::{
        paragraph::{self, Metrics},
        paragraph_extract, Allocator,
    },
};
use alloc::alloc::{alloc, dealloc};
use core::{
    alloc::Layout as Allocation,
    ffi::c_void,
    ptr::{self, NonNull},
};

#[derive(Clone, Copy)]
pub struct ParagraphContext {
    allocator: *const Allocator,
    metrics: *const Metrics,
    context: *mut c_void,
    language: i32,
}

extern "C" {
    fn xp_chapter_release_block(block: *mut c_void);
}

#[repr(transparent)]
pub struct Payload {
    pointer: NonNull<c_void>,
}
impl Drop for Payload {
    fn drop(&mut self) {
        unsafe {
            xp_chapter_release_block(self.pointer.as_ptr());
        }
    }
}

/// Inputs are borrowed until return. Returned link views last until the next
/// line_links call; pending-anchor views last until defer/record. Complete consumes
/// every payload, including when it reports stop or allocation failure.
#[repr(C)]
pub struct Callbacks {
    pub allocator: unsafe extern "C" fn() -> *const Allocator,
    pub metrics: *const Metrics,
    pub language: unsafe extern "C" fn() -> i32,
    pub reclaim_paragraph_memory: unsafe extern "C" fn(),
    pub entity_lookup: unsafe extern "C" fn(*mut c_void, Span) -> Span,
    pub entity_buffer: unsafe extern "C" fn(*mut c_void, u32) -> *mut u8,
    pub font_variant: unsafe extern "C" fn(*mut c_void, i32, f32) -> i32,
    pub line_height: unsafe extern "C" fn(*mut c_void, i32, f32) -> i32,
    pub codepoint: unsafe extern "C" fn(*mut c_void, i32, u32, u8, *mut i32, *mut i32) -> u8,
    pub create_line:
        unsafe extern "C" fn(*mut c_void, *const paragraph_extract::Output) -> *mut c_void,
    pub line_metrics:
        unsafe extern "C" fn(*mut c_void, *const c_void, *const Options, bool) -> LineMetrics,
    pub set_margin: unsafe extern "C" fn(*mut c_void, i16),
    pub line_links: unsafe extern "C" fn(*mut c_void, *mut c_void, *mut u32) -> *const LineLink,
    pub complete: unsafe extern "C" fn(
        *mut c_void,
        *const Element<Payload>,
        u32,
        *const Link,
        u32,
        *const Footnote,
        u32,
        Position,
    ) -> u8,
    pub probe_image:
        unsafe extern "C" fn(*mut c_void, Span, *mut i32, *mut bool, *mut u16, *mut u16) -> u8,
    pub create_image: unsafe extern "C" fn(*mut c_void, i32, i32) -> *mut c_void,
    pub pending_anchor: unsafe extern "C" fn(*mut c_void) -> Span,
    pub defer_anchor: unsafe extern "C" fn(*mut c_void, Span),
    pub anchor_is_toc: unsafe extern "C" fn(*mut c_void, Span) -> u8,
    pub anchor_count: unsafe extern "C" fn(*mut c_void) -> u32,
    pub record_anchor: unsafe extern "C" fn(*mut c_void, u16),
    pub measure_table: unsafe extern "C" fn(*mut c_void, *mut crate::chapter::State),
    pub trace: unsafe extern "C" fn(*mut c_void, *const Trace),
    pub trace_bytes: unsafe extern "C" fn(*mut c_void, Span),
}

#[derive(Clone, Copy)]
pub struct PageSink {
    pub(in crate::chapter) callbacks: *const Callbacks,
    pub(in crate::chapter) context: *mut c_void,
}
impl PageSink {
    pub fn api(&self) -> &Callbacks {
        unsafe { &*self.callbacks }
    }
    fn payload(&self, pointer: *mut c_void) -> Result<Payload> {
        Ok(Payload {
            pointer: NonNull::new(pointer).ok_or(Error::Allocation)?,
        })
    }
}
impl Sink for PageSink {
    type Payload = Payload;
    type Image = ();
    fn paragraph_context(&self) -> Option<ParagraphContext> {
        let api = self.api();
        Some(ParagraphContext {
            allocator: unsafe { (api.allocator)() },
            metrics: api.metrics,
            context: self.context,
            language: unsafe { (api.language)() },
        })
    }
    fn create_line(&mut self, line: &paragraph_extract::Output) -> Result<Payload> {
        self.payload(unsafe { (self.api().create_line)(self.context, line) })
    }
    #[cfg(feature = "chapter-trace")]
    fn trace(&self, observation: super::trace::Observation<'_>) {
        use core::fmt::{self, Write};
        struct Writer(PageSink);
        impl Write for Writer {
            fn write_str(&mut self, text: &str) -> fmt::Result {
                unsafe {
                    (self.0.api().trace_bytes)(self.0.context, Span::new(text.as_bytes()));
                }
                Ok(())
            }
        }
        let _ = write!(Writer(*self), "{observation}");
    }
    fn reclaim_paragraph_memory(&mut self) {
        unsafe { (self.api().reclaim_paragraph_memory)() };
    }
    fn pending_anchor(&self) -> &[u8] {
        let span = unsafe { (self.api().pending_anchor)(self.context) };
        if span.len == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(span.data, span.len as usize) }
        }
    }
    fn defer_anchor(&mut self, text: &[u8]) -> Result {
        unsafe {
            (self.api().defer_anchor)(self.context, Span::new(text));
        }
        Ok(())
    }
    fn anchor_starts_chapter(&self) -> bool {
        unsafe { (self.api().anchor_is_toc)(self.context, Span::new(self.pending_anchor())) != 0 }
    }
    fn record_anchor(&mut self, page: u16) -> Result {
        unsafe {
            (self.api().record_anchor)(self.context, page);
        }
        Ok(())
    }
    fn font_variant(&mut self, font: i32, scale: f32) -> i32 {
        let value = unsafe { (self.api().font_variant)(self.context, font, scale) };
        observe!(self, "resolveFontVariant", font, scale, value);
        if value == 0 {
            font
        } else {
            value
        }
    }
    fn line_height(&mut self, font: i32, compression: f32) -> i32 {
        let value = unsafe { (self.api().line_height)(self.context, font, compression) };
        observe!(self, "getLineHeight", font, compression, value);
        value
    }
    fn table_line_height(&mut self, font: i32) -> i32 {
        let value = unsafe { (self.api().line_height)(self.context, font, 1.0) };
        observe!(self, "getLineHeight", font, value);
        value
    }
    fn ascender(&mut self, font: i32) -> i32 {
        let metrics = unsafe { &*self.api().metrics };
        let value = unsafe { (metrics.ascender)(self.context, font) };
        observe!(self, "getFontAscenderSize", font, value);
        value
    }
    fn space_width(&mut self, font: i32) -> i32 {
        let metrics = unsafe { &*self.api().metrics };
        let value = unsafe { (metrics.space)(self.context, font, 0) };
        observe!(self, "getSpaceWidth", font, 0u8, value);
        value
    }
    fn codepoint(&mut self, font: i32, cp: u32, style: u8) -> Option<(i32, i32)> {
        let (mut advance, mut top) = (0, 0);
        if unsafe { (self.api().codepoint)(self.context, font, cp, style, &mut advance, &mut top) }
            != 0
        {
            Some((advance, top))
        } else {
            None
        }
    }
    fn line_metrics(&mut self, line: &Payload, options: Options) -> LineMetrics {
        unsafe { (self.api().line_metrics)(self.context, line.pointer.as_ptr(), &options, false) }
    }
    fn row_line_metrics(&mut self, line: &Payload, options: Options) -> LineMetrics {
        unsafe { (self.api().line_metrics)(self.context, line.pointer.as_ptr(), &options, true) }
    }
    fn set_line_margin(&mut self, line: &mut Payload, left: i16) {
        unsafe {
            (self.api().set_margin)(line.pointer.as_ptr(), left);
        }
    }
    fn line_links<'a>(&'a mut self, line: &'a mut Payload) -> &'a [LineLink] {
        let mut count = 0;
        let data =
            unsafe { (self.api().line_links)(self.context, line.pointer.as_ptr(), &mut count) };
        if count == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, count as usize) }
        }
    }
    fn complete(&mut self, page: Page<Payload>, position: Position) -> Result {
        let status = unsafe {
            (self.api().complete)(
                self.context,
                page.elements.as_ptr(),
                page.elements.len() as u32,
                page.links.as_ptr(),
                page.links.len() as u32,
                page.footnotes.as_ptr(),
                page.footnotes.len() as u32,
                position,
            )
        };
        for element in page.elements {
            if let Content::Line(payload) | Content::Image(payload) = element.content {
                core::mem::forget(payload);
            }
        }
        match status {
            0 => Ok(()),
            1 => Err(Error::Stopped),
            _ => Err(Error::Allocation),
        }
    }
    fn probe_image(
        &mut self,
        src: &[u8],
        counter: &mut i32,
        popup: &mut bool,
    ) -> Result<ImageProbe<()>> {
        let (mut width, mut height) = (0, 0);
        Ok(
            match unsafe {
                (self.api().probe_image)(
                    self.context,
                    Span::new(src),
                    counter,
                    popup,
                    &mut width,
                    &mut height,
                )
            } {
                0 => ImageProbe::Unsupported,
                1 => ImageProbe::Failed { width, height },
                _ => ImageProbe::Ready {
                    resource: (),
                    width,
                    height,
                },
            },
        )
    }
    fn create_image(&mut self, _: (), width: i32, height: i32) -> Result<Payload> {
        self.payload(unsafe { (self.api().create_image)(self.context, width, height) })
    }
}

pub struct OwnedParagraph(NonNull<paragraph::Paragraph>);
impl OwnedParagraph {
    pub fn pointer(&self) -> *mut paragraph::Paragraph {
        self.0.as_ptr()
    }
    fn get(&self) -> &paragraph::Paragraph {
        unsafe { self.0.as_ref() }
    }
    fn get_mut(&mut self) -> &mut paragraph::Paragraph {
        unsafe { self.0.as_mut() }
    }
}
impl Drop for OwnedParagraph {
    fn drop(&mut self) {
        unsafe {
            ptr::drop_in_place(self.pointer());
            dealloc(
                self.pointer().cast(),
                Allocation::new::<paragraph::Paragraph>(),
            );
        }
    }
}
impl<B> Paragraph<B> for OwnedParagraph {
    fn create<S: Sink<Payload = B>>(style: Block, options: Options, sink: &mut S) -> Result<Self> {
        let context = sink.paragraph_context().ok_or(Error::Allocation)?;
        let pointer =
            NonNull::new(unsafe { alloc(Allocation::new::<paragraph::Paragraph>()).cast() })
                .ok_or(Error::Allocation)?;
        unsafe {
            paragraph::Paragraph::initialize(
                pointer.as_ptr(),
                context.allocator,
                style,
                options.hyphenation,
                options.focus,
                options.indent_spaces,
                (*context.metrics).starts_rtl,
            );
        }
        Ok(Self(pointer))
    }
    fn style(&self) -> Block {
        self.get().block
    }
    fn set_style(&mut self, style: Block) {
        self.get_mut().block = style;
    }
    fn size(&self) -> u32 {
        self.get().tokens.len() as u32
    }
    fn dropped(&self) -> bool {
        self.get().dropped
    }
    fn reset_indent(&mut self) {
        self.get_mut().first_consumed = false;
        self.get_mut().paragraph_emitted = false;
    }
    fn suppress_indent(&mut self) {
        self.get_mut().first_consumed = true;
    }
    fn prepare_font<S: Sink<Payload = B>>(&mut self, sink: &mut S, font: i32, scale: f32) -> i32 {
        let layout_font = sink.font_variant(font, scale);
        let p = self.get_mut();
        for slot in 1..usize::from(p.slots) {
            p.fonts[slot] = sink.font_variant(font, p.scales[slot]);
        }
        p.block.font_id = if layout_font == font { 0 } else { layout_font };
        layout_font
    }
    fn layout<S: Sink<Payload = B>>(
        &mut self,
        sink: &mut S,
        layout: Layout,
        mut emit: impl FnMut(&mut S, Line<B>) -> Result,
    ) -> Result {
        let context = sink.paragraph_context().ok_or(Error::Allocation)?;
        struct Emission<'a, S, F> {
            sink: &'a mut S,
            emit: &'a mut F,
            paragraph: *const paragraph::Paragraph,
            result: Result,
            stop: *mut u8,
        }
        unsafe extern "C" fn receive<
            B,
            S: Sink<Payload = B>,
            F: FnMut(&mut S, Line<B>) -> Result,
        >(
            raw: *mut c_void,
            line: *const paragraph_extract::Output,
        ) -> u8 {
            let emission = &mut *raw.cast::<Emission<'_, S, F>>();
            let line = &*line;
            emission.result = emission.sink.create_line(line).and_then(|payload| {
                (emission.emit)(
                    emission.sink,
                    Line {
                        payload,
                        visible_offset: line.visible_offset,
                        lines_after: (*emission.paragraph).lines_after as u32,
                    },
                )
            });
            if emission.result.is_err() {
                *emission.stop = 1;
            }
            u8::from(emission.result != Err(Error::Allocation))
        }
        fn receiver<B, S: Sink<Payload = B>, F: FnMut(&mut S, Line<B>) -> Result>(
            _: &F,
        ) -> unsafe extern "C" fn(*mut c_void, *const paragraph_extract::Output) -> u8 {
            receive::<B, S, F>
        }
        let receive = receiver::<B, S, _>(&emit);
        let mut stopped = 0u8;
        let mut emission = Emission {
            sink,
            emit: &mut emit,
            paragraph: self.pointer(),
            result: Ok(()),
            stop: &mut stopped,
        };
        unsafe {
            extract(
                self.pointer(),
                layout,
                context,
                &stopped,
                (&mut emission as *mut Emission<'_, S, _>).cast(),
                receive,
            );
        }
        emission.result
    }
}

// Keep the metrics/options frame shared across ordinary, drop-cap and table layout.
#[inline(never)]
unsafe fn extract(
    paragraph: *mut paragraph::Paragraph,
    layout: Layout,
    context: ParagraphContext,
    stop: *const u8,
    receiver: *mut c_void,
    receive: unsafe extern "C" fn(*mut c_void, *const paragraph_extract::Output) -> u8,
) {
    let options = paragraph_extract::Options {
        font: layout.font,
        width: layout.width,
        include_last: u8::from(layout.last),
        tracking: layout.character_spacing,
        spacing: layout.word_spacing,
        max_lines: layout.max_lines,
        language: context.language,
        stop,
        context: receiver,
        emit: receive,
    };
    let prototype = &*context.metrics;
    let metrics = Metrics {
        context: context.context,
        trace: prototype.trace,
        advance: prototype.advance,
        space: prototype.space,
        kerning: prototype.kerning,
        space_advance: prototype.space_advance,
        ascender: prototype.ascender,
        is_sd: prototype.is_sd,
        prepare: prototype.prepare,
        starts_rtl: prototype.starts_rtl,
        visual_order: prototype.visual_order,
    };
    paragraph_extract::layout(paragraph, &metrics, &options);
}
