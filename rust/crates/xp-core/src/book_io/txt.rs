//! TXT/Markdown cache writer; preserves the original chunk and failure ordering.
use core::ffi::c_void;

pub const BUFFER_SIZE: usize = 8192;
pub type Read = unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32;
pub type Write = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> usize;

pub fn escaped_size(text: &[u8]) -> usize {
    text.iter().fold(0usize, |size, byte| {
        size.wrapping_add(match byte {
            b'&' => 5,
            b'<' | b'>' => 4,
            _ => 1,
        })
    })
}

pub fn escape(byte: u8) -> Option<&'static [u8]> {
    match byte {
        b'&' => Some(b"&amp;"),
        b'<' => Some(b"&lt;"),
        b'>' => Some(b"&gt;"),
        _ => None,
    }
}

struct Writer<'a> {
    buffer: &'a mut [u8; BUFFER_SIZE],
    position: usize,
    ok: bool,
    context: *mut c_void,
    write: Write,
}
impl Writer<'_> {
    fn flush(&mut self) {
        if self.position != 0 {
            let written =
                unsafe { (self.write)(self.context, self.buffer.as_ptr(), self.position) };
            self.ok &= written == self.position;
            self.position = 0;
        }
    }
    fn byte(&mut self, byte: u8) {
        self.buffer[self.position] = byte;
        self.position += 1;
        if self.position == BUFFER_SIZE {
            self.flush();
        }
    }
    fn text(&mut self, text: &[u8]) {
        for &byte in text {
            self.byte(byte);
        }
    }
    fn escaped(&mut self, text: &[u8]) {
        for &byte in text {
            if let Some(entity) = escape(byte) {
                self.text(entity);
            } else {
                self.byte(byte);
            }
        }
    }
}

