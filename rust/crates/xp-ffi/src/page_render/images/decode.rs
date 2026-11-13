//! Reader decoder bindings; codec bodies are shared with the native consumers.
use super::*;
use xp_core::{gif::Diagnostic as GifEvent, image, png::Diagnostic as PngEvent};

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub(super) enum Format {
    Png,
    Jpeg,
    Gif,
}
pub(super) fn format(path: &[u8]) -> Option<Format> {
    let extension = &path[path.iter().rposition(|b| *b == b'.')?..];
    if extension.eq_ignore_ascii_case(b".png") {
        Some(Format::Png)
    } else if extension.eq_ignore_ascii_case(b".gif") {
        Some(Format::Gif)
    } else if extension.eq_ignore_ascii_case(b".jpg") || extension.eq_ignore_ascii_case(b".jpeg") {
        Some(Format::Jpeg)
    } else {
        None
    }
}
// Values match XpPageImageCodecEvent in xp_page_paint.h.
#[repr(u32)]
enum Event {
    Begin = 50,
    Open = 51,
    Seek = 52,
    Read = 53,
}
#[repr(C)]
struct Log {
    image: *const Image,
    cache: *const u8,
    input: *mut FileStorage,
    free_heap: u32,
    elapsed: u32,
    color: i32,
    depth: i32,
    format: Format,
    seek_failed: u8,
}
extern "C" {
    fn xp_native_page_image_codec_log(log: *const Log, event: u32, a: u32, b: u32, c: u32, d: u32);
}
struct Decode<'a, 'b> {
    physical: &'a mut Physical<'b>,
    cache: Pin<&'a mut File>,
    log: Log,
    last_yield: u32,
    decode_start: u32,
    decoding: bool,
}
impl Decode<'_, '_> {
    fn emit(&self, event: u32, a: u32, b: u32, c: u32, d: u32) {
        unsafe { xp_native_page_image_codec_log(&self.log, event, a, b, c, d) }
    }
    fn cache_op(&mut self, op: FileOp) -> usize {
        unsafe {
            xp_native_page_image_file(
                self.cache.as_mut().pointer(),
                op,
                self.log.cache,
                ptr::null_mut(),
                0,
            )
        }
    }
    unsafe extern "C" fn target(raw: *mut c_void, out: *mut ImageTarget) {
        *out = (&mut *raw.cast::<Self>()).physical.target();
    }
    unsafe extern "C" fn progress(raw: *mut c_void) {
        let this = &mut *raw.cast::<Self>();
        let now = this.physical.scalar(FileOp::Now, 0) as u32;
        if now.wrapping_sub(this.last_yield) >= 250 {
            this.last_yield = now;
            this.physical.scalar(FileOp::Yield, 0);
        }
    }
    unsafe extern "C" fn read(
        raw: *mut c_void,
        offset: u32,
        bytes: *mut u8,
        length: usize,
    ) -> usize {
        let this = &mut *raw.cast::<Self>();
        let file = this.physical.file.as_mut().pointer();
        this.log.seek_failed = u8::from(
            this.physical.scalar(FileOp::Position, 0) != offset as usize
                && this.physical.scalar(FileOp::Seek, offset as usize) == 0,
        );
        if this.log.seek_failed != 0 {
            if this.decoding && this.log.format == Format::Gif {
                this.emit(Event::Seek as u32, offset, 0, 0, 0);
            }
            return 0;
        }
        let count =
            xp_native_page_image_file(file, FileOp::ReadRaw, ptr::null(), bytes, length) as isize;
        if this.log.format == Format::Gif {
            if count < 0 && this.decoding {
                this.emit(Event::Read as u32, offset, length as u32, 0, 0);
            }
            Self::progress(raw);
        }
        count.max(0) as usize
    }
    unsafe extern "C" fn write(
        raw: *mut c_void,
        operation: u32,
        bytes: *const u8,
        length: usize,
    ) -> bool {
        let this = &mut *raw.cast::<Self>();
        match operation {
            0 => this.cache_op(FileOp::OpenCacheWrite) != 0,
            1 => {
                xp_native_page_image_write(this.cache.as_mut().pointer().cast(), bytes, length) != 0
            }
            2 => this.cache_op(FileOp::Close) != 0,
            3 => {
                if this.cache_op(FileOp::IsOpen) != 0 {
                    this.cache_op(FileOp::Close);
                }
                this.cache_op(FileOp::Remove);
                true
            }
            _ => false,
        }
    }
    unsafe extern "C" fn diagnostic(raw: *mut c_void, event: u32, a: u32, b: u32, c: u32, d: u32) {
        let this = &mut *raw.cast::<Self>();
        if this.log.format == Format::Png {
            if event == PngEvent::FrameFormat as u32 {
                this.log.color = a as i32;
                this.log.depth = b as i32;
                return;
            }
            if event == PngEvent::DecodeStart as u32 {
                this.decode_start = this.physical.scalar(FileOp::Now, 0) as u32;
                return;
            }
            if event == PngEvent::DecodeComplete as u32 {
                this.log.elapsed =
                    (this.physical.scalar(FileOp::Now, 0) as u32).wrapping_sub(this.decode_start);
            }
        } else if event == GifEvent::DecodeStart as u32 {
            this.decoding = true;
            return;
        }
        this.emit(event, a, b, c, d);
    }
}
impl Drop for Decode<'_, '_> {
    fn drop(&mut self) {
        // The decoder has aborted/finished its pipeline before these handles close.
        self.cache_op(FileOp::Drop);
        self.physical.scalar(FileOp::Close, 0);
    }
}
#[inline(never)]
pub(super) fn render(physical: &mut Physical<'_>, format: Format) -> bool {
    let mut cache = core::pin::pin!(File {
        storage: FileStorage {
            words: [MaybeUninit::uninit(); if size_of::<usize>() == 4 { 3 } else { 8 }]
        },
        pinned: PhantomPinned,
    });
    let input = physical.file.as_mut().pointer();
    let log = Log {
        image: &physical.image,
        cache: physical.cache,
        input,
        free_heap: 0,
        elapsed: 0,
        color: -1,
        depth: 0,
        format,
        seek_failed: 0,
    };
    let mut context = Decode {
        physical,
        cache: cache.as_mut(),
        log,
        last_yield: 0,
        decode_start: 0,
        decoding: false,
    };
    context.cache_op(FileOp::Init);
    context.emit(Event::Begin as u32, 0, 0, 0, 0);
    if format == Format::Png {
        context.last_yield = context.physical.scalar(FileOp::Now, 0) as u32;
    }
    let op = if format == Format::Png {
        FileOp::OpenPng
    } else {
        FileOp::OpenGif
    };
    if unsafe {
        xp_native_page_image_file(input, op, context.physical.image.path, ptr::null_mut(), 0)
    } == 0
    {
        context.emit(Event::Open as u32, 0, 0, 0, 0);
        return false;
    }
    let size = context.physical.scalar(FileOp::Size, 0);
    if format == Format::Png && size > u32::MAX as usize {
        context.emit(Event::Open as u32, 0, 0, 0, 0);
        return false;
    }
    if format == Format::Gif {
        context.log.free_heap = context.physical.scalar(FileOp::FreeInternalHeap, 0) as u32;
    }
    let target = context.physical.target();
    let (screen_width, screen_height) = if target.orientation & 1 == 0 {
        (target.height, target.width)
    } else {
        (target.width, target.height)
    };
    let image = &context.physical.image;
    let config = image::Config {
        x: image.x,
        y: image.y,
        max_width: image.width,
        max_height: image.height,
        screen_width,
        screen_height,
        crop_x: 0.0,
        crop_y: 0.0,
        exact: 1,
        dither: 1,
        alpha: 0,
        kind: format as u8,
    };
    let raw = ptr::from_mut(&mut context).cast();
    let cache = unsafe { *context.log.cache != 0 };
    unsafe {
        if format == Format::Png {
            let services = crate::png::Services {
                context: raw,
                read: Decode::read,
                target: Some(Decode::target),
                write: Decode::write,
                progress: Decode::progress,
                size: size as u32,
                diagnostic: Some(Decode::diagnostic),
            };
            crate::png::xp_png_render(&services, &config, cache)
        } else {
            let input = crate::gif::Input {
                context: raw,
                read: Decode::read,
                poll: Decode::progress,
                diagnostic: Some(Decode::diagnostic),
            };
            let output = crate::image::Callbacks {
                context: raw,
                target: Decode::target,
                io: Decode::write,
                diagnostic: Some(Decode::diagnostic),
            };
            crate::gif::xp_gif_frame(&input, &config, &output, cache)
        }
    }
}
