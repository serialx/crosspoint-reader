//! Package pass sequencing; C++ owns parser/file/string adapter lifetimes.
use super::residual::Span;
use core::ffi::c_void;
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub call: unsafe extern "C" fn(*mut c_void, u32, usize) -> usize,
    pub value: unsafe extern "C" fn(*mut c_void, u32) -> Span,
    pub assign: unsafe extern "C" fn(*mut c_void, u32, Span),
    pub diagnostic: unsafe extern "C" fn(*mut c_void, u32),
}
#[repr(u32)]
enum Op {
    Find = 0,
    Base = 1,
    Size = 2,
    Setup = 3,
    Read = 4,
    GuideLoad = 5,
    GuideAssign = 6,
    GuideRelease = 7,
    Css = 8,
}
impl Services {
    fn call(&self, op: Op, arg: usize) -> usize {
        unsafe { (self.call)(self.ctx, op as u32, arg) }
    }
    fn log(&self, event: u32) {
        unsafe { (self.diagnostic)(self.ctx, event) }
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
    if s.call(Op::Size, 0) == 0 {
        s.log(0);
        return false;
    }
    if s.call(Op::Setup, 0) == 0 {
        return false;
    }
    if s.call(Op::Read, 512) == 0 {
        s.log(1);
        return false;
    }
    let value = s.value(0);
    if value.len == 0 {
        s.log(2);
        return false;
    }
    s.assign(0, value);
    true
}
/// # Safety
/// Same as container; guide bytes remain valid through GuideRelease.
pub unsafe fn opf(s: &Services, metadata_only: bool) -> bool {
    if s.call(Op::Find, 0) == 0 {
        s.log(3);
        return false;
    }
    s.call(Op::Base, 0);
    s.log(4);
    if s.call(Op::Size, 0) == 0 {
        s.log(5);
        return false;
    }
    if s.call(Op::Setup, 0) == 0 {
        s.log(6);
        return false;
    }
    if s.call(Op::Read, 1024) == 0 {
        s.log(7);
        return false;
    }
    // Package field numbers are the existing XpPkgField ABI.
    for field in [0, 1, 2] {
        s.assign(field, s.value(field));
    }
    if metadata_only {
        s.log(8);
        return true;
    }
    s.assign(8, s.value(8));
    if s.value(8).len == 0 && s.value(9).len != 0 {
        s.log(9);
        if s.call(Op::GuideLoad, 0) != 0 {
            let html = s.value(100);
            let image = super::residual::guide_image(html.bytes());
            if !image.is_empty() {
                s.assign(100, Span::new(image));
                s.call(Op::GuideAssign, 0);
                s.log(10);
            }
            s.call(Op::GuideRelease, 0);
        }
    }
    s.assign(10, s.value(10));
    for field in [6, 7] {
        let value = s.value(field);
        if value.len != 0 {
            s.assign(field, value);
        }
    }
    s.call(Op::Css, 0);
    s.log(11);
    true
}
/// # Safety
/// Services own the selected NCX or nav pass for this call.
pub unsafe fn toc(s: &Services, nav: bool) -> bool {
    let event = if nav { 17 } else { 12 };
    if s.value(100).len == 0 {
        s.log(event);
        return false;
    }
    s.log(event + 1);
    if s.call(Op::Size, 0) == 0 {
        s.log(event + 2);
        return false;
    }
    s.call(Op::Base, 0);
    if s.call(Op::Setup, 0) == 0 {
        s.log(event + 3);
        return false;
    }
    if s.call(Op::Read, 1024) == 0 {
        s.log(event + 4);
        return false;
    }
    s.log(if nav { 23 } else { 22 });
    true
}
