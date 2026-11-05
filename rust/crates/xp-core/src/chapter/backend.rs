//! Rust-only Chapter services; native dictionary callbacks keep their existing ABI.
use super::{
    assembly::{self, runtime::Callbacks, ImageProbe, Position, Result},
    services::{Span, Trace},
    State,
};
use crate::para::{paragraph::Metrics, Allocator};
use core::{ffi::c_void, ptr::NonNull};

/// Reader-owned services are address-stable and disjoint from Chapter state.
///
/// # Safety
/// Physical tables and returned entity spans remain valid for their synchronous use.
/// Entity buffers are writable exclusive loans until replacement. Methods may mutate
/// only interior fields; no method may feed, destroy or reborrow the Chapter owner.
/// Table visitors receive the caller's fresh State pointer and must finish before return.
pub unsafe trait ReaderServices {
    fn metrics(&self) -> &Metrics;
    fn allocator(&self) -> &Allocator;
    fn language(&self) -> i32;
    fn entity_lookup(&self, spelling: &[u8]) -> Option<&[u8]>;
    fn entity_buffer(&self, minimum: u32) -> Option<NonNull<[u8]>>;
    fn font_variant(&self, font: i32, scale: f32) -> i32;
    fn line_height(&self, font: i32, compression: f32) -> i32;
    fn codepoint(&self, font: i32, codepoint: u32, style: u8) -> Option<(i32, i32)>;
    fn font_ready(&self, font: i32, fallback: i32) -> i32;
    fn complete(&self, bytes: &[u8], position: Position) -> Result;
    fn probe_image(
        &self,
        source: &[u8],
        counter: &mut i32,
        popup: &mut bool,
    ) -> Result<ImageProbe<()>>;
    fn image_paths(&self) -> (&[u8], &[u8]);
    fn pending_anchor(&self) -> &[u8];
    fn defer_anchor(&self, text: &[u8]) -> Result;
    fn toc_anchor(&self, index: u32) -> Option<&[u8]>;
    fn anchor_count(&self) -> u32;
    fn record_anchor(&self, page: u16) -> Result;
    /// # Safety
    /// State and visitor are the current synchronous reborrow, not a retained owner pointer.
    unsafe fn table_file(&self, state: *mut State, visitor: super::table::UseReader);
    fn trace(&self, record: &Trace);
    fn trace_bytes(&self, bytes: &[u8]);
    fn diagnostic(&self, event: assembly::Diagnostic, a: u32, b: u32, c: u32, text: &[u8]);
}

#[repr(u32)]
enum NativeOutput {
    HasByteSink,
    Bytes,
    Page,
}

#[derive(Clone, Copy)]
pub(super) enum Backend {
    Native {
        callbacks: *const Callbacks,
        context: *mut c_void,
    },
    Reader {
        services: NonNull<dyn ReaderServices>,
    },
}

#[derive(Clone, Copy)]
pub(crate) enum Reclaim {
    Native(unsafe extern "C" fn()),
    Reader(*const Allocator),
}
impl Reclaim {
    pub(crate) fn call(self) {
        unsafe {
            match self {
                Self::Native(call) => call(),
                Self::Reader(allocator) => {
                    if let Some(reclaim) = (*allocator).reclaim {
                        reclaim(4096);
                    }
                }
            }
        }
    }
}

unsafe fn bytes<'a>(span: Span) -> &'a [u8] {
    if span.len == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(span.data, span.len as usize) }
    }
}

