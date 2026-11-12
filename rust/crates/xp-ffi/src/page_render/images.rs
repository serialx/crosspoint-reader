//! Page image/cache policy stays in Rust; native leaves own only file/decoder work.
use super::{direct::Target, *};
use core::{cell::UnsafeCell, marker::PhantomPinned, mem::MaybeUninit, pin::Pin};
use xp_core::{
    assets::image::{Config, Services},
    image::frame::Target as ImageTarget,
};

pub(crate) type Stream =
    unsafe fn(*mut c_void, &[u8], xp_core::book::io::Sink, *mut c_void) -> bool;
struct Extractor(UnsafeCell<(*mut c_void, Option<Stream>)>);
// SAFETY: the reader resource guard serializes binding, painting and teardown.
unsafe impl Sync for Extractor {}
static EXTRACTOR: Extractor = Extractor(UnsafeCell::new((ptr::null_mut(), None)));

#[cfg_attr(feature = "app-test", allow(dead_code))]
pub(crate) unsafe fn set_extractor(owner: *mut c_void, stream: Option<Stream>) {
    *EXTRACTOR.0.get() = (owner, stream);
}

#[repr(C)]
struct Image {
    path: *const u8,
    source: *const u8,
    path_len: usize,
    source_len: usize,
    x: i32,
    y: i32,
    width: i32,
    height: i32,
}
#[repr(C)]
struct FileStorage {
    words: [MaybeUninit<usize>; if size_of::<usize>() == 4 { 3 } else { 8 }],
}
#[repr(C)]
struct File {
    storage: FileStorage,
    pinned: PhantomPinned,
}
impl File {
    fn pointer(self: Pin<&mut Self>) -> *mut FileStorage {
        // Native placement construction and destruction use this pinned address.
        unsafe { ptr::addr_of_mut!(self.get_unchecked_mut().storage) }
    }
}
#[repr(u32)]
enum FileOp {
    Init,
    Drop,
    Open,
    Read,
    Seek,
    Size,
    Close,
    Exists,
    FreeHeap,
    LargestBlock,
    OpenWrite,
    Flush,
    Remove,
}
extern "C" {
    fn xp_native_page_image_file(
        file: *mut FileStorage,
        op: FileOp,
        path: *const u8,
        bytes: *mut u8,
        value: usize,
    ) -> usize;
    fn xp_native_page_image_write(file: *mut c_void, bytes: *const u8, length: usize) -> u8;
    fn xp_native_page_image_decode(
        native: *mut c_void,
        image: *const Image,
        cache: *const u8,
    ) -> u8;
    #[cfg(feature = "app-test")]
    fn xp_native_page_image_extract(image: *const Image, query: u8) -> u8;
    fn xp_native_page_image_log(
        native: *mut c_void,
        image: *const Image,
        cache: *const u8,
        event: u32,
        a: i32,
        b: i32,
    );
}
use core::mem::size_of;
struct Physical<'a> {
    target: Option<&'a Target>,
    image: Image,
    file: Pin<&'a mut File>,
    cache: *const u8,
    initialized: bool,
}
impl Physical<'_> {
    fn scalar(&mut self, op: FileOp, value: usize) -> usize {
        unsafe {
            xp_native_page_image_file(
                self.file.as_mut().pointer(),
                op,
                ptr::null(),
                ptr::null_mut(),
                value,
            )
        }
    }
    fn open(&mut self, path: *const u8) -> bool {
        unsafe {
            xp_native_page_image_file(
                self.file.as_mut().pointer(),
                FileOp::Open,
                path,
                ptr::null_mut(),
                0,
            ) != 0
        }
    }
}
impl Physical<'_> {
    unsafe fn extract_stream(&mut self, owner: *mut c_void, stream: Stream) -> bool {
        let file = self.file.as_mut().pointer();
        if xp_native_page_image_file(file, FileOp::OpenWrite, self.image.path, ptr::null_mut(), 0)
            == 0
        {
            return false;
        }
        let source = slice::from_raw_parts(self.image.source, self.image.source_len);
        let source = &source[..source
            .iter()
            .position(|byte| *byte == 0)
            .unwrap_or(source.len())];
        let ok = stream(owner, source, xp_native_page_image_write, file.cast());
        self.scalar(FileOp::Flush, 0);
        self.scalar(FileOp::Close, 0);
        if !ok {
            xp_native_page_image_file(
                self.file.as_mut().pointer(),
                FileOp::Remove,
                self.image.path,
                ptr::null_mut(),
                0,
            );
        }
        ok
    }
}

