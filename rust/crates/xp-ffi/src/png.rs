//! PNG byte I/O remains with the caller; decoding and row processing stay in Rust.
use core::{ffi::c_void, ptr, slice};
use xp_core::{
    image::{self, cache::ByteSink, frame::Target, Pipeline, Sink},
    png::{self, bmp, Diagnostic, Mode, Source},
};

type Read = unsafe extern "C" fn(*mut c_void, u32, *mut u8, usize) -> usize;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    context: *mut c_void,
    read: Read,
    target: Option<unsafe extern "C" fn(*mut c_void, *mut Target)>,
    write: unsafe extern "C" fn(*mut c_void, u32, *const u8, usize) -> bool,
    progress: unsafe extern "C" fn(*mut c_void),
    size: u32,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32)>,
}
impl Source for Services {
    fn diagnostic(&mut self, event: Diagnostic, a: u32, b: u32, c: u32, d: u32) {
        if let Some(callback) = self.diagnostic {
            unsafe { callback(self.context, event as u32, a, b, c, d) };
        }
    }
    fn read(&mut self, offset: u32, bytes: &mut [u8]) -> usize {
        unsafe { (self.read)(self.context, offset, bytes.as_mut_ptr(), bytes.len()) }
    }
    fn progress(&mut self) {
        unsafe { (self.progress)(self.context) };
    }
    fn len(&self) -> u32 {
        self.size
    }
}
impl ByteSink for Services {
    fn image_diagnostic(&mut self, event: image::Diagnostic, a: u32, b: u32, c: u32, d: u32) {
        if let Some(callback) = self.diagnostic {
            unsafe { callback(self.context, event as u32, a, b, c, d) };
        }
    }
    fn open(&mut self) -> bool {
        unsafe { (self.write)(self.context, 0, ptr::null(), 0) }
    }
    fn write(&mut self, bytes: &[u8]) -> bool {
        unsafe { (self.write)(self.context, 1, bytes.as_ptr(), bytes.len()) }
    }
    fn close(&mut self) -> bool {
        unsafe { (self.write)(self.context, 2, ptr::null(), 0) }
    }
    fn abort(&mut self) {
        unsafe {
            (self.write)(self.context, 3, ptr::null(), 0);
        }
    }
}
// SAFETY: xp_png_render requires callbacks to lend exclusive target memory.
unsafe impl Sink for Services {
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
        if let Some(callback) = self.target {
            unsafe { callback(self.context, &mut target) };
        }
        target
    }
}
impl bmp::Output for Services {
    fn diagnostic(&mut self, event: Diagnostic, a: u32, b: u32, c: u32, d: u32) {
        Source::diagnostic(self, event, a, b, c, d);
    }
    fn write(&mut self, bytes: &[u8]) -> bool {
        ByteSink::write(self, bytes)
    }
    fn yield_rows(&mut self) {
        unsafe { (self.progress)(self.context) };
    }
}

struct Input {
    context: *mut c_void,
    read: Read,
    size: u32,
}
impl Source for Input {
    fn read(&mut self, offset: u32, bytes: &mut [u8]) -> usize {
        unsafe { (self.read)(self.context, offset, bytes.as_mut_ptr(), bytes.len()) }
    }
    fn len(&self) -> u32 {
        self.size
    }
}

/// # Safety
/// Read/context remain live for this call; read writes at most len bytes and
/// returns their count. Width and height are disjoint writable u32 records.
#[no_mangle]
pub unsafe extern "C" fn xp_png_dimensions(
    context: *mut c_void,
    read: Read,
    size: u32,
    width: *mut u32,
    height: *mut u32,
) -> bool {
    if width.is_null() || height.is_null() {
        return false;
    }
    let mut input = Input {
        context,
        read,
        size,
    };
    let Some(info) = png::info(&mut input, Mode::Frame) else {
        return false;
    };
    *width = info.width;
    *height = info.height;
    true
}

/// # Safety
/// Services/config are valid records. Context and file remain live; callbacks
/// cannot reenter. Target lends exclusive writable storage; IO preserves its loan.
#[no_mangle]
pub unsafe extern "C" fn xp_png_render(
    services: *const Services,
    config: *const image::Config,
    cache: bool,
) -> bool {
    if services.is_null() || config.is_null() {
        return false;
    }
    let mut input = *services;
    let mut sink = *services;
    if sink.target.is_none() {
        return false;
    }
    let Some(info) = png::info(&mut input, Mode::Frame) else {
        input.diagnostic(Diagnostic::Open, 0, 0, 0, 0);
        return false;
    };
    if info.width == 0
        || info.height == 0
        || info.width > 32767
        || info.height > 32767
        || u64::from(info.width) * u64::from(info.height) > 8388608
    {
        input.diagnostic(Diagnostic::FrameDimensions, info.width, info.height, 0, 0);
        return false;
    }
    input.diagnostic(
        Diagnostic::FrameFormat,
        u32::from(info.color),
        u32::from(info.depth),
        0,
        0,
    );
    let Some(mut pipeline) = Pipeline::new(
        *config,
        info.width as i32,
        info.height as i32,
        false,
        &mut sink,
    ) else {
        return false;
    };
    let Some(mut decoder) = png::Decoder::new(info, Mode::Frame, &mut input) else {
        return false;
    };
    if !pipeline.prepare(&mut sink) {
        return false;
    }
    if cache && (*config).alpha == 0 && !pipeline.start_cache(&mut sink) {
        input.diagnostic(Diagnostic::Cache, 0, 0, 0, 0);
    }
    input.diagnostic(Diagnostic::DecodeStart, 0, 0, 0, 0);
    let ok = decoder.decode(&mut input, |row, _| pipeline.row(row, &mut sink));
    input.diagnostic(
        if ok {
            Diagnostic::DecodeComplete
        } else {
            Diagnostic::DecodeFailed
        },
        0,
        0,
        0,
        0,
    );
    if ok {
        pipeline.finish(&mut sink);
    }
    let success = ok && pipeline.success();
    pipeline.abort(&mut sink);
    success
}

/// Bytes needed for an optional exclusive BMP scratch loan.
#[no_mangle]
pub extern "C" fn xp_png_scratch_size() -> usize {
    png::scratch_size()
}

/// # Safety
/// Services/config follow xp_png_render. Scratch is null/zero or exclusively
/// writable, aligned storage of xp_png_scratch_size bytes until this call returns.
#[no_mangle]
pub unsafe extern "C" fn xp_png_bmp(
    services: *const Services,
    config: *const bmp::Config,
    scratch: *mut u8,
    scratch_len: usize,
) -> bool {
    if services.is_null() || config.is_null() {
        return false;
    }
    let mut source = *services;
    let mut output = *services;
    let Some(info) = png::info(&mut source, Mode::Bmp) else {
        return false;
    };
    if scratch_len > isize::MAX as usize || (scratch.is_null() && scratch_len != 0) {
        return false;
    }
    let scratch = if scratch.is_null() {
        None
    } else {
        Some(slice::from_raw_parts_mut(scratch, scratch_len))
    };
    let Some(mut decoder) = png::Decoder::with_scratch(info, Mode::Bmp, scratch, &mut source)
    else {
        return false;
    };
    bmp::convert(&mut decoder, &mut source, *config, &mut output)
}

/// # Safety
/// Header lends len readable bytes for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_png_sleep_header(header: *const u8, len: usize) -> bool {
    if header.is_null() || len < 29 || len > isize::MAX as usize {
        return false;
    }
    let bytes = slice::from_raw_parts(header, 29);
    png::sleep_header(bytes)
}
