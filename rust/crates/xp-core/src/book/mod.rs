pub mod cover;
pub mod css;
pub mod io;
pub mod metadata;
// EPUB cache/open ordering. All storage and resource adapters remain borrowed.
use core::ffi::c_void;

#[derive(Clone, Copy)]
#[repr(u32)]
pub enum Operation {
    OpenProtection = 0,
    NewMetadata = 1,
    NewCss = 2,
    LoadMetadata = 3,
    IsText = 4,
    ValidateText = 5,
    BuildText = 6,
    CssInspect = 7,
    CssLoad = 8,
    CssDelete = 9,
    CopyMetadata = 10,
    ParseOpf = 11,
    DiscoverCss = 12,
    ReleaseMetadata = 13,
    ParseCss = 14,
    RemoveSections = 15,
    CssClear = 16,
    SetupCache = 17,
    BeginWrite = 18,
    BeginOpf = 19,
    EndOpf = 20,
    BeginToc = 21,
    HasNav = 22,
    ParseNav = 23,
    HasNcx = 24,
    ParseNcx = 25,
    EndToc = 26,
    EndWrite = 27,
    BuildBook = 28,
    CleanTmp = 29,
    ReleaseWorkMetadata = 30,
}

#[derive(Clone, Copy)]
#[repr(u32)]
pub enum Diagnostic {
    Loading = 0,
    OomCacheCss = 1,
    TextInvalid = 2,
    OomTextCache = 3,
    TextLoaded = 4,
    CssLowMemory = 5,
    CssRebuild = 6,
    CssOpfFailed = 7,
    CssReloadFailed = 8,
    Loaded = 9,
    Building = 10,
    BeginWriteFailed = 11,
    BeginOpfFailed = 12,
    OpfFailed = 13,
    EndOpfFailed = 14,
    OpfTime = 15,
    BeginTocFailed = 16,
    NavAttempt = 17,
    NcxFallback = 18,
    TocFailed = 19,
    EndTocFailed = 20,
    TocTime = 21,
    EndWriteFailed = 22,
    BuildFailed = 23,
    BuildTime = 24,
    TotalTime = 25,
    CleanTmpFailed = 26,
    ReloadFailed = 27,
}

#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub operation: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    pub millis: Option<unsafe extern "C" fn(*mut c_void) -> u32>,
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32)>,
}

impl Services {
    fn call(&self, operation: Operation, argument: u32) -> u32 {
        match self.operation {
            Some(call) => unsafe { call(self.ctx, operation as u32, argument) },
            None => 0,
        }
    }
    fn yes(&self, operation: Operation) -> bool {
        self.call(operation, 0) != 0
    }
    fn log(&self, event: Diagnostic, value: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, value) };
        }
    }
    fn now(&self) -> u32 {
        match self.millis {
            Some(now) => unsafe { now(self.ctx) },
            None => 0,
        }
    }
    fn attempt(&self, operation: Operation, event: Diagnostic) -> bool {
        if self.yes(operation) {
            return true;
        }
        self.log(event, 0);
        false
    }
}

const CACHE_COMPLETE: u32 = 1;
const CACHE_PARTIAL: u32 = 2;
const CACHE_INVALID: u32 = 3;
const LOAD_COMPLETE: u32 = 0;
const LOAD_LOW_MEMORY: u32 = 1;
const LOAD_INVALID: u32 = 2;
const PARSE_COMPLETE: u32 = 0;
const PARSE_PARTIAL: u32 = 1;
const PARSE_ERROR: u32 = 2;

