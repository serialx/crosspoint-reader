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
    pub control: Option<unsafe extern "C" fn(*mut c_void, u32, i32, i32) -> bool>,
    pub read: Option<Read>,
    pub write: Option<Write>,
    pub size: Option<unsafe extern "C" fn(*mut c_void) -> usize>,
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, usize)>,
}
impl FileServices {
    unsafe fn diagnose(&self, event: u32, size: usize) {
        if let Some(diagnostic) = self.diagnostic {
            diagnostic(self.context, event, size);
        }
    }
}
/// # Safety
/// Services borrow their context and file buffers only for each call.
/// Control/read are required; size is required when cached is nonzero.
/// Write is unused and a missing diagnostic callback is silent.
pub unsafe fn validate(io: &FileServices, cached: usize, tag: &[u8]) -> bool {
    let (Some(control), Some(read)) = (io.control, io.read) else {
        return false;
    };
    if cached > 0 && io.size.is_none() {
        return false;
    }
    let mut valid = true;
    if control(io.context, 0, 0, 0) {
        if control(io.context, 1, 0, 0) {
            let mut header = [0u8; 80];
            let count = read(io.context, header.as_mut_ptr(), 79);
            if count > 0 && count < 80 {
                let end = header.iter().position(|b| *b == 0).unwrap_or(80);
                if !tag.is_empty() && !header[..end].windows(tag.len()).any(|part| part == tag) {
                    io.diagnose(0, 0);
                    valid = false;
                }
            } else {
                valid = false;
            }
        } else {
            valid = false;
        }
        control(io.context, 2, 0, 0);
    }
    if valid && cached > 0 {
        if control(io.context, 3, 0, 0) {
            if let Some(size) = io.size {
                if size(io.context) != cached {
                    let size = size(io.context);
                    io.diagnose(1, size);
                    valid = false;
                }
            }
        } else {
            valid = false;
        }
        control(io.context, 4, 0, 0);
    }
    if !valid {
        io.diagnose(2, 0);
        control(io.context, 5, 0, 0);
    }
    valid
}
/// # Safety
/// Services obey validate's borrowing contract. Control is required, as are
/// read/write for BMP. Size/diagnostic are unused. Kind is 0 unsupported,
/// 1 BMP, 2 JPEG, or 3 PNG.
pub unsafe fn convert(io: &FileServices, kind: u32, height: i32) -> bool {
    let Some(control) = io.control else {
        return false;
    };
    let bmp_io = match (kind == 1, io.read, io.write) {
        (true, Some(read), Some(write)) => Some((read, write)),
        (true, _, _) => return false,
        _ => None,
    };
    if !control(io.context, 0, 0, 0) || kind == 0 || !control(io.context, 1, 0, 0) {
        return false;
    }
    let success = if let Some((read, write)) = bmp_io {
        let mut buffer = [0u8; 128];
        loop {
            let count = read(io.context, buffer.as_mut_ptr(), buffer.len());
            if count <= 0 {
                break count == 0;
            }
            if count as usize > buffer.len()
                || write(io.context, buffer.as_ptr(), count as usize) != count as usize
            {
                break false;
            }
        }
    } else if height > 0 {
        control(
            io.context,
            kind + 2,
            (f64::from(height) * 0.6) as i32,
            height,
        )
    } else {
        control(io.context, kind, 0, 0)
    };
    control(io.context, 6, 0, 0);
    if !success {
        control(io.context, 7, 0, 0);
    }
    success
}