impl Backend {
    pub(super) fn metrics(&self) -> (&Metrics, *mut c_void) {
        unsafe {
            match self {
                Self::Native { callbacks, context } => (&*(**callbacks).metrics, *context),
                Self::Reader { services } => {
                    let metrics = services.as_ref().metrics();
                    (metrics, metrics.context)
                }
            }
        }
    }
    pub(super) fn allocator(&self) -> *const Allocator {
        unsafe {
            match self {
                Self::Native { callbacks, .. } => ((**callbacks).allocator)(),
                Self::Reader { services } => services.as_ref().allocator(),
            }
        }
    }
    pub(super) fn language(&self) -> i32 {
        unsafe {
            match self {
                Self::Native { callbacks, .. } => ((**callbacks).language)(),
                Self::Reader { services } => services.as_ref().language(),
            }
        }
    }
    pub(super) fn reclaim(&self) -> Reclaim {
        unsafe {
            match self {
                Self::Native { callbacks, .. } => {
                    Reclaim::Native((**callbacks).reclaim_paragraph_memory)
                }
                Self::Reader { services } => Reclaim::Reader(services.as_ref().allocator()),
            }
        }
    }
    pub(super) fn entity_lookup(&self, spelling: Span) -> Span {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    ((**callbacks).entity_lookup)(*context, spelling)
                }
                Self::Reader { services } => {
                    services.as_ref().entity_lookup(bytes(spelling)).map_or(
                        Span {
                            data: core::ptr::null(),
                            len: 0,
                        },
                        Span::new,
                    )
                }
            }
        }
    }
    pub(super) fn entity_buffer(&self, minimum: u32) -> Span {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    ((**callbacks).entity_buffer)(*context, minimum)
                }
                Self::Reader { services } => {
                    let Some(buffer) = services.as_ref().entity_buffer(minimum) else {
                        return Span {
                            data: core::ptr::null(),
                            len: 0,
                        };
                    };
                    let Ok(len) = u32::try_from(buffer.len()) else {
                        return Span {
                            data: core::ptr::null(),
                            len: 0,
                        };
                    };
                    Span {
                        data: buffer.as_ptr().cast(),
                        len,
                    }
                }
            }
        }
    }
    pub(super) fn font_variant(&self, font: i32, scale: f32) -> i32 {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    ((**callbacks).font_variant)(*context, font, scale)
                }
                Self::Reader { services } => services.as_ref().font_variant(font, scale),
            }
        }
    }
    pub(super) fn line_height(&self, font: i32, compression: f32) -> i32 {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    ((**callbacks).line_height)(*context, font, compression)
                }
                Self::Reader { services } => services.as_ref().line_height(font, compression),
            }
        }
    }
    pub(super) fn font_ready(&self, font: i32, fallback: i32) -> i32 {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    ((**callbacks).font_ready)(*context, font, fallback)
                }
                Self::Reader { services } => services.as_ref().font_ready(font, fallback),
            }
        }
    }
    pub(super) fn codepoint(&self, font: i32, codepoint: u32, style: u8) -> Option<(i32, i32)> {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    let (mut advance, mut top) = (0, 0);
                    (((**callbacks).codepoint.unwrap())(
                        *context,
                        font,
                        codepoint,
                        style,
                        &mut advance,
                        &mut top,
                    ) != 0)
                        .then_some((advance, top))
                }
                Self::Reader { services } => services.as_ref().codepoint(font, codepoint, style),
            }
        }
    }
    pub(super) fn complete(&self, data: &[u8], position: Position) -> Result {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    let complete = (**callbacks).complete;
                    let output = if complete(
                        *context,
                        NativeOutput::HasByteSink as u32,
                        Span::new(&[]),
                        position,
                    ) != 0
                    {
                        NativeOutput::Bytes
                    } else {
                        NativeOutput::Page
                    };
                    match complete(*context, output as u32, Span::new(data), position) {
                        0 => Ok(()),
                        1 => Err(assembly::Error::Stopped),
                        _ => Err(assembly::Error::Allocation),
                    }
                }
                Self::Reader { services } => services.as_ref().complete(data, position),
            }
        }
    }
    pub(super) fn probe_image(
        &self,
        source: &[u8],
        counter: &mut i32,
        popup: &mut bool,
    ) -> Result<ImageProbe<()>> {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    let (mut width, mut height) = (0, 0);
                    Ok(
                        match ((**callbacks).probe_image)(
                            *context,
                            Span::new(source),
                            counter,
                            popup,
                            &mut width,
                            &mut height,
                        ) {
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
                Self::Reader { services } => services.as_ref().probe_image(source, counter, popup),
            }
        }
    }
    pub(super) fn image_paths(&self) -> (&[u8], &[u8]) {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    let (mut path, mut source) = (Span::new(&[]), Span::new(&[]));
                    ((**callbacks).image_paths)(*context, &mut path, &mut source);
                    (bytes(path), bytes(source))
                }
                Self::Reader { services } => services.as_ref().image_paths(),
            }
        }
    }
    pub(super) fn pending_anchor(&self) -> &[u8] {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    bytes(((**callbacks).pending_anchor)(*context))
                }
                Self::Reader { services } => services.as_ref().pending_anchor(),
            }
        }
    }
    pub(super) fn defer_anchor(&self, text: Span) -> Result {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    ((**callbacks).defer_anchor)(*context, text);
                    Ok(())
                }
                Self::Reader { services } => services.as_ref().defer_anchor(bytes(text)),
            }
        }
    }
    pub(super) fn toc_anchor(&self, index: u32) -> Option<&[u8]> {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    let span = ((**callbacks).toc_anchor)(*context, index);
                    if span.data.is_null() {
                        None
                    } else {
                        Some(bytes(span))
                    }
                }
                Self::Reader { services } => services.as_ref().toc_anchor(index),
            }
        }
    }
    pub(super) fn anchor_count(&self) -> u32 {
        unsafe {
            match self {
                Self::Native { callbacks, context } => ((**callbacks).anchor_count)(*context),
                Self::Reader { services } => services.as_ref().anchor_count(),
            }
        }
    }
    pub(super) fn record_anchor(&self, page: u16) -> Result {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    ((**callbacks).record_anchor)(*context, page);
                    Ok(())
                }
                Self::Reader { services } => services.as_ref().record_anchor(page),
            }
        }
    }
    pub(super) unsafe fn table_file(&self, state: *mut State, visitor: super::table::UseReader) {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    ((**callbacks).table_file)(*context, state, visitor)
                }
                Self::Reader { services } => services.as_ref().table_file(state, visitor),
            }
        }
    }
    #[cfg(feature = "chapter-trace")]
    pub(super) fn trace(&self, record: &Trace) {
        unsafe {
            match self {
                Self::Native { callbacks, context } => ((**callbacks).trace)(*context, record),
                Self::Reader { services } => services.as_ref().trace(record),
            }
        }
    }
    #[cfg(feature = "chapter-trace")]
    pub(super) fn trace_bytes(&self, data: &[u8]) {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    ((**callbacks).trace_bytes)(*context, Span::new(data))
                }
                Self::Reader { services } => services.as_ref().trace_bytes(data),
            }
        }
    }
    pub(super) fn diagnostic(
        &self,
        event: assembly::Diagnostic,
        a: u32,
        b: u32,
        c: u32,
        text: &[u8],
    ) {
        unsafe {
            match self {
                Self::Native { callbacks, context } => {
                    if let Some(call) = (**callbacks).diagnostic {
                        call(
                            *context,
                            event as u32,
                            a,
                            b,
                            c,
                            0,
                            text.as_ptr(),
                            text.len() as u32,
                        );
                    }
                }
                Self::Reader { services } => services.as_ref().diagnostic(event, a, b, c, text),
            }
        }
    }
}
