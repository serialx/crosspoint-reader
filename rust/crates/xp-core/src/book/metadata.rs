use core::ffi::c_void;
#[repr(u32)]
enum Operation {
    ClearOutput = 0,
    IsText = 1,
    TextTitle = 2,
    NewCache = 3,
    LoadCache = 4,
    CopyCache = 5,
    DropCache = 6,
    LoadSource = 7,
    OpenSource = 8,
    ParseSource = 9,
    CloseSource = 10,
    CopySource = 11,
    FindOpf = 12,
    SizeOpf = 13,
    SetupOpf = 14,
    ReadOpf = 15,
    CopySync = 16,
    CacheExists = 17,
    CacheRemove = 18,
    CacheMkdir = 19,
}
#[repr(u32)]
enum Diagnostic {
    CacheOom = 0,
    SourceOpen = 1,
    SyncOpen = 2,
    CacheAbsent = 3,
    CacheClearFailed = 4,
    CacheCleared = 5,
}
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub operation: unsafe extern "C" fn(*mut c_void, u32) -> u8,
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32)>,
}
impl Services {
    fn call(&self, op: Operation) -> bool {
        unsafe { (self.operation)(self.ctx, op as u32) != 0 }
    }
    fn log(&self, event: Diagnostic) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32) };
        }
    }
}
use Diagnostic as D;
use Operation as O;
/// # Safety
/// The facade must supply valid synchronous callbacks and a live context.
pub unsafe fn metadata(s: &Services) -> bool {
    s.call(O::ClearOutput);
    if s.call(O::IsText) {
        s.call(O::TextTitle);
        return true;
    }
    let allocated = s.call(O::NewCache);
    if allocated && s.call(O::LoadCache) {
        s.call(O::CopyCache);
        return true;
    }
    if !allocated {
        s.log(D::CacheOom);
    }
    s.call(O::DropCache);
    s.call(O::LoadSource)
}
/// # Safety
/// The facade must supply valid synchronous callbacks and a live context.
pub unsafe fn source(s: &Services) -> bool {
    if !s.call(O::OpenSource) {
        s.log(D::SourceOpen);
        return false;
    }
    let parsed = s.call(O::ParseSource);
    s.call(O::CloseSource);
    if !parsed {
        return false;
    }
    s.call(O::CopySource);
    true
}
/// # Safety
/// The facade must supply valid synchronous callbacks and a live context.
pub unsafe fn sync(s: &Services) -> bool {
    s.call(O::ClearOutput);
    if !s.call(O::OpenSource) {
        s.log(D::SyncOpen);
        return false;
    }
    if !s.call(O::FindOpf) || !s.call(O::SizeOpf) || !s.call(O::SetupOpf) {
        s.call(O::CloseSource);
        return false;
    }
    let read = s.call(O::ReadOpf);
    s.call(O::CloseSource);
    if !read {
        return false;
    }
    s.call(O::CopySync);
    true
}
/// # Safety
/// The facade must supply valid synchronous callbacks and a live context.
pub unsafe fn clear_cache(s: &Services) -> bool {
    if !s.call(O::CacheExists) {
        s.log(D::CacheAbsent);
        return true;
    }
    if !s.call(O::CacheRemove) {
        s.log(D::CacheClearFailed);
        return false;
    }
    s.log(D::CacheCleared);
    true
}
/// # Safety
/// The facade must supply valid synchronous callbacks and a live context.
pub unsafe fn setup_cache(s: &Services) {
    if !s.call(O::CacheExists) {
        s.call(O::CacheMkdir);
    }
}