/// The caller guarantees that callbacks and ctx stay valid and serialized throughout
/// the call. Callbacks may use other Rust modules but cannot reenter this operation.
/// No references to callback-owned metadata or resources are retained in Rust.
///
/// # Safety
/// Callback contracts above must hold, including their context pointer validity.
pub unsafe fn load(s: &Services, build_if_missing: bool, skip_css: bool) -> bool {
    use Diagnostic as D;
    use Operation as O;
    if s.operation.is_none() || s.millis.is_none() {
        return false;
    }
    s.log(D::Loading, 0);
    if !s.yes(O::OpenProtection) {
        return false;
    }
    let metadata = s.yes(O::NewMetadata);
    let css = s.yes(O::NewCss);
    if !metadata || !css {
        s.log(D::OomCacheCss, 0);
        return false;
    }
    if s.yes(O::LoadMetadata) {
        if s.yes(O::IsText) {
            if !s.yes(O::ValidateText) {
                s.log(D::TextInvalid, 0);
                if !build_if_missing {
                    return false;
                }
                if !s.attempt(O::NewMetadata, D::OomTextCache) {
                    return false;
                }
                return s.yes(O::BuildText);
            }
            s.log(D::TextLoaded, 0);
            return true;
        }
        if !skip_css {
            let status = s.call(O::CssInspect, 0);
            let loaded = if status == CACHE_COMPLETE {
                s.call(O::CssLoad, 0)
            } else {
                LOAD_INVALID
            };
            if loaded == LOAD_LOW_MEMORY {
                s.log(D::CssLowMemory, 0);
            } else if loaded != LOAD_COMPLETE {
                s.log(D::CssRebuild, 0);
                if status == CACHE_INVALID || (status == CACHE_COMPLETE && loaded == LOAD_INVALID) {
                    s.call(O::CssDelete, 0);
                }
                s.call(O::CopyMetadata, 0);
                let mut parsed = PARSE_ERROR;
                if s.call(O::ParseOpf, 0) == 0 {
                    s.log(D::CssOpfFailed, 0);
                } else {
                    s.call(O::DiscoverCss, 0);
                    s.call(O::ReleaseMetadata, 0);
                    parsed = s.call(O::ParseCss, status);
                }
                s.call(O::ReleaseMetadata, 0);
                if !s.yes(O::NewMetadata) || !s.yes(O::LoadMetadata) {
                    s.log(D::CssReloadFailed, 0);
                    return false;
                }
                if parsed == PARSE_COMPLETE || (parsed == PARSE_PARTIAL && status != CACHE_PARTIAL)
                {
                    s.call(O::RemoveSections, 0);
                }
                s.call(O::ReleaseWorkMetadata, 0);
            }
        }
        s.call(O::CssClear, 0);
        s.log(D::Loaded, 0);
        return true;
    }
    if !build_if_missing {
        return false;
    }
    if s.yes(O::IsText) {
        return s.yes(O::BuildText);
    }
    s.log(D::Building, 0);
    s.call(O::SetupCache, 0);
    let indexing_start = s.now();
    if !s.attempt(O::BeginWrite, D::BeginWriteFailed) {
        return false;
    }
    let opf_start = s.now();
    if !s.attempt(O::BeginOpf, D::BeginOpfFailed) {
        return false;
    }
    if s.call(O::ParseOpf, 1) == 0 {
        s.log(D::OpfFailed, 0);
        return false;
    }
    s.call(O::DiscoverCss, 0);
    if !s.attempt(O::EndOpf, D::EndOpfFailed) {
        return false;
    }
    s.log(D::OpfTime, s.now().wrapping_sub(opf_start));
    let toc_start = s.now();
    if !s.attempt(O::BeginToc, D::BeginTocFailed) {
        return false;
    }
    let mut toc_parsed = false;
    if s.yes(O::HasNav) {
        s.log(D::NavAttempt, 0);
        toc_parsed = s.yes(O::ParseNav);
    }
    if !toc_parsed && s.yes(O::HasNcx) {
        s.log(D::NcxFallback, 0);
        toc_parsed = s.yes(O::ParseNcx);
    }
    if !toc_parsed {
        s.log(D::TocFailed, 0);
    }
    if !s.attempt(O::EndToc, D::EndTocFailed) {
        return false;
    }
    s.log(D::TocTime, s.now().wrapping_sub(toc_start));
    if !s.attempt(O::EndWrite, D::EndWriteFailed) {
        return false;
    }
    let build_start = s.now();
    if !s.attempt(O::BuildBook, D::BuildFailed) {
        return false;
    }
    s.log(D::BuildTime, s.now().wrapping_sub(build_start));
    s.log(D::TotalTime, s.now().wrapping_sub(indexing_start));
    if !s.yes(O::CleanTmp) {
        s.log(D::CleanTmpFailed, 0);
    }
    if !skip_css {
        s.call(O::ReleaseMetadata, 0);
        let status = s.call(O::CssInspect, 0);
        if s.call(O::ParseCss, status) != PARSE_ERROR {
            s.call(O::RemoveSections, 0);
        }
    }
    if !s.yes(O::NewMetadata) || !s.yes(O::LoadMetadata) {
        s.log(D::ReloadFailed, 0);
        return false;
    }
    s.log(D::Loaded, 0);
    true
}

pub fn progress(book_size: usize, previous: usize, current: usize, fraction: f32) -> f32 {
    if book_size == 0 {
        return 0.0;
    }
    let chapter = current.wrapping_sub(previous);
    let section = fraction * chapter as f32;
    let total = previous as f32 + section;
    total / book_size as f32
}
