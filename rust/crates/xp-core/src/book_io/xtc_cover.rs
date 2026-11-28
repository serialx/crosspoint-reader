//! Cover transaction over caller-owned files and pixel buffers.
use super::xtc_image::{self, Thumbnail};
use crate::book::residual_abi::*;
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
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, usize)>,
}
impl Services {
    fn call(&self, op: u32) -> usize {
        unsafe { (self.call)(self.ctx, op) }
    }
    fn log(&self, e: u32, n: usize) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, e, n) }
        }
    }
}
/// # Safety
/// Callbacks own buffers/files for the call and synchronously copy the borrowed plan.
pub unsafe fn generate(s: &Services, thumb: bool, height: i32) -> bool {
    if s.call(XP_BOOK_XTC_EXISTS) != 0 {
        return true;
    }
    if s.call(XP_BOOK_XTC_READY) == 0 {
        s.log(
            if thumb {
                XP_BOOK_XTC_LOG_THUMB_UNLOADED
            } else {
                XP_BOOK_XTC_LOG_COVER_UNLOADED
            },
            0,
        );
        return false;
    }
    if s.call(XP_BOOK_XTC_PAGES) == 0 {
        s.log(XP_BOOK_XTC_LOG_NO_PAGES, 0);
        return false;
    }
    s.call(XP_BOOK_XTC_SETUP);
    if s.call(XP_BOOK_XTC_FIRST) == 0 {
        s.log(XP_BOOK_XTC_LOG_PAGE_INFO, 0);
        return false;
    }
    let info = (s.info)(s.ctx);
    let plan = if thumb {
        Some(xtc_image::thumbnail(info.width, info.height, height))
    } else {
        None
    };
    if plan.as_ref().is_some_and(|p| p.scale >= 1.0) {
        if s.call(XP_BOOK_XTC_GENERATE_COVER) == 0 {
            return false;
        }
        if s.call(XP_BOOK_XTC_OPEN_SOURCE) != 0
            && s.call(XP_BOOK_XTC_OPEN_TARGET) != 0
            && s.call(XP_BOOK_XTC_COPY) == 0
        {
            return false;
        }
        s.log(XP_BOOK_XTC_LOG_COPIED, 0);
        return s.call(XP_BOOK_XTC_EXISTS) != 0;
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
        s.log(
            if thumb {
                XP_BOOK_XTC_LOG_THUMB_OOM
            } else {
                XP_BOOK_XTC_LOG_COVER_OOM
            },
            bitmap + row,
        );
        return false;
    }
    if s.call(XP_BOOK_XTC_LOAD) == 0 {
        s.log(
            if thumb {
                XP_BOOK_XTC_LOG_THUMB_READ
            } else {
                XP_BOOK_XTC_LOG_COVER_READ
            },
            0,
        );
        return false;
    }
    if s.call(XP_BOOK_XTC_OPEN_IMAGE) == 0 {
        s.log(
            if thumb {
                XP_BOOK_XTC_LOG_THUMB_OPEN
            } else {
                XP_BOOK_XTC_LOG_COVER_OPEN
            },
            0,
        );
        return false;
    }
    s.call(XP_BOOK_XTC_RENDER);
    s.log(
        if thumb {
            XP_BOOK_XTC_LOG_THUMB_DONE
        } else {
            XP_BOOK_XTC_LOG_COVER_DONE
        },
        0,
    );
    true
}
