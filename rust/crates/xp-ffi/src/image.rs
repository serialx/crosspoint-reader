//! Caller-owned image contexts; all borrowed buffers live for one synchronous call.
use core::{ffi::c_void, mem, ptr, slice};
use xp_core::image::{
    self,
    cache::ByteSink,
    frame::{Target, Writer},
    probe::Probe,
    Config, Pipeline, Plan, Sink,
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Callbacks {
    context: *mut c_void,
    target: unsafe extern "C" fn(*mut c_void, *mut Target),
    io: unsafe extern "C" fn(*mut c_void, u32, *const u8, usize) -> bool,
}
impl ByteSink for Callbacks {
    fn open(&mut self) -> bool {
        unsafe { (self.io)(self.context, 0, ptr::null(), 0) }
    }
    fn write(&mut self, bytes: &[u8]) -> bool {
        unsafe { (self.io)(self.context, 1, bytes.as_ptr(), bytes.len()) }
    }
    fn close(&mut self) -> bool {
        unsafe { (self.io)(self.context, 2, ptr::null(), 0) }
    }
    fn abort(&mut self) {
        unsafe {
            (self.io)(self.context, 3, ptr::null(), 0);
        }
    }
}
// SAFETY: xp_image_init requires the C target callback to lend exclusive valid memory.
unsafe impl Sink for Callbacks {
    fn target(&mut self) -> Target {
        let mut target = Target {
            data: ptr::null_mut(),
            len: 0,
            stride: 0,
            origin: 0,
            rows: 0,
            width: 0,
            height: 0,
            mode: 0,
            orientation: 0,
            absolute: 0,
        };
        unsafe {
            (self.target)(self.context, &mut target);
        }
        target
    }
}
struct Context {
    pipeline: Pipeline,
    callbacks: Callbacks,
}
const _: () = assert!(mem::size_of::<Context>() <= 48 * mem::size_of::<usize>());
const _: () = assert!(mem::align_of::<Context>() <= mem::align_of::<usize>());
const _: () = assert!(mem::size_of::<Probe>() <= 8 * mem::size_of::<usize>());

#[repr(C)]
pub struct Row {
    x: i32,
    y: i32,
    width: i32,
    height: i32,
    stride: i32,
    pixels: *const u8,
    len: usize,
    palette: *const u8,
    palette_len: usize,
    transparent: u32,
    pixel_type: u8,
    depth: u8,
    has_alpha: u8,
}
unsafe fn bytes<'a>(data: *const u8, len: usize) -> Option<&'a [u8]> {
    if len > isize::MAX as usize || (len != 0 && data.is_null()) {
        None
    } else if len == 0 {
        Some(&[])
    } else {
        Some(slice::from_raw_parts(data, len))
    }
}
/// # Safety
/// Storage is aligned writable XpImageStorage and uninitialized. Other pointers
/// are valid, disjoint records. Callback context outlives destroy and callbacks
/// do not reenter. The target callback lends valid exclusively writable bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_image_init(
    storage: *mut c_void,
    config: *const Config,
    width: i32,
    height: i32,
    progressive: bool,
    callbacks: *const Callbacks,
    plan: *mut Plan,
) -> bool {
    if storage.is_null() || config.is_null() || callbacks.is_null() || plan.is_null() {
        return false;
    }
    let Some(pipeline) = Pipeline::new(*config, width, height, progressive) else {
        return false;
    };
    *plan = pipeline.plan;
    ptr::write(
        storage.cast::<Context>(),
        Context {
            pipeline,
            callbacks: *callbacks,
        },
    );
    true
}
/// # Safety
/// Storage is a live exclusive initialized context, as for all context calls.
#[no_mangle]
pub unsafe extern "C" fn xp_image_prepare(storage: *mut c_void) -> bool {
    (&mut *storage.cast::<Context>()).pipeline.prepare()
}
/// # Safety
/// Same live exclusive context contract as xp_image_prepare.
#[no_mangle]
pub unsafe extern "C" fn xp_image_cache_begin(storage: *mut c_void) -> bool {
    let c = &mut *storage.cast::<Context>();
    c.pipeline.start_cache(&mut c.callbacks)
}
/// # Safety
/// Row spans are readable and disjoint from context/target for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_image_row(storage: *mut c_void, row: *const Row) -> bool {
    if storage.is_null() || row.is_null() {
        return false;
    }
    let row = &*row;
    let Some(pixels) = bytes(row.pixels, row.len) else {
        return false;
    };
    let Some(palette) = bytes(row.palette, row.palette_len) else {
        return false;
    };
    let c = &mut *storage.cast::<Context>();
    c.pipeline.row(
        image::Row {
            x: row.x,
            y: row.y,
            width: row.width,
            height: row.height,
            stride: row.stride,
            pixels,
            palette,
            pixel_type: row.pixel_type,
            depth: row.depth,
            has_alpha: row.has_alpha != 0,
            transparent: row.transparent,
        },
        &mut c.callbacks,
    )
}
/// # Safety
/// Context is live and exclusive; success points to disjoint writable storage.
#[no_mangle]
pub unsafe extern "C" fn xp_image_finish(storage: *mut c_void, success: *mut bool) -> bool {
    let c = &mut *storage.cast::<Context>();
    let cached = c.pipeline.finish(&mut c.callbacks);
    *success = c.pipeline.success();
    cached
}
/// # Safety
/// Context is live and exclusive. Invalidates it; call exactly once after init.
#[no_mangle]
pub unsafe extern "C" fn xp_image_destroy(storage: *mut c_void) {
    let c = &mut *storage.cast::<Context>();
    c.pipeline.abort(&mut c.callbacks);
    ptr::drop_in_place(c);
}
/// # Safety
/// Target is a valid exclusive borrowed target; row points to len readable bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_image_packed_row(
    target: *const Target,
    x: i32,
    y: i32,
    width: i32,
    row: *const u8,
    len: usize,
) {
    if target.is_null() {
        return;
    }
    let Some(row) = bytes(row, len) else {
        return;
    };
    Writer::new(*target).packed_row(x, y, width, row);
}
/// # Safety
/// Storage is an aligned writable XpImageProbe object. It owns no heap memory.
#[no_mangle]
pub unsafe extern "C" fn xp_image_probe_init(storage: *mut c_void) {
    ptr::write(storage.cast::<Probe>(), Probe::default());
}
/// # Safety
/// Probe is initialized, exclusive; input borrows len readable bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_image_probe_write(
    storage: *mut c_void,
    data: *const u8,
    len: usize,
) -> usize {
    let Some(data) = bytes(data, len) else {
        return 0;
    };
    (&mut *storage.cast::<Probe>()).write(data)
}
/// # Safety
/// Probe is initialized; output dimensions are valid disjoint writable integers.
#[no_mangle]
pub unsafe extern "C" fn xp_image_probe_dimensions(
    storage: *const c_void,
    width: *mut u32,
    height: *mut u32,
) -> bool {
    let Some((w, h)) = (&*storage.cast::<Probe>()).dimensions() else {
        return false;
    };
    *width = w;
    *height = h;
    true
}