#[cfg_attr(feature = "app-test", allow(dead_code))]
pub(crate) unsafe fn needs_decode(path: &[u8], width: i32, height: i32) -> bool {
    if crate::assets::xp_assets_image_failed(path.as_ptr(), path.len()) {
        return false;
    }
    let mut file = core::pin::pin!(File {
        storage: FileStorage {
            words: [MaybeUninit::uninit(); if size_of::<usize>() == 4 { 3 } else { 8 }]
        },
        pinned: PhantomPinned,
    });
    let mut physical = Physical {
        target: None,
        image: Image {
            path: path.as_ptr(),
            source: c"".as_ptr().cast(),
            path_len: path.len(),
            source_len: 0,
            x: 0,
            y: 0,
            width,
            height,
        },
        file: file.as_mut(),
        cache: c"".as_ptr().cast(),
        initialized: false,
    };
    physical.scalar(FileOp::Init, 0);
    physical.initialized = true;
    !xp_core::assets::image::State::valid(path, width, height, &mut physical)
}

impl Drop for Physical<'_> {
    fn drop(&mut self) {
        if self.initialized {
            self.scalar(FileOp::Drop, 0);
        }
    }
}
// SAFETY: the pass lends the target exclusively. File operations do not retain
// its bytes or reenter the assets owner; decoding finishes synchronously.
unsafe impl Services for Physical<'_> {
    fn open_cache(&mut self, path: &[u8]) -> bool {
        self.cache = path.as_ptr();
        self.open(path.as_ptr())
    }
    fn read(&mut self, bytes: &mut [u8]) -> usize {
        unsafe {
            xp_native_page_image_file(
                self.file.as_mut().pointer(),
                FileOp::Read,
                ptr::null(),
                bytes.as_mut_ptr(),
                bytes.len(),
            )
        }
    }
    fn seek(&mut self, offset: usize) {
        self.scalar(FileOp::Seek, offset);
    }
    #[cfg(feature = "reader-strips")]
    fn seek_rows(&mut self, offset: usize) -> bool {
        self.scalar(FileOp::Seek, offset) != 0
    }
    fn size(&mut self) -> usize {
        self.scalar(FileOp::Size, 0)
    }
    fn close(&mut self) {
        self.scalar(FileOp::Close, 0);
    }
    fn heap(&mut self) -> (usize, usize) {
        (
            self.scalar(FileOp::FreeHeap, 0),
            self.scalar(FileOp::LargestBlock, 0),
        )
    }
    fn target(&mut self) -> ImageTarget {
        let t = &self.target.expect("paint target").glyphs;
        ImageTarget {
            data: t.buffer,
            len: t.buffer_len as usize,
            stride: t.stride,
            origin: t.origin_y,
            rows: t.rows,
            width: t.width,
            height: t.height,
            mode: t.plane,
            orientation: t.orientation,
            absolute: t.absolute,
        }
    }
    fn extract(&mut self) {
        let (owner, stream) = unsafe { *EXTRACTOR.0.get() };
        #[cfg(feature = "app-test")]
        let native = unsafe { xp_native_page_image_extract(&self.image, 1) } != 0;
        #[cfg(not(feature = "app-test"))]
        let native = false;
        if self.image.source_len == 0 || (stream.is_none() && !native) {
            return;
        }
        if unsafe {
            xp_native_page_image_file(
                self.file.as_mut().pointer(),
                FileOp::Exists,
                self.image.path,
                ptr::null_mut(),
                0,
            )
        } != 0
        {
            return;
        }
        self.diagnostic(14, 0, 0);
        let ok = if let Some(stream) = stream {
            unsafe { self.extract_stream(owner, stream) }
        } else {
            #[cfg(feature = "app-test")]
            {
                unsafe { xp_native_page_image_extract(&self.image, 0) != 0 }
            }
            #[cfg(not(feature = "app-test"))]
            {
                false
            }
        };
        if !ok {
            self.diagnostic(15, 0, 0);
        }
    }
    fn source_size(&mut self) -> Option<usize> {
        if !self.open(self.image.path) {
            return None;
        }
        let size = self.size();
        self.close();
        Some(size)
    }
    fn decode(&mut self, cache: &[u8]) -> Option<bool> {
        self.cache = cache.as_ptr();
        match unsafe {
            xp_native_page_image_decode(
                self.target.expect("paint target").native,
                &self.image,
                cache.as_ptr(),
            )
        } {
            0 => None,
            1 => Some(false),
            _ => Some(true),
        }
    }
    fn diagnostic(&mut self, event: u32, a: i32, b: i32) {
        unsafe {
            xp_native_page_image_log(
                self.target.expect("paint target").native,
                &self.image,
                self.cache,
                event,
                a,
                b,
            )
        }
    }
    #[cfg(feature = "strip-spike")]
    fn packed_override(
        &mut self,
        target: &ImageTarget,
        x: i32,
        y: i32,
        width: usize,
        row: &[u8],
    ) -> bool {
        if !cfg!(feature = "reader-strips") {
            return false;
        }
        unsafe {
            super::strip_state::xp_strip_packed_row(
                target,
                x,
                y,
                width as i32,
                row.as_ptr(),
                row.len(),
            )
        };
        true
    }
}

