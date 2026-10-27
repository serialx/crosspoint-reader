//! Synchronous GIF calls. The caller owns all file handles and output targets.
use crate::image::Callbacks;
use core::ffi::c_void;
use xp_core::{
    gif::{self, bmp, Info, Read, Rows},
    image::{self, Config, Pipeline},
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Input {
    context: *mut c_void,
    read: unsafe extern "C" fn(*mut c_void, u32, *mut u8, usize) -> usize,
    poll: unsafe extern "C" fn(*mut c_void),
}
impl Read for Input {
    fn read(&mut self, offset: u32, bytes: &mut [u8]) -> usize {
        unsafe { (self.read)(self.context, offset, bytes.as_mut_ptr(), bytes.len()) }
    }
    fn poll(&mut self) {
        unsafe { (self.poll)(self.context) }
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Output {
    context: *mut c_void,
    write: unsafe extern "C" fn(*mut c_void, *const u8, usize) -> bool,
}
impl bmp::Write for Output {
    fn write(&mut self, bytes: &[u8]) -> bool {
        unsafe { (self.write)(self.context, bytes.as_ptr(), bytes.len()) }
    }
}
struct Frame {
    pipeline: Pipeline,
    sink: Callbacks,
}
impl Rows for Frame {
    fn row(
        &mut self,
        info: Info,
        y: u16,
        pixels: &[u8],
        gray: &[u8],
        transparent: Option<u8>,
    ) -> bool {
        self.pipeline.row(
            image::Row {
                x: info.x as i32,
                y: info.y as i32 + y as i32,
                width: info.frame_width as i32,
                height: 1,
                stride: info.frame_width as i32,
                pixels,
                palette: gray,
                pixel_type: 3,
                depth: 8,
                has_alpha: transparent.is_some(),
                transparent: transparent.unwrap_or(0) as u32,
            },
            &mut self.sink,
        )
    }
}
impl Drop for Frame {
    fn drop(&mut self) {
        self.pipeline.abort(&mut self.sink);
    }
}

/// # Safety
/// Input callbacks are live, non-reentrant, and write at most len bytes. Info is
/// writable and disjoint. All pointers and callbacks borrow only this call.
#[no_mangle]
pub unsafe extern "C" fn xp_gif_inspect(input: *const Input, info: *mut Info) -> bool {
    if input.is_null() || info.is_null() {
        return false;
    }
    let mut input = *input;
    gif::inspect(&mut input, &mut *info)
}

/// # Safety
/// Same input contract as inspect. Config/callback records are readable and
/// disjoint. The image sink lends exclusive targets and preserves them during IO.
#[no_mangle]
pub unsafe extern "C" fn xp_gif_frame(
    input: *const Input,
    config: *const Config,
    sink: *const Callbacks,
    cache: bool,
) -> bool {
    if input.is_null() || config.is_null() || sink.is_null() {
        return false;
    }
    let mut input = *input;
    let mut info = Info::default();
    if !gif::inspect(&mut input, &mut info) || info.interlaced != 0 || info.width > gif::MAX_WIDTH {
        return false;
    }
    let mut config = *config;
    config.kind = 2;
    let Some(pipeline) = Pipeline::new(config, info.width as i32, info.height as i32, false) else {
        return false;
    };
    let mut frame = Frame {
        pipeline,
        sink: *sink,
    };
    if !frame.pipeline.prepare() || (cache && !frame.pipeline.start_cache(&mut frame.sink)) {
        return false;
    }
    if gif::decode(&mut input, &mut frame).is_err() {
        return false;
    }
    let cached = frame.pipeline.finish(&mut frame.sink);
    frame.pipeline.success() && (!cache || cached)
}

/// # Safety
/// Records are readable, callbacks are synchronous and non-reentrant. The output
/// callback consumes bytes before returning; input and output memory are disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_gif_bmp(
    input: *const Input,
    config: *const bmp::Config,
    output: *const Output,
) -> bool {
    if input.is_null() || config.is_null() || output.is_null() {
        return false;
    }
    let mut input = *input;
    let mut info = Info::default();
    if !gif::inspect(&mut input, &mut info) || info.interlaced != 0 {
        return false;
    }
    let Ok(mut bmp) = bmp::Bmp::new(info, *config, *output) else {
        return false;
    };
    gif::decode(&mut input, &mut bmp).is_ok() && bmp.finish()
}
