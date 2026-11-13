//! Borrowed platform services for asset conversion. No callback retains a row or reenters this module.
use core::{ffi::c_void, slice};
use xp_core::assets::{
    self,
    jpeg::{Block as JpegBlock, Config, Converter, Output},
};

type Block = unsafe extern "C" fn(*mut c_void, i32, i32, i32, i32, i32, *const u8) -> bool;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct JpegServices {
    context: *mut c_void,
    write: unsafe extern "C" fn(*mut c_void, *const u8, usize),
    progress: unsafe extern "C" fn(*mut c_void),
    diagnostic: unsafe extern "C" fn(*mut c_void, u32, i32, i32, i32, i32),
    decode: unsafe extern "C" fn(*mut c_void, *mut c_void, Block) -> bool,
}
impl Output for JpegServices {
    fn write(&mut self, bytes: &[u8]) {
        unsafe { (self.write)(self.context, bytes.as_ptr(), bytes.len()) };
    }
    fn progress(&mut self) {
        unsafe { (self.progress)(self.context) };
    }
    fn diagnostic(&mut self, event: u32, a: i32, b: i32, c: i32, d: i32) {
        unsafe { (self.diagnostic)(self.context, event, a, b, c, d) };
    }
}
struct Decode {
    converter: Converter,
    services: JpegServices,
    failed: bool,
}
unsafe extern "C" fn block(
    raw: *mut c_void,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    stride: i32,
    pixels: *const u8,
) -> bool {
    let state = unsafe { &mut *raw.cast::<Decode>() };
    if state.failed {
        return false;
    }
    if pixels.is_null() || h < 0 || stride < 0 {
        state.failed = true;
        return false;
    }
    let Some(length) = (h as usize)
        .checked_mul(stride as usize)
        .filter(|n| *n <= isize::MAX as usize)
    else {
        state.failed = true;
        return false;
    };
    let pixels = unsafe { slice::from_raw_parts(pixels, length) };
    state.failed = !state.converter.block(
        JpegBlock {
            x,
            y,
            width: w,
            height: h,
            stride,
            pixels,
        },
        &mut state.services,
    );
    !state.failed
}
#[no_mangle]
pub unsafe extern "C" fn xp_assets_jpeg_bmp(
    sw: i32,
    sh: i32,
    progressive: bool,
    config: *const Config,
    services: *const JpegServices,
) -> bool {
    if config.is_null() || services.is_null() {
        return false;
    }
    let mut services = unsafe { *services };
    let Some(converter) = Converter::new(sw, sh, progressive, unsafe { *config }, &mut services)
    else {
        return false;
    };
    let mut state = Decode {
        converter,
        services,
        failed: false,
    };
    if !unsafe { (services.decode)(services.context, (&mut state as *mut Decode).cast(), block) }
        || state.failed
    {
        return false;
    }
    state.converter.finish(&mut state.services)
}

#[no_mangle]
pub unsafe extern "C" fn xp_assets_screenshot(
    frame: *const u8,
    len: usize,
    width: u32,
    height: u32,
    context: *mut c_void,
    write: unsafe extern "C" fn(*mut c_void, *const u8, usize) -> bool,
) -> u32 {
    if frame.is_null() || len > isize::MAX as usize {
        return 1;
    }
    assets::screenshot(
        unsafe { slice::from_raw_parts(frame, len) },
        width as usize,
        height as usize,
        &mut |bytes| unsafe { write(context, bytes.as_ptr(), bytes.len()) },
    )
}

#[no_mangle]
pub unsafe extern "C" fn xp_assets_qr_plan(
    text: *const u8,
    len: usize,
    version: *mut u8,
    payload_len: *mut usize,
) -> usize {
    if text.is_null() || version.is_null() || payload_len.is_null() || len > isize::MAX as usize {
        return 0;
    }
    let (length, v, bytes) = assets::qr_plan(unsafe { slice::from_raw_parts(text, len) });
    unsafe {
        *version = v;
        *payload_len = length;
    }
    bytes
}

#[no_mangle]
pub unsafe extern "C" fn xp_assets_qr_bitmap(
    text: *const u8,
    len: usize,
    modules: *mut u8,
    capacity: usize,
    size: *mut u8,
    context: *mut c_void,
    encode: unsafe extern "C" fn(*mut c_void, *const u8, usize, u8, *mut u8) -> bool,
) -> bool {
    if text.is_null() || modules.is_null() || size.is_null() || len > isize::MAX as usize {
        return false;
    }
    unsafe {
        *size = 0;
    }
    let text = unsafe { slice::from_raw_parts(text, len) };
    let (length, version, bytes) = assets::qr_plan(text);
    if capacity < bytes {
        return false;
    }
    // initText used strlen: preserve embedded NUL handling after selecting by string length.
    let length = text[..length]
        .iter()
        .position(|b| *b == 0)
        .unwrap_or(length);
    if !unsafe { encode(context, text.as_ptr(), length, version, modules) } {
        return false;
    }
    unsafe {
        *size = version * 4 + 17;
    }
    true
}