fn rotate(t: &xp_core::glyph::GlyphTarget, x: i32, y: i32) -> (i32, i32) {
    match t.orientation {
        0 => (y, t.height - 1 - x),
        1 => (t.width - 1 - x, t.height - 1 - y),
        2 => (t.width - 1 - y, x),
        _ => (x, y),
    }
}
unsafe fn placeholder(target: &Target, x: i32, y: i32, width: i32, height: i32) {
    let t = &target.glyphs;
    let buffer = slice::from_raw_parts_mut(t.buffer, t.buffer_len as usize);
    let mut surface = Surface::from_validated(t, buffer);
    surface.fill(x, y, width, height, true);
    if width > 2 && height > 2 {
        surface.fill(x + 1, y + 1, width - 2, height - 2, false);
    }
}
unsafe fn preserve_polarity(target: &Target, x: i32, y: i32, width: i32, height: i32) {
    let t = &target.glyphs;
    if t.plane != 0
        || target.inverted == 0
        || (t.strip != 0 && !cfg!(feature = "reader-strips"))
        || width <= 0
        || height <= 0
    {
        return;
    }
    let left = x.max(t.clip.left);
    let top = y.max(t.clip.top);
    let right = (x + width).min(t.clip.right);
    let bottom = (y + height).min(t.clip.bottom);
    if left >= right || top >= bottom {
        return;
    }
    let (ax, ay) = rotate(t, left, top);
    let (bx, by) = rotate(t, right - 1, bottom - 1);
    let left = ax.min(bx).max(0);
    let right = ax.max(bx).min(t.width - 1);
    let top = ay.min(by).max(t.origin_y);
    let bottom = ay.max(by).min(t.origin_y + t.rows - 1);
    if left > right || top > bottom {
        return;
    }
    let bytes = slice::from_raw_parts_mut(t.buffer, t.buffer_len as usize);
    for y in top..=bottom {
        let row = (y - t.origin_y) as usize * t.stride as usize;
        let mut x = left;
        while x <= right && x & 7 != 0 {
            bytes[row + (x >> 3) as usize] ^= 0x80 >> (x & 7);
            x += 1;
        }
        while x + 7 <= right {
            bytes[row + (x >> 3) as usize] ^= 255;
            x += 8;
        }
        while x <= right {
            bytes[row + (x >> 3) as usize] ^= 0x80 >> (x & 7);
            x += 1;
        }
    }
}

#[allow(clippy::too_many_arguments)]
pub(super) unsafe fn paint(
    target: &Target,
    path: &[u8],
    source: &[u8],
    x: i32,
    y: i32,
    width: i32,
    height: i32,
    placeholders: bool,
) {
    if placeholders {
        placeholder(target, x, y, width, height);
        return;
    }
    let t = &target.glyphs;
    let mut file = core::pin::pin!(File {
        storage: FileStorage {
            words: [MaybeUninit::uninit(); if size_of::<usize>() == 4 { 3 } else { 8 }],
        },
        pinned: PhantomPinned,
    });
    let mut physical = Physical {
        target: Some(target),
        image: Image {
            path: path.as_ptr(),
            source: source.as_ptr(),
            path_len: path.len(),
            source_len: source.len(),
            x,
            y,
            width,
            height,
        },
        file: file.as_mut(),
        cache: c"".as_ptr().cast(),
        initialized: false,
    };
    physical.scalar(FileOp::Init, 0);
    physical.initialized = true;
    let (screen_width, screen_height) = if t.orientation & 1 == 0 {
        (t.height, t.width)
    } else {
        (t.width, t.height)
    };
    let config = Config {
        x,
        y,
        width,
        height,
        screen_width,
        screen_height,
        scanning: t.scanning != 0,
        intersects: t.intersects_strip(x, y, x + width - 1, y + height - 1),
    };
    match crate::assets::render_image(path, config, &mut physical) {
        1 => preserve_polarity(target, x, y, width, height),
        2 => placeholder(target, x, y, width, height),
        _ => (),
    }
}
