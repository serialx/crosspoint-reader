use super::css::{raster_kind, Span};
use core::ffi::c_void;
#[repr(u32)]
enum Operation {
    TargetExists = 0,
    CacheLoaded = 1,
    CopyCover = 2,
    BorrowCover = 3,
    IsText = 4,
    ConvertText = 5,
    CompanionCover = 6,
    NewSource = 7,
    SourceZipOpen = 8,
    ParseSource = 9,
    ReleaseSourceZip = 10,
    HasProtection = 11,
    OpenProtection = 12,
    SetupCache = 13,
    SourceCover = 14,
    MakeRasterSource = 15,
    ExtractRaster = 16,
    OpenSource = 17,
    OpenTarget = 18,
    CloseSource = 19,
    CloseTarget = 20,
    RemoveSource = 21,
    RemoveTarget = 22,
    CreateEmpty = 23,
    PrepareGifTarget = 24,
    Lock = 25,
    Unlock = 26,
    MakeGifPaths = 27,
    ExtractGif = 28,
    RenameTarget = 29,
    ExistsSource = 30,
    ExistsTargetTemp = 31,
    RemoveTargetTemp = 32,
    ReleaseGifHandles = 33,
}
#[repr(u32)]
enum Diagnostic {
    CacheCover = 0,
    EmptyCover = 1,
    JpegCoverStart = 2,
    PngCoverStart = 3,
    CoverConvertFailed = 4,
    JpegCoverEnd = 5,
    PngCoverEnd = 6,
    UnsupportedCover = 7,
    CacheThumb = 8,
    OomSource = 9,
    OpenSourceMetadata = 10,
    EmptyThumb = 11,
    JpegThumbStart = 12,
    PngThumbStart = 13,
    JpegThumbFailed = 14,
    PngThumbFailed = 15,
    JpegThumbEnd = 16,
    PngThumbEnd = 17,
    UnsupportedThumb = 18,
    GifLock = 19,
    GifOpenSource = 20,
    GifOpenTarget = 21,
    GifRename = 22,
    GifRemoveTemp = 23,
    PngCoverFailed = 24,
}
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub operation: unsafe extern "C" fn(*mut c_void, u32, u32) -> u8,
    pub href: unsafe extern "C" fn(*mut c_void) -> Span,
    pub convert: unsafe extern "C" fn(*mut c_void, u32, u8, i32, i32) -> u8,
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32)>,
}
impl Services {
    fn call(&self, op: Operation, argument: u32) -> bool {
        unsafe { (self.operation)(self.ctx, op as u32, argument) != 0 }
    }
    fn log(&self, event: Diagnostic, argument: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, argument) };
        }
    }
    fn href(&self) -> Span {
        unsafe { (self.href)(self.ctx) }
    }
    fn convert(&self, kind: u32, thumb: bool, height: i32) -> bool {
        let width = if thumb {
            (height as f64 * 0.6) as i32
        } else {
            0
        };
        unsafe { (self.convert)(self.ctx, kind, thumb as u8, width, height) != 0 }
    }
}
use Diagnostic as D;
use Operation as O;
fn remove_gif(s: &Services, target: bool) {
    let (remove, exists) = if target {
        (O::RemoveTargetTemp, O::ExistsTargetTemp)
    } else {
        (O::RemoveSource, O::ExistsSource)
    };
    if !s.call(remove, 0) && s.call(exists, 0) {
        s.log(D::GifRemoveTemp, target as u32);
    }
}
fn gif(s: &Services, thumb: bool, height: i32) -> bool {
    s.call(O::PrepareGifTarget, 0);
    if !s.call(O::Lock, 0) {
        s.log(D::GifLock, 0);
        return false;
    }
    let result = gif_locked(s, thumb, height);
    s.call(O::ReleaseGifHandles, 0);
    s.call(O::Unlock, 0);
    result
}
fn gif_locked(s: &Services, thumb: bool, height: i32) -> bool {
    if s.call(O::TargetExists, 0) {
        return true;
    }
    s.call(O::MakeGifPaths, 0);
    if !s.call(O::ExtractGif, 0) {
        return false;
    }
    if !s.call(O::OpenSource, 0) {
        s.log(D::GifOpenSource, 0);
        remove_gif(s, false);
        return false;
    }
    if !s.call(O::OpenTarget, 0) {
        s.log(D::GifOpenTarget, 0);
        s.call(O::CloseSource, 0);
        remove_gif(s, false);
        remove_gif(s, true);
        return false;
    }
    let converted = s.convert(3, thumb, height);
    let source_closed = s.call(O::CloseSource, 0);
    let output_closed = s.call(O::CloseTarget, 0);
    let mut success = converted && source_closed && output_closed;
    remove_gif(s, false);
    if success && !s.call(O::RenameTarget, 0) {
        s.log(D::GifRename, 0);
        success = false;
    }
    if !success {
        remove_gif(s, true);
    }
    success
}
fn raster(s: &Services, kind: u32, thumb: bool, height: i32) -> bool {
    let (start, failed, end) = match (kind, thumb) {
        (1, false) => (D::JpegCoverStart, D::CoverConvertFailed, D::JpegCoverEnd),
        (2, false) => (D::PngCoverStart, D::PngCoverFailed, D::PngCoverEnd),
        (1, true) => (D::JpegThumbStart, D::JpegThumbFailed, D::JpegThumbEnd),
        _ => (D::PngThumbStart, D::PngThumbFailed, D::PngThumbEnd),
    };
    s.log(start, 0);
    s.call(O::MakeRasterSource, kind);
    if !s.call(O::ExtractRaster, 0) || !s.call(O::OpenSource, 0) || !s.call(O::OpenTarget, 0) {
        return false;
    }
    let success = s.convert(kind, thumb, height);
    s.call(O::CloseSource, 0);
    s.call(O::CloseTarget, 0);
    s.call(O::RemoveSource, 0);
    if !success {
        s.log(failed, 0);
        s.call(O::RemoveTarget, 0);
    }
    s.log(end, success as u32);
    success
}
unsafe fn from_href(s: &Services, thumb: bool, height: i32) -> bool {
    let href = s.href();
    if href.len == 0 {
        s.log(if thumb { D::EmptyThumb } else { D::EmptyCover }, 0);
        return false;
    }
    if s.call(O::IsText, 0) {
        return s.call(O::ConvertText, 0);
    }
    let href = s.href();
    if href.data.is_null() || href.len > isize::MAX as usize {
        return false;
    }
    let kind = raster_kind(unsafe { core::slice::from_raw_parts(href.data, href.len) });
    match kind {
        1 | 2 => raster(s, kind, thumb, height),
        3 => gif(s, thumb, height),
        _ => {
            s.log(
                if thumb {
                    D::UnsupportedThumb
                } else {
                    D::UnsupportedCover
                },
                0,
            );
            if thumb {
                s.call(O::CreateEmpty, 0);
            }
            false
        }
    }
}
/// # Safety
/// Callbacks and context remain valid through return. href is borrowed and stable
/// until the next operation; conversion callbacks do not retain any file or buffer.
pub unsafe fn generate(s: &Services, mode: u32, height: i32) -> bool {
    if mode > 2 {
        return false;
    }
    if s.call(O::TargetExists, 0) {
        return true;
    }
    match mode {
        0 | 1 => {
            if !s.call(O::CacheLoaded, 0) {
                s.log(
                    if mode == 0 {
                        D::CacheCover
                    } else {
                        D::CacheThumb
                    },
                    0,
                );
                return false;
            }
            s.call(
                if mode == 0 {
                    O::CopyCover
                } else {
                    O::BorrowCover
                },
                0,
            );
        }
        2 => {
            if s.call(O::IsText, 0) {
                s.call(O::CompanionCover, 0);
                if s.href().len == 0 {
                    return false;
                }
                s.call(O::SetupCache, 0);
                return unsafe { from_href(s, true, height) };
            }
            if !s.call(O::NewSource, 0) {
                s.log(D::OomSource, 0);
                return false;
            }
            if !s.call(O::SourceZipOpen, 0) {
                s.log(D::OpenSourceMetadata, 0);
                return false;
            }
            if !s.call(O::ParseSource, 0) {
                return false;
            }
            s.call(O::ReleaseSourceZip, 0);
            if !s.call(O::HasProtection, 0) && !s.call(O::OpenProtection, 0) {
                return false;
            }
            s.call(O::SetupCache, 0);
            s.call(O::SourceCover, 0);
        }
        _ => {}
    }
    unsafe { from_href(s, mode != 0, height) }
}