/// # Safety
/// Callbacks obey their buffer bounds, do not retain pointers or reenter this
/// operation, and context is valid for both callbacks for the entire call.
pub unsafe fn stream(
    title: &[u8],
    tag: &[u8],
    input: &mut [u8; BUFFER_SIZE],
    output: &mut [u8; BUFFER_SIZE],
    context: *mut c_void,
    read: Read,
    write: Write,
) -> i32 {
    let mut out = Writer {
        buffer: output,
        position: 0,
        ok: true,
        context,
        write,
    };
    out.text(b"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    out.text(tag);
    out.byte(b'\n');
    out.text(b"<!DOCTYPE html>\n<html>\n<head><title>");
    out.escaped(title);
    out.text(b"</title></head>\n<body>\n");
    let mut first = true;
    let mut line_start = true;
    let mut spaces = 0usize;
    loop {
        let count = read(context, input.as_mut_ptr(), input.len());
        if count < 0 || count as usize > input.len() {
            return -1;
        }
        if count == 0 {
            break;
        }
        let bytes = &input[..count as usize];
        let skip = if first && bytes.starts_with(&[0xef, 0xbb, 0xbf]) {
            3
        } else {
            0
        };
        first = false;
        for &byte in &bytes[skip..] {
            match byte {
                b'\r' => continue,
                b'\n' => {
                    spaces = 0;
                    out.text(b"<br />");
                    line_start = true;
                    continue;
                }
                b' ' => {
                    if line_start {
                        out.text(b"&#160;");
                    } else {
                        spaces = spaces.wrapping_add(1);
                    }
                    continue;
                }
                _ => {}
            }
            if spaces != 0 {
                for _ in 1..spaces {
                    out.text(b"&#160;");
                }
                out.byte(b' ');
                spaces = 0;
            }
            line_start = false;
            if let Some(entity) = escape(byte) {
                out.text(entity);
            } else if byte < 0x20 && byte != b'\t' {
                out.byte(b' ');
            } else {
                out.byte(byte);
            }
        }
    }
    out.text(b"\n</body>\n</html>\n");
    out.flush();
    if out.ok {
        0
    } else {
        -2
    }
}

#[repr(u32)]
#[derive(Clone, Copy)]
pub enum CacheAction {
    Exists,
    Mkdir,
    Invalidate,
    Begin,
    BeginOpf,
    Spine,
    EndOpf,
    BeginToc,
    Toc,
    EndToc,
    End,
    Book,
    Cleanup,
    Reload,
}
pub type CacheControl = unsafe extern "C" fn(*mut c_void, u32) -> bool;

/// # Safety
/// Control performs the requested existing cache operation and borrows context
/// for the call. It must not reenter this transaction.
pub unsafe fn build_cache(context: *mut c_void, control: CacheControl) -> u32 {
    use CacheAction::*;
    if control(context, Exists as u32) {
        control(context, Invalidate as u32);
    } else {
        control(context, Mkdir as u32);
    }
    for action in [
        Begin, BeginOpf, Spine, EndOpf, BeginToc, Toc, EndToc, End, Book, Cleanup, Reload,
    ] {
        let ok = control(context, action as u32);
        if !ok && !matches!(action, Spine | Toc | Cleanup) {
            return action as u32;
        }
    }
    u32::MAX
}

/// # Safety
/// Candidate borrows the static NUL-terminated basename (null means use the book
/// basename) and extension for the call. It returns true for the first existing path.
pub unsafe fn find_cover(
    context: *mut c_void,
    candidate: unsafe extern "C" fn(*mut c_void, *const u8, *const u8) -> bool,
) {
    const EXTENSIONS: [&[u8]; 8] = [
        b".bmp\0", b".jpg\0", b".jpeg\0", b".png\0", b".BMP\0", b".JPG\0", b".JPEG\0", b".PNG\0",
    ];
    for name in [
        None,
        Some(b"cover\0".as_slice()),
        Some(b"Cover\0".as_slice()),
        Some(b"COVER\0".as_slice()),
    ] {
        for extension in EXTENSIONS {
            if candidate(
                context,
                name.map_or(core::ptr::null(), |name| name.as_ptr()),
                extension.as_ptr(),
            ) {
                return;
            }
        }
    }
}

#[repr(C)]
pub struct FileServices {
    pub context: *mut c_void,
    pub control: unsafe extern "C" fn(*mut c_void, u32, i32, i32) -> bool,
    pub read: Read,
    pub write: Write,
    pub size: unsafe extern "C" fn(*mut c_void) -> usize,
    pub diagnostic: unsafe extern "C" fn(*mut c_void, u32, usize),
}
/// # Safety
/// Services borrow their context and file buffers only for each call.
pub unsafe fn validate(io: &FileServices, cached: usize, tag: &[u8]) -> bool {
    let mut valid = true;
    if (io.control)(io.context, 0, 0, 0) {
        if (io.control)(io.context, 1, 0, 0) {
            let mut header = [0u8; 80];
            let count = (io.read)(io.context, header.as_mut_ptr(), 79);
            if count > 0 && count < 80 {
                let end = header.iter().position(|b| *b == 0).unwrap_or(80);
                if !tag.is_empty() && !header[..end].windows(tag.len()).any(|part| part == tag) {
                    (io.diagnostic)(io.context, 0, 0);
                    valid = false;
                }
            } else {
                valid = false;
            }
        } else {
            valid = false;
        }
        (io.control)(io.context, 2, 0, 0);
    }
    if valid && cached > 0 {
        if (io.control)(io.context, 3, 0, 0) {
            if (io.size)(io.context) != cached {
                let size = (io.size)(io.context);
                (io.diagnostic)(io.context, 1, size);
                valid = false;
            }
        } else {
            valid = false;
        }
        (io.control)(io.context, 4, 0, 0);
    }
    if !valid {
        (io.diagnostic)(io.context, 2, 0);
        (io.control)(io.context, 5, 0, 0);
    }
    valid
}
/// # Safety
/// Services obey validate's contract. Kind is 1 BMP, 2 JPEG, 3 PNG, or 0 unsupported.
pub unsafe fn convert(io: &FileServices, kind: u32, height: i32) -> bool {
    if !(io.control)(io.context, 0, 0, 0) || kind == 0 || !(io.control)(io.context, 1, 0, 0) {
        return false;
    }
    let success = if kind == 1 {
        let mut buffer = [0u8; 128];
        loop {
            let count = (io.read)(io.context, buffer.as_mut_ptr(), buffer.len());
            if count <= 0 {
                break count == 0;
            }
            if count as usize > buffer.len()
                || (io.write)(io.context, buffer.as_ptr(), count as usize) != count as usize
            {
                break false;
            }
        }
    } else if height > 0 {
        (io.control)(
            io.context,
            kind + 2,
            (f64::from(height) * 0.6) as i32,
            height,
        )
    } else {
        (io.control)(io.context, kind, 0, 0)
    };
    (io.control)(io.context, 6, 0, 0);
    if !success {
        (io.control)(io.context, 7, 0, 0);
    }
    success
}
