//! Package pass sequencing; C++ owns parser/file/string adapter lifetimes.
use super::residual::Span;
use crate::book::residual_abi::*;
use crate::epub::opf;
use core::ffi::c_void;
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub call: unsafe extern "C" fn(*mut c_void, u32, usize) -> usize,
    pub value: unsafe extern "C" fn(*mut c_void, u32) -> Span,
    pub assign: unsafe extern "C" fn(*mut c_void, u32, Span),
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32)>,
}
impl Services {
    fn call(&self, op: u32, arg: usize) -> usize {
        unsafe { (self.call)(self.ctx, op, arg) }
    }
    #[inline(never)]
    fn log(&self, event: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event) }
        }
    }
    unsafe fn value(&self, field: u32) -> Span {
        (self.value)(self.ctx, field)
    }
    unsafe fn assign(&self, field: u32, value: Span) {
        (self.assign)(self.ctx, field, value)
    }
}
/// # Safety
/// Services and borrowed values remain live for the call. Callbacks do not retain spans.
pub unsafe fn container(s: &Services) -> bool {
    if s.call(XP_BOOK_DOCUMENT_SIZE, 0) == 0 {
        s.log(XP_BOOK_DOCUMENT_LOG_CONTAINER_SIZE);
        return false;
    }
    if s.call(XP_BOOK_DOCUMENT_SETUP, 0) == 0 {
        return false;
    }
    if s.call(XP_BOOK_DOCUMENT_READ, 512) == 0 {
        s.log(XP_BOOK_DOCUMENT_LOG_CONTAINER_READ);
        return false;
    }
    let value = s.value(XP_BOOK_DOCUMENT_CONTAINER_PATH);
    if value.len == 0 {
        s.log(XP_BOOK_DOCUMENT_LOG_CONTAINER_ROOT);
        return false;
    }
    s.assign(XP_BOOK_DOCUMENT_CONTAINER_PATH, value);
    true
}
/// # Safety
/// Same as container; guide bytes remain valid through GuideRelease.
pub unsafe fn opf(s: &Services, metadata_only: bool) -> bool {
    if s.call(XP_BOOK_DOCUMENT_FIND, 0) == 0 {
        s.log(XP_BOOK_DOCUMENT_LOG_FIND_OPF);
        return false;
    }
    s.call(XP_BOOK_DOCUMENT_BASE, 0);
    s.log(XP_BOOK_DOCUMENT_LOG_PARSING_OPF);
    if s.call(XP_BOOK_DOCUMENT_SIZE, 0) == 0 {
        s.log(XP_BOOK_DOCUMENT_LOG_OPF_SIZE);
        return false;
    }
    if s.call(XP_BOOK_DOCUMENT_SETUP, 0) == 0 {
        s.log(XP_BOOK_DOCUMENT_LOG_OPF_SETUP);
        return false;
    }
    if s.call(XP_BOOK_DOCUMENT_READ, 1024) == 0 {
        s.log(XP_BOOK_DOCUMENT_LOG_OPF_READ);
        return false;
    }
    // Package field numbers are the existing XpPkgField ABI.
    for field in [opf::TITLE, opf::AUTHOR, opf::LANGUAGE] {
        s.assign(field, s.value(field));
    }
    if metadata_only {
        s.log(XP_BOOK_DOCUMENT_LOG_METADATA_PARSED);
        return true;
    }
    s.assign(opf::COVER, s.value(opf::COVER));
    if s.value(opf::COVER).len == 0 && s.value(opf::GUIDE).len != 0 {
        s.log(XP_BOOK_DOCUMENT_LOG_GUIDE_FALLBACK);
        if s.call(XP_BOOK_DOCUMENT_GUIDE_LOAD, 0) != 0 {
            let html = s.value(XP_BOOK_DOCUMENT_AUXILIARY);
            let image = super::residual::guide_image(html.bytes());
            if !image.is_empty() {
                s.assign(XP_BOOK_DOCUMENT_AUXILIARY, Span::new(image));
                s.call(XP_BOOK_DOCUMENT_GUIDE_ASSIGN, 0);
                s.log(XP_BOOK_DOCUMENT_LOG_GUIDE_FOUND);
            }
            s.call(XP_BOOK_DOCUMENT_GUIDE_RELEASE, 0);
        }
    }
    s.assign(opf::TEXT, s.value(opf::TEXT));
    for field in [opf::NCX, opf::NAV] {
        let value = s.value(field);
        if value.len != 0 {
            s.assign(field, value);
        }
    }
    s.call(XP_BOOK_DOCUMENT_CSS, 0);
    s.log(XP_BOOK_DOCUMENT_LOG_OPF_PARSED);
    true
}
/// # Safety
/// Services own the selected NCX or nav pass for this call.
pub unsafe fn toc(s: &Services, nav: bool) -> bool {
    if s.value(XP_BOOK_DOCUMENT_AUXILIARY).len == 0 {
        s.log(if nav {
            XP_BOOK_DOCUMENT_LOG_NO_NAV
        } else {
            XP_BOOK_DOCUMENT_LOG_NO_NCX
        });
        return false;
    }
    s.log(if nav {
        XP_BOOK_DOCUMENT_LOG_PARSING_NAV
    } else {
        XP_BOOK_DOCUMENT_LOG_PARSING_NCX
    });
    if s.call(XP_BOOK_DOCUMENT_SIZE, 0) == 0 {
        s.log(if nav {
            XP_BOOK_DOCUMENT_LOG_NAV_SIZE
        } else {
            XP_BOOK_DOCUMENT_LOG_NCX_SIZE
        });
        return false;
    }
    s.call(XP_BOOK_DOCUMENT_BASE, 0);
    if s.call(XP_BOOK_DOCUMENT_SETUP, 0) == 0 {
        s.log(if nav {
            XP_BOOK_DOCUMENT_LOG_NAV_SETUP
        } else {
            XP_BOOK_DOCUMENT_LOG_NCX_SETUP
        });
        return false;
    }
    if s.call(XP_BOOK_DOCUMENT_READ, 1024) == 0 {
        s.log(if nav {
            XP_BOOK_DOCUMENT_LOG_NAV_READ
        } else {
            XP_BOOK_DOCUMENT_LOG_NCX_READ
        });
        return false;
    }
    s.log(if nav {
        XP_BOOK_DOCUMENT_LOG_NAV_PARSED
    } else {
        XP_BOOK_DOCUMENT_LOG_NCX_PARSED
    });
    true
}
