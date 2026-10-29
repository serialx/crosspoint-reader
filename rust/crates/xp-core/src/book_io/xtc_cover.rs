//! Cover transaction over caller-owned files and pixel buffers.
use super::xtc_image::{self, Thumbnail};
use core::ffi::c_void;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Info {
    pub width: u16,
    pub height: u16,
    pub depth: u8,
}
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub call: unsafe extern "C" fn(*mut c_void, u32) -> usize,
    pub info: unsafe extern "C" fn(*mut c_void) -> Info,
    pub prepare: unsafe extern "C" fn(*mut c_void, usize, usize, *const Thumbnail) -> bool,
    pub diagnostic: unsafe extern "C" fn(*mut c_void, u32, usize),
}
#[repr(u32)]
enum Op {
    Exists,
    Ready,
    Pages,
    Setup,
    First,
    GenerateCover,
    OpenSource,
    OpenTarget,
    Copy,
    Load,
    OpenImage,
    Render,
}
impl Services {
    fn call(&self, op: Op) -> usize {
        unsafe { (self.call)(self.ctx, op as u32) }
    }
    fn log(&self, e: u32, n: usize) {
        unsafe { (self.diagnostic)(self.ctx, e, n) }
    }
}
/// # Safety
/// Callbacks own buffers/files for the call and synchronously copy the borrowed plan.
pub unsafe fn generate(s: &Services, thumb: bool, height: i32) -> bool {
    use Op as O;
    if s.call(O::Exists) != 0 {
        return true;
    }
    if s.call(O::Ready) == 0 {
        s.log(if thumb { 1 } else { 0 }, 0);
        return false;
    }
    if s.call(O::Pages) == 0 {
        s.log(2, 0);
        return false;
    }
    s.call(O::Setup);
    if s.call(O::First) == 0 {
        s.log(3, 0);
        return false;
    }
    let info = (s.info)(s.ctx);
    let plan = if thumb {
        Some(xtc_image::thumbnail(info.width, info.height, height))
    } else {
        None
    };
    if plan.as_ref().is_some_and(|p| p.scale >= 1.0) {
        if s.call(O::GenerateCover) == 0 {
            return false;
        }
        if s.call(O::OpenSource) != 0 && s.call(O::OpenTarget) != 0 && s.call(O::Copy) == 0 {
            return false;
        }
        s.log(4, 0);
        return s.call(O::Exists) != 0;
    }
    let bitmap = crate::xtc::bitmap_size(info.width, info.height, info.depth) as usize;
    let row = if let Some(p) = &plan {
        ((p.width as u32).div_ceil(32) * 4) as usize
    } else if info.depth == 2 {
        ((info.width as u32 * 2).div_ceil(32) * 4) as usize
    } else {
        0
    };
    // The prepare callback records dimensions before its existing allocation.
    if !(s.prepare)(
        s.ctx,
        bitmap,
        row,
        plan.as_ref().map_or(core::ptr::null(), |p| p),
    ) {
        s.log(if thumb { 6 } else { 5 }, bitmap + row);
        return false;
    }
    if s.call(O::Load) == 0 {
        s.log(if thumb { 8 } else { 7 }, 0);
        return false;
    }
    if s.call(O::OpenImage) == 0 {
        s.log(if thumb { 10 } else { 9 }, 0);
        return false;
    }
    s.call(O::Render);
    s.log(if thumb { 12 } else { 11 }, 0);
    true
}
