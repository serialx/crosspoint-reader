//! Physical display boundary; ownership and logical rendering stay in Rust.
use super::PhysicalResources;

#[repr(u8)]
#[derive(Clone, Copy)]
pub(crate) enum Query {
    IsBw,
    IsX3,
    IsUc8279,
    AbsoluteSupported,
    OverlayCaps,
    AbsoluteCaps,
    StripBoard,
    WidthBytes,
    Height,
    FreeHeap,
    MaxBlock,
    #[cfg_attr(not(feature = "reader-strips"), allow(dead_code))]
    Mode,
    Inverted,
    #[cfg_attr(not(feature = "reader-strips"), allow(dead_code))]
    Width,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub(crate) enum Kind {
    Clear,
    Display,
    DisplayAsync,
    GrayBase,
    AbsoluteBase,
    Precondition,
    #[cfg_attr(not(feature = "reader-strips"), allow(dead_code))]
    Mode,
    StripBegin,
    StripEnd,
    Wait,
    WritePlane,
    GrayDisplay,
    Cleanup,
    StoreBw,
    RestoreBw,
    CopyLsb,
    CopyMsb,
    #[cfg_attr(not(feature = "reader-strips"), allow(dead_code))]
    CopyStatus,
}

#[repr(C)]
pub(crate) struct Op {
    pub(crate) kind: u8,
    pub(crate) mode: u8,
    pub(crate) y: u16,
    pub(crate) rows: u16,
    pub(crate) reserved: u16,
    pub(crate) value: u32,
    pub(crate) buffer: *mut u8,
}
impl Op {
    pub(crate) fn new(kind: Kind) -> Self {
        Self {
            kind: kind as u8,
            mode: 0,
            y: 0,
            rows: 0,
            reserved: 0,
            value: 0,
            buffer: core::ptr::null_mut(),
        }
    }
}

/// Synchronous physical calls; retained plan/source loans end at explicit detach.
pub(crate) trait Physical: PhysicalResources {
    fn query(&self, query: Query) -> u32;
    fn operation(&self, op: &Op) -> u32;
    #[cfg(feature = "reader-strips")]
    fn plan(
        &self,
        plan: *mut crate::page_render::Plan,
        bounds: *mut u8,
        count: u16,
        recording: bool,
    );
    #[cfg(feature = "reader-strips")]
    fn image_window(&self, bytes: *mut u8, length: u32);
    #[cfg(feature = "reader-strips")]
    fn source(&self, source: *mut crate::display_source::DisplaySource) -> bool;
    #[cfg(feature = "reader-strips")]
    fn framebuffer(&self) -> (*mut u8, usize);
}
