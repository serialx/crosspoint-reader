//! Synchronous borrowed diagnostics; no parser-owned state or allocation.
use core::ffi::c_void;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Diagnostics {
    pub context: *mut c_void,
    pub diagnostic: Option<
        unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32, *const u8, u32, *const u8, u32),
    >,
}
pub const SILENT: Diagnostics = Diagnostics {
    context: core::ptr::null_mut(),
    diagnostic: None,
};
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Event {
    NavBegin = 1,
    NavEnd,
    MetadataTruncated,
    CollectionIgnored,
    CoverIgnored,
    NcxDuplicate,
    NavDocument,
    GuideStart,
    GuideCover,
    ManifestIndex,
    GuideBegin,
    IndexOom = 20,
    IndexReady,
    SpineClosed,
    TocClosed,
    MissingTocHref,
    MissingSpineTitle,
}
impl Diagnostics {
    pub fn emit(&self, event: Event, a: u32, b: u32, text: &[u8], other: &[u8]) {
        if let Some(f) = self.diagnostic {
            unsafe {
                f(
                    self.context,
                    event as u32,
                    a,
                    b,
                    0,
                    0,
                    text.as_ptr(),
                    text.len() as u32,
                    other.as_ptr(),
                    other.len() as u32,
                )
            }
        }
    }
}
#[repr(C)]
#[derive(Default)]
pub struct Error {
    pub code: u32,
    pub line: u32,
    pub buffer: u8,
}
