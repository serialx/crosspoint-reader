//! Host-only bridge: the native fixture supplies the actual physical adapters.
extern crate alloc;
use crate::app::{
    abi,
    epub::render_resources::{self, physical, renderer, PageResources, PrewarmStorage},
};
use alloc::boxed::Box;
use core::{ffi::c_void, pin::Pin};
use xp_ui::reader::render::Log;
use xp_ui::reader::render::{Kind, Query};

#[repr(C)]
pub struct Callbacks {
    context: *mut c_void,
    query: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    operation: unsafe extern "C" fn(*mut c_void, *const physical::Op) -> u32,
    prewarm_begin: unsafe extern "C" fn(*mut c_void, *mut PrewarmStorage) -> u8,
    prewarm_finish: unsafe extern "C" fn(*mut c_void, *mut PrewarmStorage),
    prewarm_drop: unsafe extern "C" fn(*mut c_void, *mut PrewarmStorage),
    plan: unsafe extern "C" fn(*mut c_void, *mut c_void, *mut u8, u16, u8),
    window: unsafe extern "C" fn(*mut c_void, *mut u8, u32),
    source: unsafe extern "C" fn(*mut c_void, *mut c_void) -> u8,
    framebuffer: unsafe extern "C" fn(*mut c_void, *mut usize) -> *mut u8,
    page_query: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    page_command: unsafe extern "C" fn(*mut c_void, u8, u32),
    log: unsafe extern "C" fn(*mut c_void, u8, *const u8, usize),
}
impl render_resources::PhysicalResources for Callbacks {
    unsafe fn prewarm_begin(&self, p: *mut PrewarmStorage) -> bool {
        (self.prewarm_begin)(self.context, p) != 0
    }
    unsafe fn prewarm_finish(&self, p: *mut PrewarmStorage) {
        (self.prewarm_finish)(self.context, p);
    }
    unsafe fn prewarm_drop(&self, p: *mut PrewarmStorage) {
        (self.prewarm_drop)(self.context, p);
    }
    fn detach_source(&self) {
        unsafe {
            (self.source)(self.context, core::ptr::null_mut());
        }
    }
    fn clear_plan(&self) {
        unsafe {
            (self.plan)(
                self.context,
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                0,
                0,
            );
        }
    }
    fn clear_window(&self) {
        unsafe {
            (self.window)(self.context, core::ptr::null_mut(), 0);
        }
    }
    fn release_images(&self) {
        unsafe {
            (self.page_command)(self.context, 255, 0);
        }
    }
}
impl physical::Physical for Callbacks {
    fn query(&self, q: physical::Query) -> u32 {
        unsafe { (self.query)(self.context, q as u8) }
    }
    fn operation(&self, op: &physical::Op) -> u32 {
        unsafe { (self.operation)(self.context, op) }
    }
    #[cfg(feature = "reader-strips")]
    fn plan(&self, p: *mut crate::page_render::Plan, bounds: *mut u8, count: u16, recording: bool) {
        unsafe {
            (self.plan)(self.context, p.cast(), bounds, count, u8::from(recording));
        }
    }
    #[cfg(feature = "reader-strips")]
    fn image_window(&self, bytes: *mut u8, length: u32) {
        unsafe {
            (self.window)(self.context, bytes, length);
        }
    }
    #[cfg(feature = "reader-strips")]
    fn source(&self, source: *mut crate::display_source::DisplaySource) -> bool {
        unsafe { (self.source)(self.context, source.cast()) != 0 }
    }
    #[cfg(feature = "reader-strips")]
    fn framebuffer(&self) -> (*mut u8, usize) {
        let mut length = 0;
        let bytes = unsafe { (self.framebuffer)(self.context, &mut length) };
        (bytes, length)
    }
}
impl Callbacks {
    fn fact(&self, q: Query) -> u32 {
        unsafe { (self.page_query)(self.context, q as u8) }
    }
    fn command(&self, kind: Kind, value: u32) {
        unsafe {
            (self.page_command)(self.context, kind as u8, value);
        }
    }
}
impl renderer::Context for Callbacks {
    fn now(&self) -> u32 {
        self.fact(Query::Now)
    }
    fn has_images(&self) -> bool {
        self.fact(Query::HasImages) != 0
    }
    fn images_need_decode(&self) -> bool {
        self.fact(Query::ImagesNeedDecode) != 0
    }
    fn elements(&self) -> u16 {
        self.fact(Query::Elements) as u16
    }
    fn take_forced(&self) -> bool {
        self.fact(Query::TakeForced) != 0
    }
    fn counter(&self) -> u32 {
        self.fact(Query::Counter)
    }
    fn set_counter(&self, value: u32) {
        self.command(Kind::SetCounter, value);
    }
    fn frequency(&self) -> u32 {
        self.fact(Query::Frequency)
    }
    fn text_aa(&self) -> bool {
        self.fact(Query::TextAa) != 0
    }
    fn is_a4(&self) -> bool {
        self.fact(Query::IsA4) != 0
    }
    fn image_reset(&self) {
        self.command(Kind::ImageReset, 0);
    }
    fn paint(&self, mode: u8) {
        self.command(
            match mode {
                0 => Kind::Page,
                1 => Kind::Images,
                2 => Kind::Placeholders,
                _ => unreachable!(),
            },
            0,
        );
    }
    fn highlights(&self) {
        self.command(Kind::Highlights, 0);
    }
    fn status(&self) {
        self.command(Kind::Status, 0);
    }
    fn log_enabled(&self, _: u8) -> bool {
        true
    }
    fn log(&self, severity: u8, bytes: &[u8]) {
        unsafe {
            (self.log)(self.context, severity, bytes.as_ptr(), bytes.len());
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_render_resources_create() -> *mut c_void {
    let mut storage = Box::<PageResources>::new_uninit();
    PageResources::initialize(storage.as_mut_ptr());
    Box::into_raw(storage.assume_init()).cast()
}

#[cfg(feature = "reader-strips")]
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_strip_replay(
    callbacks: *const Callbacks,
    committed: *mut u16,
    scratch: *mut u8,
    y: u16,
    rows: u16,
    images: u8,
    inverted: u8,
    status_top: i32,
) -> u32 {
    let c = &*callbacks;
    render_resources::replay::paint(
        c,
        render_resources::PageGeometry {
            images: images != 0,
            inverted: inverted != 0,
            status_top,
            ..render_resources::PageGeometry::EMPTY
        },
        &*committed.cast::<core::cell::Cell<u16>>(),
        (scratch, y, rows),
        || renderer::Context::highlights(c),
        || renderer::Context::paint(c, 0),
    )
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_render_resources_log(
    callbacks: *const Callbacks,
    kind: u8,
    times: *const u32,
    images: u8,
    msb: u8,
    stride: u32,
) {
    let kind = match kind {
        0 => Log::StripOom,
        1 => Log::AbsoluteFailed,
        2 => Log::AbsoluteQuality,
        3 => Log::ScratchOom,
        4 => Log::StoreFailed,
        5 => Log::TimingAsync,
        6 => Log::TimingTiled,
        7 => Log::TimingGray,
        8 => Log::TimingBw,
        _ => panic!("invalid test log"),
    };
    render_resources::logging::log(
        &*callbacks,
        kind,
        *(times.cast::<[u32; 10]>()),
        images != 0,
        msb != 0,
        stride,
    );
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_render_resources_destroy(owner: *mut c_void) {
    drop(Box::from_raw(owner.cast::<PageResources>()));
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_render_resources_idle(owner: *mut c_void) -> u8 {
    #[cfg(feature = "reader-strips")]
    {
        u8::from(Pin::new_unchecked(&*owner.cast::<PageResources>()).release_idle_strips())
    }
    #[cfg(not(feature = "reader-strips"))]
    {
        let _ = owner;
        0
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_render_resources_run(
    owner: *mut c_void,
    callbacks: *const Callbacks,
    resources: *const abi::Resources,
    arena: *const u8,
    length: usize,
    strips: u8,
) {
    let callbacks = &*callbacks;
    renderer::run(
        Pin::new_unchecked(&*owner.cast::<PageResources>()),
        core::slice::from_raw_parts(arena, length),
        callbacks,
        callbacks,
        &*resources,
        render_resources::PageGeometry {
            font: 24,
            left: 7,
            top: 11,
            status_top: 792,
            inverted: physical::Physical::query(callbacks, physical::Query::Inverted) != 0,
            ..render_resources::PageGeometry::EMPTY
        },
        strips != 0,
    );
}