#[repr(C)]
pub struct GifExtractServices {
    pub ctx: *mut c_void,
    pub operation: unsafe extern "C" fn(*mut c_void, u32) -> u8,
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32)>,
}
#[repr(u32)]
enum GifOperation {
    Open,
    Stream,
    Close,
    Remove,
    Exists,
}
#[repr(u32)]
enum GifDiagnostic {
    Open,
    Stream,
    Remove,
}
impl GifExtractServices {
    fn call(&self, operation: GifOperation) -> bool {
        unsafe { (self.operation)(self.ctx, operation as u32) != 0 }
    }
    fn log(&self, event: GifDiagnostic) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32) };
        }
    }
}
/// # Safety
/// Services and ctx must stay valid through the call, with synchronous streaming.
pub unsafe fn extract_gif(s: &GifExtractServices) -> bool {
    let mut success = false;
    if !s.call(GifOperation::Open) {
        s.log(GifDiagnostic::Open);
    } else {
        let streamed = s.call(GifOperation::Stream);
        let closed = s.call(GifOperation::Close);
        success = streamed && closed;
        if !success {
            s.log(GifDiagnostic::Stream);
        }
    }
    s.call(GifOperation::Close);
    if !success && !s.call(GifOperation::Remove) && s.call(GifOperation::Exists) {
        s.log(GifDiagnostic::Remove);
    }
    success
}