#[no_mangle]
pub unsafe extern "C" fn xp_assets_qr_draw(
    modules: *const u8,
    len: usize,
    size: u8,
    x: i32,
    y: i32,
    width: i32,
    height: i32,
    context: *mut c_void,
    draw: unsafe extern "C" fn(*mut c_void, i32, i32, i32),
) {
    if modules.is_null() || len > isize::MAX as usize {
        return;
    }
    assets::qr_rectangles(
        unsafe { slice::from_raw_parts(modules, len) },
        size as i32,
        [x, y, width, height],
        &mut |x, y, px| unsafe { draw(context, x, y, px) },
    );
}

use core::cell::UnsafeCell;
use xp_core::assets::image::{self, Services as ImageServices};
struct ImageState(UnsafeCell<image::State>);
// SAFETY: reader entry resets before publishing state.ready through the model lock;
// render skips images until ready. Other access uses the serialized render/cache owner.
unsafe impl Sync for ImageState {}
static IMAGE_STATE: ImageState = ImageState(UnsafeCell::new(image::State::new()));

// All native and Rust page entries use this one serialized cache owner.
pub(crate) unsafe fn render_image(
    path: &[u8],
    config: image::Config,
    services: &mut dyn ImageServices,
) -> u32 {
    (&mut *IMAGE_STATE.0.get()).render(path, config, services)
}
#[cfg(feature = "app-test")]
struct ImageObserver(UnsafeCell<Option<unsafe extern "C" fn(bool)>>);
#[cfg(feature = "app-test")]
// SAFETY: tests register and invoke the observer under the serialized image owner.
unsafe impl Sync for ImageObserver {}
#[cfg(feature = "app-test")]
static IMAGE_OBSERVER: ImageObserver = ImageObserver(UnsafeCell::new(None));
#[cfg(feature = "app-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_image_observer(callback: Option<unsafe extern "C" fn(bool)>) {
    *IMAGE_OBSERVER.0.get() = callback;
}

#[no_mangle]
pub unsafe extern "C" fn xp_assets_image_reset(release: bool) {
    let state = unsafe { &mut *IMAGE_STATE.0.get() };
    if release {
        state.release();
    } else {
        state.clear_failures();
    }
    #[cfg(feature = "app-test")]
    if let Some(observe) = unsafe { *IMAGE_OBSERVER.0.get() } {
        unsafe { observe(release) };
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_assets_image_failed(path: *const u8, len: usize) -> bool {
    if path.is_null() || len > isize::MAX as usize {
        return false;
    }
    unsafe { (&*IMAGE_STATE.0.get()).failed(slice::from_raw_parts(path, len)) }
}
#[no_mangle]
pub unsafe extern "C" fn xp_assets_limited_write(
    remaining: *mut usize,
    data: *const u8,
    len: usize,
    context: *mut c_void,
    write: unsafe extern "C" fn(*mut c_void, *const u8, usize) -> usize,
) -> usize {
    if remaining.is_null() || (data.is_null() && len != 0) {
        return 0;
    }
    let budget = unsafe { *remaining };
    if len > budget {
        return 0;
    }
    let written = unsafe { write(context, data, len) };
    unsafe { *remaining = budget.wrapping_sub(written) };
    written
}

#[cfg(feature = "strip-spike")]
static STRIP_SPIKE: core::sync::atomic::AtomicBool = core::sync::atomic::AtomicBool::new(false);
#[cfg(feature = "strip-spike")]
#[no_mangle]
pub extern "C" fn xp_assets_image_strip_spike(enabled: bool) {
    STRIP_SPIKE.store(enabled, core::sync::atomic::Ordering::Relaxed);
}

/// # Safety
/// Under the render lock, lend exclusive storage until the next null setter.
/// Release/reset does not end the loan. Storage must not alias any renderer target.
#[cfg(feature = "reader-strips")]
#[no_mangle]
pub unsafe extern "C" fn xp_assets_image_read_window(data: *mut u8, len: usize) {
    unsafe { (&mut *IMAGE_STATE.0.get()).set_read_window(data, len) };
}
