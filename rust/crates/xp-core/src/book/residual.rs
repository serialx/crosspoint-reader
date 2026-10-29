//! Book facade decisions over borrowed storage and platform operations.
use core::ffi::c_void;

/// # Safety
/// Text is a readable NUL-terminated string, unchanged for this call.
pub unsafe fn should_shape(mut text: *const u8) -> bool {
    let mut rtl = false;
    while *text != 0 {
        rtl |= (0xd6..=0xdb).contains(&*text);
        text = text.add(1);
    }
    rtl
}
pub fn word_count(count: usize) -> bool {
    (2..=128).contains(&count)
}
pub fn word_spans(text: &[u8], offsets: &[u16], output: usize) -> bool {
    output == offsets.len()
        && offsets
            .iter()
            .all(|&o| text.get(o as usize..).is_some_and(|s| s.contains(&0)))
        && word_count(offsets.len())
}

pub fn xtc_progress(loaded: bool, page: u32, pages: u32) -> u8 {
    if !loaded || pages == 0 {
        0
    } else {
        page.wrapping_add(1).wrapping_mul(100).wrapping_div(pages) as u8
    }
}
pub fn basename(path: &[u8]) -> &[u8] {
    &path[path.iter().rposition(|&b| b == b'/').map_or(0, |p| p + 1)..]
}
pub fn xtc_title(path: &[u8]) -> &[u8] {
    let start = path.iter().rposition(|&b| b == b'/').map_or(0, |p| p + 1);
    let end = path
        .iter()
        .rposition(|&b| b == b'.')
        .filter(|&p| p > start)
        .unwrap_or(path.len());
    &path[start..end]
}
pub fn extension(path: &[u8], suffix: &[u8]) -> bool {
    path.get(path.len().saturating_sub(suffix.len())..)
        .is_some_and(|s| s.eq_ignore_ascii_case(suffix))
}
pub fn txt_kind(path: &[u8]) -> u32 {
    if extension(path, b".bmp") {
        1
    } else if extension(path, b".jpg") || extension(path, b".jpeg") {
        2
    } else if extension(path, b".png") {
        3
    } else {
        0
    }
}
pub fn guide_image(html: &[u8]) -> &[u8] {
    for pattern in [b"xlink:href=\"".as_slice(), b"src=\""] {
        let mut cursor = 0;
        while let Some(pos) = html
            .get(cursor..)
            .and_then(|s| s.windows(pattern.len()).position(|w| w == pattern))
        {
            cursor += pos + pattern.len();
            if let Some(end) = html[cursor..].iter().position(|&b| b == b'"') {
                let reference = &html[cursor..cursor + end];
                if super::css::raster_kind(reference) != 0 {
                    return reference;
                }
            }
        }
    }
    &[]
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Span {
    pub data: *const u8,
    pub len: usize,
}
impl Span {
    pub fn new(data: &[u8]) -> Self {
        Self {
            data: data.as_ptr(),
            len: data.len(),
        }
    }
    /// # Safety
    /// The borrowed span is readable and remains valid for the returned lifetime.
    pub unsafe fn bytes<'a>(self) -> &'a [u8] {
        if self.len == 0 {
            &[]
        } else {
            core::slice::from_raw_parts(self.data, self.len)
        }
    }
}

pub type GetPath = unsafe extern "C" fn(*mut c_void, usize) -> Span;
/// # Safety
/// GetPath returns a span valid until the next callback, and cannot modify base/path.
pub unsafe fn discover_css(
    base: &[u8],
    path: &[u8],
    count: usize,
    ctx: *mut c_void,
    get: GetPath,
) -> bool {
    if !path.starts_with(base) || !extension(path, b".css") {
        return false;
    }
    !(0..count).any(|i| get(ctx, i).bytes() == path)
}

/// # Safety
/// GetPath returns a borrowed path valid until the next call. No pointer is retained.
pub unsafe fn find_spine(
    target: &[u8],
    count: usize,
    filename: bool,
    ctx: *mut c_void,
    get: GetPath,
) -> i32 {
    if target.is_empty() {
        return if filename { -1 } else { -2 };
    }
    let name = basename(target);
    for index in 0..count {
        let path = get(ctx, index);
        let path = path.bytes();
        if path == target || (filename && basename(path) == name) {
            return index as i32;
        }
    }
    -1
}

/// # Safety
/// Control borrows path prefixes only for each call; operation 0 tests existence,
/// operation 1 creates a directory. Failed mkdir results are intentionally ignored.
pub unsafe fn directories(
    path: &[u8],
    ctx: *mut c_void,
    control: unsafe extern "C" fn(*mut c_void, u32, Span) -> bool,
) {
    if control(ctx, 0, Span::new(path)) {
        return;
    }
    for (i, &byte) in path.iter().enumerate().skip(1) {
        if byte == b'/' {
            control(ctx, 1, Span::new(&path[..i]));
        }
    }
    control(ctx, 1, Span::new(path));
}
pub fn txt_title(path: &[u8]) -> &[u8] {
    let start = path
        .iter()
        .rposition(|&b| b == b'/' || b == b'\\')
        .map_or(0, |i| i + 1);
    let name = &path[start..];
    let end = name
        .iter()
        .rposition(|&b| b == b'.')
        .filter(|&i| i > 0)
        .unwrap_or(name.len());
    &name[..end]
}
/// # Safety
/// The callback returns a cover span that remains readable through the next call.
pub unsafe fn txt_metadata(
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, u32, Span) -> Span,
) {
    operation(ctx, 0, Span::new(&[]));
    operation(ctx, 1, Span::new(b"en"));
    let cover = operation(ctx, 2, Span::new(&[]));
    if cover.len != 0 {
        operation(ctx, 3, cover);
    }
}

#[repr(C)]
pub struct Lookup {
    pub index: i32,
    pub diagnostic: u32,
}
pub fn lookup(loaded: bool, index: i32, count: i32, toc: bool) -> Lookup {
    if !loaded {
        Lookup {
            index: -1,
            diagnostic: 1,
        }
    } else if index < 0 || index >= count {
        Lookup {
            index: if toc { -1 } else { 0 },
            diagnostic: 2,
        }
    } else {
        Lookup {
            index,
            diagnostic: 0,
        }
    }
}

/// # Safety
/// Callbacks synchronously reclaim platform caches and report memory statistics.
pub unsafe fn reclaim_content(
    before: usize,
    clear: unsafe extern "C" fn(),
    diagnostic: unsafe extern "C" fn(usize),
) {
    if before < 64 * 1024 {
        clear();
        diagnostic(before);
    }
}
pub fn toc_spine(index: i32) -> Lookup {
    Lookup {
        index: if index < 0 { 0 } else { index },
        diagnostic: if index < 0 { 3 } else { 0 },
    }
}
