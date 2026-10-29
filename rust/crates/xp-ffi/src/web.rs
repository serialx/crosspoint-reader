mod config;
mod fonts;
mod jobs;
mod plugins;
mod rpc;
extern crate alloc;
// Web file-route policy; synchronous transport and HAL leaves are supplied by C++.
use crate::system_items::hidden_name as hidden;
use alloc::vec::Vec;
use core::ffi::{c_char, c_void};

// Fixed loan capacity in xp_web.h, shared by stream reads and upload copies.
const BUFFER_CAPACITY: usize = 4096;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Span {
    data: *const u8,
    len: usize,
}
impl Span {
    fn new(bytes: &[u8]) -> Self {
        Self {
            data: bytes.as_ptr(),
            len: bytes.len(),
        }
    }
    unsafe fn bytes<'a>(self) -> &'a [u8] {
        if self.len == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(self.data, self.len) }
        }
    }
}
#[repr(C)]
pub struct Services {
    argument: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    send: unsafe extern "C" fn(*mut c_void, u16, *const c_char, Span),
    header: unsafe extern "C" fn(*mut c_void, *const c_char, Span),
    chunk: unsafe extern "C" fn(*mut c_void, Span),
    length: unsafe extern "C" fn(*mut c_void, usize),
    io: unsafe extern "C" fn(*mut c_void, u32, u32, *const c_char, *mut u8, usize) -> usize,
    sensitive: unsafe extern "C" fn(*mut c_void, *const c_char) -> bool,
    book: unsafe extern "C" fn(*mut c_void, u32, *const c_char, *const c_char) -> bool,
    buffer: unsafe extern "C" fn(*mut c_void, bool) -> *mut u8,
    watchdog: unsafe extern "C" fn(*mut c_void),
    yield_task: unsafe extern "C" fn(*mut c_void),
    flags: unsafe extern "C" fn(*mut c_void) -> u32,
    paths: unsafe extern "C" fn(*mut c_void, bool) -> isize,
    path: unsafe extern "C" fn(*mut c_void, usize) -> Span,
    diagnostic: unsafe extern "C" fn(*mut c_void, u32, Span, Span),
    memory_error: unsafe extern "C" fn(*mut c_void) -> *const c_char,
}
struct Text(Vec<u8>);
type Result<T> = core::result::Result<T, ()>;
impl Text {
    fn new(s: &[u8]) -> Result<Self> {
        let mut v = Vec::new();
        v.try_reserve_exact(s.len().checked_add(1).ok_or(())?)
            .map_err(|_| ())?;
        v.extend_from_slice(s);
        v.push(0);
        Ok(Self(v))
    }
    fn bytes(&self) -> &[u8] {
        self.0.get(..self.0.len().saturating_sub(1)).unwrap_or(&[])
    }
    fn ptr(&self) -> *const c_char {
        self.0.as_ptr().cast()
    }
    fn append(&mut self, s: &[u8]) -> Result<()> {
        self.0.try_reserve(s.len()).map_err(|_| ())?;
        self.0.pop();
        self.0.extend_from_slice(s);
        self.0.push(0);
        Ok(())
    }
    fn join(&mut self, s: &[u8]) -> Result<()> {
        if !self.bytes().ends_with(b"/") {
            self.append(b"/")?;
        }
        self.append(s)
    }
    fn name(&self) -> &[u8] {
        self.bytes().rsplit(|&b| b == b'/').next().unwrap_or(&[])
    }
    fn normalized(self) -> Result<Self> {
        let input = self.bytes().split(|&b| b == 0).next().unwrap_or(&[]);
        let mut out = Self::new(b"")?;
        for part in input.split(|&b| b == b'/') {
            if part.is_empty() {
                continue;
            }
            if part == b".." {
                let end = out.bytes().iter().rposition(|&b| b == b'/').unwrap_or(0);
                out.0.truncate(end);
                out.0.push(0);
            } else {
                out.join(part)?;
            }
        }
        if out.bytes().is_empty() {
            out.append(b"/")?;
        }
        Ok(out)
    }
    fn trimmed(self) -> Result<Self> {
        let s = self.bytes();
        let first = s
            .iter()
            .position(|b| !b.is_ascii_whitespace())
            .unwrap_or(s.len());
        let last = s
            .iter()
            .rposition(|b| !b.is_ascii_whitespace())
            .map_or(first, |n| n + 1);
        Self::new(s.get(first..last).unwrap_or(&[]))
    }
}
impl Text {
    fn number(&mut self, n: usize) -> Result<()> {
        let mut bytes = [0; 24];
        let mut out = Output::new(&mut bytes);
        out.number(n);
        self.append(out.bytes())
    }
    fn quoted(&mut self, bytes: &[u8]) -> Result<()> {
        self.append(b"\"")?;
        // Reuse the shared escaping rules with a bounded scratch loan.
        for part in bytes.chunks(16) {
            let mut scratch = [0; 98];
            let mut out = Output::new(&mut scratch);
            out.quoted(part);
            let encoded = out.bytes();
            self.append(
                encoded
                    .get(1..encoded.len().saturating_sub(1))
                    .unwrap_or(&[]),
            )?;
        }
        self.append(b"\"")
    }
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Io {
    Exists,
    Open,
    Close,
    IsOpen,
    IsDir,
    Size,
    Next,
    Name,
    Read,
    Write,
    Mkdir,
    Remove,
    Rmdir,
    ClientWrite,
    ClientClear,
    Create,
    Available,
    OpenRead,
    EnsureDir,
    Replace,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Event {
    ListOpen,
    ListNotDir,
    ListScan,
    ListSkip,
    ListDone,
    StreamOom,
    FolderUnsafe,
    FolderProtected,
    FolderCreate,
    FolderDone,
    FolderFailed,
    RenameDone,
    RenameFailed,
    MoveDone,
    MoveFailed,
    Oom,
}
struct Request<'a> {
    ctx: *mut c_void,
    services: &'a Services,
}
impl Request<'_> {
    fn memory_error(&self) -> &[u8] {
        unsafe { core::ffi::CStr::from_ptr((self.services.memory_error)(self.ctx)).to_bytes() }
    }
    fn arg(&self, key: &'static [u8]) -> Result<Option<Text>> {
        let value = unsafe { (self.services.argument)(self.ctx, key.as_ptr().cast()) };
        if value.data.is_null() {
            Ok(None)
        } else {
            let text = Text::new(unsafe { value.bytes() });
            unsafe {
                (self.services.argument)(self.ctx, core::ptr::null());
            }
            text.map(Some)
        }
    }
    fn arg_or(&self, key: &'static [u8], fallback: &[u8]) -> Result<Text> {
        match self.arg(key)? {
            Some(text) => Ok(text),
            None => Text::new(fallback),
        }
    }
    fn io(&self, op: Io, slot: u32, path: *const c_char, data: *mut u8, len: usize) -> usize {
        unsafe { (self.services.io)(self.ctx, op as u32, slot, path, data, len) }
    }
    #[inline(never)]
    fn file(&self, op: Io, slot: u32) -> usize {
        self.io(op, slot, core::ptr::null(), core::ptr::null_mut(), 0)
    }
    #[inline(never)]
    fn fs(&self, op: Io, slot: u32, path: &Text) -> usize {
        self.io(op, slot, path.ptr(), core::ptr::null_mut(), 0)
    }
    #[inline(never)]
    fn json_body(
        &self,
        parse: unsafe extern "C" fn(*mut c_void, Span) -> Span,
        send: unsafe extern "C" fn(*mut c_void, u16, *const c_char),
    ) -> bool {
        let body = unsafe { (self.services.argument)(self.ctx, c"plain".as_ptr()) };
        let error = if body.data.is_null() {
            Some(c"{\"error\":\"missing body\"}")
        } else {
            let error = unsafe { parse(self.ctx, body) };
            unsafe {
                (self.services.argument)(self.ctx, core::ptr::null());
            }
            if error.data.is_null() {
                None
            } else {
                Some(c"{\"error\":\"bad json\"}")
            }
        };
        if let Some(error) = error {
            unsafe {
                send(self.ctx, 400, error.as_ptr());
            }
            false
        } else {
            true
        }
    }
    #[inline(never)]
    fn send(&self, status: u16, body: &[u8]) {
        unsafe { (self.services.send)(self.ctx, status, c"text/plain".as_ptr(), Span::new(body)) }
    }
    #[inline(never)]
    fn header(&self, key: &'static [u8], value: &[u8]) {
        unsafe { (self.services.header)(self.ctx, key.as_ptr().cast(), Span::new(value)) }
    }
    #[inline(never)]
    fn event(&self, event: Event, a: &[u8], b: &[u8]) {
        unsafe { (self.services.diagnostic)(self.ctx, event as u32, Span::new(a), Span::new(b)) }
    }
    fn sensitive(&self, text: &Text) -> bool {
        unsafe { (self.services.sensitive)(self.ctx, text.ptr()) }
    }
    #[inline(never)]
    fn book(&self, operation: u32, a: &Text, b: Option<&Text>) -> bool {
        unsafe {
            (self.services.book)(
                self.ctx,
                operation,
                a.ptr(),
                b.map_or(core::ptr::null(), Text::ptr),
            )
        }
    }
    fn watchdog(&self) {
        unsafe { (self.services.watchdog)(self.ctx) }
    }
    #[inline(never)]
    fn chunk(&self, bytes: &[u8]) {
        unsafe { (self.services.chunk)(self.ctx, Span::new(bytes)) }
    }
    fn flags(&self) -> u32 {
        unsafe { (self.services.flags)(self.ctx) }
    }
    fn stream(&self) {
        let buffer = unsafe { (self.services.buffer)(self.ctx, true) };
        if buffer.is_null() {
            self.event(Event::StreamOom, b"", b"");
            return;
        }
        loop {
            // available() and read() are separate to retain the old I/O sequence.
            if self.file(Io::Available, 0) == 0 {
                break;
            }
            let got = self.io(Io::Read, 0, core::ptr::null(), buffer, BUFFER_CAPACITY) as isize;
            if got <= 0 {
                break;
            }
            let mut sent = 0;
            while sent < got as usize {
                self.watchdog();
                let n = self.io(
                    Io::ClientWrite,
                    0,
                    core::ptr::null(),
                    unsafe { buffer.add(sent) },
                    got as usize - sent,
                );
                if n == 0 {
                    self.file(Io::ClientClear, 0);
                    unsafe {
                        (self.services.buffer)(self.ctx, false);
                    }
                    return;
                }
                sent = sent.saturating_add(n);
            }
        }
        self.file(Io::ClientClear, 0);
        unsafe {
            (self.services.buffer)(self.ctx, false);
        }
    }
    fn download(&self) -> Result<()> {
        let Some(path) = self.arg(b"path\0")? else {
            self.send(400, b"Missing path");
            return Ok(());
        };
        let path = path.normalized()?;
        if path.bytes() == b"/" {
            self.send(400, b"Invalid path");
            return Ok(());
        }
        if path.name().starts_with(b".") || self.sensitive(&path) {
            self.send(403, b"Cannot access system files");
            return Ok(());
        }
        if hidden(path.name()) {
            self.send(403, b"Cannot access protected items");
            return Ok(());
        }
        if self.fs(Io::Exists, 0, &path) == 0 {
            self.send(404, b"Item not found");
            return Ok(());
        }
        if self.fs(Io::Open, 0, &path) == 0 {
            self.send(500, b"Failed to open file");
            return Ok(());
        }
        if self.file(Io::IsDir, 0) != 0 {
            self.file(Io::Close, 0);
            self.send(400, b"Path is a directory");
            return Ok(());
        }
        let mut name = [0u8; 128];
        let n = self.io(
            Io::Name,
            0,
            core::ptr::null(),
            name.as_mut_ptr(),
            name.len(),
        );
        let name = if n == 0 {
            b"download"
        } else {
            name.split(|&b| b == 0).next().unwrap_or(&[])
        };
        let mut disposition = Text::new(b"attachment; filename=\"")?;
        disposition.append(name)?;
        disposition.append(b"\"")?;
        unsafe {
            (self.services.length)(self.ctx, self.file(Io::Size, 0));
        }
        self.header(b"Content-Disposition\0", disposition.bytes());
        let mime = if epub(path.bytes()) {
            c"application/epub+zip"
        } else {
            c"application/octet-stream"
        };
        unsafe {
            (self.services.send)(self.ctx, 200, mime.as_ptr(), Span::new(b""));
        }
        self.stream();
        self.file(Io::Close, 0);
        Ok(())
    }
    fn mkdir(&self) -> Result<()> {
        let Some(name) = self.arg(b"name\0")? else {
            self.send(400, b"Missing folder name");
            return Ok(());
        };
        if name.bytes().is_empty() {
            self.send(400, b"Folder name cannot be empty");
            return Ok(());
        }
        if !safe(name.bytes()) {
            self.event(Event::FolderUnsafe, name.bytes(), b"");
            self.send(400, b"Invalid folder name");
            return Ok(());
        }
        if protected(name.bytes()) {
            self.event(Event::FolderProtected, name.bytes(), b"");
            self.send(403, b"Cannot create protected item");
            return Ok(());
        }
        let mut path = self.arg_or(b"path\0", b"/")?.normalized()?;
        path.join(name.bytes())?;
        self.event(Event::FolderCreate, path.bytes(), b"");
        if self.fs(Io::Exists, 0, &path) != 0 {
            self.send(400, b"Folder already exists");
            return Ok(());
        }
        if self.fs(Io::Mkdir, 0, &path) != 0 {
            self.event(Event::FolderDone, path.bytes(), b"");
            let mut body = Text::new(b"Folder created: ")?;
            body.append(name.bytes())?;
            self.send(200, body.bytes());
        } else {
            self.event(Event::FolderFailed, path.bytes(), b"");
            self.send(500, b"Failed to create folder");
        }
        Ok(())
    }
    fn move_file(&self, rename: bool) -> Result<()> {
        let path = self.arg(b"path\0")?;
        let other = self.arg(if rename { b"name\0" } else { b"dest\0" })?;
        let (Some(path), Some(other)) = (path, other) else {
            self.send(
                400,
                if rename {
                    b"Missing path or new name"
                } else {
                    b"Missing path or destination"
                },
            );
            return Ok(());
        };
        let path = path.normalized()?;
        let other = if rename {
            other.trimmed()?
        } else {
            other.normalized()?
        };
        if path.bytes() == b"/" {
            self.send(400, b"Invalid path");
            return Ok(());
        }
        if rename {
            if other.bytes().is_empty() {
                self.send(400, b"New name cannot be empty");
                return Ok(());
            }
            if other.bytes().iter().any(|&b| b == b'/' || b == b'\\') {
                self.send(400, b"Invalid file name");
                return Ok(());
            }
            if protected(other.bytes()) {
                self.send(403, b"Cannot rename to protected name");
                return Ok(());
            }
        }
        if protected(path.name()) {
            self.send(
                403,
                if rename {
                    b"Cannot rename protected item"
                } else {
                    b"Cannot move protected item"
                },
            );
            return Ok(());
        }
        if rename && other.bytes() == path.name() {
            self.send(200, b"Name unchanged");
            return Ok(());
        }
        if !rename && other.bytes() != b"/" && protected(other.name()) {
            self.send(403, b"Cannot move into protected folder");
            return Ok(());
        }
        if self.fs(Io::Exists, 0, &path) == 0 {
            self.send(404, b"Item not found");
            return Ok(());
        }
        if self.fs(Io::Open, 0, &path) == 0 {
            self.send(500, b"Failed to open file");
            return Ok(());
        }
        if self.file(Io::IsDir, 0) != 0 {
            self.file(Io::Close, 0);
            self.send(
                400,
                if rename {
                    b"Only files can be renamed"
                } else {
                    b"Only files can be moved"
                },
            );
            return Ok(());
        }
        if !rename {
            if self.fs(Io::Exists, 1, &other) == 0 {
                self.file(Io::Close, 0);
                self.send(404, b"Destination not found");
                return Ok(());
            }
            if self.fs(Io::Open, 1, &other) == 0 || self.file(Io::IsDir, 1) == 0 {
                self.file(Io::Close, 1);
                self.file(Io::Close, 0);
                self.send(400, b"Destination is not a folder");
                return Ok(());
            }
            self.file(Io::Close, 1);
        }
        let mut target = if rename {
            let end = path.bytes().iter().rposition(|&b| b == b'/').unwrap_or(0);
            Text::new(path.bytes().get(..end).unwrap_or(&[]))?
        } else {
            Text::new(other.bytes())?
        };
        target.join(if rename { other.bytes() } else { path.name() })?;
        if self.sensitive(&path) || self.sensitive(&target) {
            self.file(Io::Close, 0);
            self.send(403, b"Cannot move protected item");
            return Ok(());
        }
        if !rename && target.bytes() == path.bytes() {
            self.file(Io::Close, 0);
            self.send(200, b"Already in destination");
            return Ok(());
        }
        if self.fs(Io::Exists, 0, &target) != 0 {
            self.file(Io::Close, 0);
            self.send(409, b"Target already exists");
            return Ok(());
        }
        self.book(0, &path, None);
        self.file(Io::Close, 0);
        let success = self.book(2, &path, Some(&target));
        let (event, status, message): (_, _, &[u8]) = match (rename, success) {
            (true, true) => (Event::RenameDone, 200, b"Renamed successfully"),
            (true, false) => (Event::RenameFailed, 500, b"Failed to rename file"),
            (false, true) => (Event::MoveDone, 200, b"Moved successfully"),
            (false, false) => (Event::MoveFailed, 500, b"Failed to move file"),
        };
        self.event(event, path.bytes(), target.bytes());
        self.send(status, message);
        Ok(())
    }
    fn delete(&self) -> Result<()> {
        let single = self.arg(b"path\0")?.is_some();
        let multiple = self.arg(b"paths\0")?.is_some();
        if !single && !multiple {
            self.send(400, b"Missing `path` or `paths` argument");
            return Ok(());
        }
        if single && multiple {
            self.send(400, b"Provide either 'path' or 'paths', not both");
            return Ok(());
        }
        let count = unsafe { (self.services.paths)(self.ctx, multiple) };
        if count < 0 {
            self.send(
                400,
                if count == -1 {
                    b"Invalid paths format"
                } else {
                    b"No paths provided"
                },
            );
            return Ok(());
        }
        let mut failures = Text::new(b"")?;
        for index in 0..count as usize {
            let raw = unsafe { (self.services.path)(self.ctx, index) };
            let path = Text::new(unsafe { raw.bytes() })?.normalized()?;
            let failure: Option<&[u8]> = if path.bytes() == b"/" {
                Some(b" (cannot delete root); ")
            } else if self.sensitive(&path) || hidden(path.name()) {
                Some(b" (protected file); ")
            } else if self.fs(Io::Exists, 0, &path) == 0 {
                Some(b" (not found); ")
            } else {
                let directory = self.fs(Io::Open, 0, &path) != 0 && self.file(Io::IsDir, 0) != 0;
                if directory && self.file(Io::Next, 0) != 0 {
                    self.file(Io::Close, 2);
                    self.file(Io::Close, 0);
                    Some(b" (folder not empty); ")
                } else {
                    self.file(Io::Close, 0);
                    let ok = if directory {
                        self.fs(Io::Rmdir, 0, &path) != 0
                    } else {
                        self.book(1, &path, None)
                    };
                    if ok {
                        None
                    } else {
                        Some(b" (deletion failed); ")
                    }
                }
            };
            if let Some(failure) = failure {
                failures.append(path.bytes())?;
                failures.append(failure)?;
            }
        }
        if failures.bytes().is_empty() {
            self.send(200, b"All items deleted successfully");
        } else {
            let mut body = Text::new(b"Failed to delete some items: ")?;
            body.append(failures.bytes())?;
            self.send(500, body.bytes());
        }
        Ok(())
    }
    // Keep the 1 KB listing buffers off other routes' stack frames.
    #[inline(never)]
    fn list(&self) -> Result<()> {
        let path = self.arg_or(b"path\0", b"/")?.normalized()?;
        unsafe {
            (self.services.length)(self.ctx, usize::MAX);
            (self.services.send)(self.ctx, 200, c"application/json".as_ptr(), Span::new(b""));
        }
        self.chunk(b"[");
        if self.fs(Io::Open, 0, &path) == 0 {
            self.event(Event::ListOpen, path.bytes(), b"");
        } else if self.file(Io::IsDir, 0) == 0 {
            self.event(Event::ListNotDir, path.bytes(), b"");
            self.file(Io::Close, 0);
        } else {
            self.event(Event::ListScan, path.bytes(), b"");
            let mut seen = false;
            while self.file(Io::Next, 0) != 0 {
                let mut name = [0u8; 500];
                self.io(
                    Io::Name,
                    2,
                    core::ptr::null(),
                    name.as_mut_ptr(),
                    name.len(),
                );
                let name = name.split(|&b| b == 0).next().unwrap_or(&[]);
                if !(hidden(name) || self.flags() & 4 == 0 && name.starts_with(b".")) {
                    let dir = self.file(Io::IsDir, 2) != 0;
                    let size = if dir { 0 } else { self.file(Io::Size, 2) };
                    let mut bytes = [0u8; 512];
                    let mut out = Output::new(&mut bytes);
                    out.add(b"{\"name\":");
                    out.quoted(name);
                    out.add(b",\"size\":");
                    out.number(size);
                    out.add(b",\"isDirectory\":");
                    out.add(if dir { b"true" } else { b"false" });
                    out.add(b",\"isEpub\":");
                    out.add(if !dir && epub(name) {
                        b"true"
                    } else {
                        b"false"
                    });
                    out.add(b"}");
                    if out.len >= 512 {
                        self.event(Event::ListSkip, name, b"");
                    } else {
                        if seen {
                            self.chunk(b",");
                        }
                        seen = true;
                        self.chunk(out.bytes());
                    }
                }
                self.file(Io::Close, 2);
                unsafe {
                    (self.services.yield_task)(self.ctx);
                }
                self.watchdog();
            }
            self.file(Io::Close, 0);
        }
        self.chunk(b"]");
        self.chunk(b"");
        self.event(Event::ListDone, path.bytes(), b"");
        Ok(())
    }
    fn not_found(&self) -> Result<()> {
        if self.flags() & 2 != 0 {
            self.send(204, b"");
            return Ok(());
        }
        let uri = self.arg_or(b"\0", b"")?;
        if self.flags() & 1 != 0 && !uri.bytes().starts_with(b"/api/") {
            self.header(b"Location\0", b"/");
            self.send(302, b"");
            return Ok(());
        }
        let mut body = Text::new(b"404 Not Found\n\nURI: ")?;
        body.append(uri.bytes())?;
        body.append(b"\n")?;
        self.send(404, body.bytes());
        Ok(())
    }
}
fn protected(name: &[u8]) -> bool {
    name.starts_with(b".") || hidden(name)
}
fn safe(name: &[u8]) -> bool {
    !name.is_empty()
        && name != b"."
        && name != b".."
        && !name.iter().any(|&b| b == b'/' || b == b'\\')
}
fn epub(name: &[u8]) -> bool {
    name.get(name.len().saturating_sub(5)..)
        .is_some_and(|tail| tail.eq_ignore_ascii_case(b".epub"))
}
struct Output<'a> {
    data: &'a mut [u8],
    len: usize,
}
impl<'a> Output<'a> {
    fn new(data: &'a mut [u8]) -> Self {
        Self { data, len: 0 }
    }
    #[inline(never)]
    fn add(&mut self, bytes: &[u8]) {
        for &b in bytes {
            if let Some(out) = self.data.get_mut(self.len) {
                *out = b;
            }
            self.len = self.len.saturating_add(1);
        }
    }
    fn bytes(&self) -> &[u8] {
        self.data.get(..self.len).unwrap_or(&[])
    }
    fn number(&mut self, mut n: usize) {
        let mut buf = [0u8; 20];
        let mut pos = buf.len();
        loop {
            pos -= 1;
            buf[pos] = b'0' + (n % 10) as u8;
            n /= 10;
            if n == 0 {
                break;
            }
        }
        self.add(buf.get(pos..).unwrap_or(&[]));
    }
    fn quoted(&mut self, bytes: &[u8]) {
        self.add(b"\"");
        for &b in bytes {
            match b {
                b'"' => self.add(b"\\\""),
                b'\\' => self.add(b"\\\\"),
                b'\n' => self.add(b"\\n"),
                b'\r' => self.add(b"\\r"),
                b'\t' => self.add(b"\\t"),
                8 => self.add(b"\\b"),
                12 => self.add(b"\\f"),
                0 => self.add(b"\\u0000"),
                _ => self.add(&[b]),
            }
        }
        self.add(b"\"");
    }
}
/// # Safety
/// The descriptor and context must remain valid and exclusively owned for the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_files(ctx: *mut c_void, services: *const Services, route: u32) {
    let Some(services) = (unsafe { services.as_ref() }) else {
        return;
    };
    let request = Request { ctx, services };
    let result = match route {
        0 => request.download(),
        1 => request.mkdir(),
        2 => request.move_file(true),
        3 => request.move_file(false),
        4 => request.delete(),
        5 => request.list(),
        6 => request.not_found(),
        _ => return,
    };
    if result.is_err() {
        request.event(Event::Oom, b"", b"");
        request.send(503, request.memory_error());
    }
}

#[repr(C)]
pub struct Upload {
    size: usize,
    buffer_pos: usize,
    writes: usize,
    last_logged: usize,
    started_at: u32,
    write_time: u32,
    buffer: *mut u8,
    success: bool,
}
#[repr(C)]
pub struct WsUpload {
    total: usize,
    received: usize,
    last_progress: usize,
    complete_size: usize,
    started_at: u32,
    completed_at: u32,
    client: u8,
    active: bool,
}
#[repr(C)]
pub struct Transfers {
    files: *const Services,
    get: unsafe extern "C" fn(*mut c_void, u32) -> Span,
    set: unsafe extern "C" fn(*mut c_void, u32, Span),
    joined_path: unsafe extern "C" fn(*mut c_void, bool) -> Span,
    time: unsafe extern "C" fn(*mut c_void) -> u32,
    cancel: unsafe extern "C" fn(*mut c_void) -> bool,
    diagnostic: unsafe extern "C" fn(*mut c_void, u32, Span, Span, usize, usize, usize),
    ws_send: unsafe extern "C" fn(*mut c_void, u8, Span) -> bool,
    ws_memory_error: unsafe extern "C" fn(*mut c_void, u8) -> bool,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Field {
    UploadName,
    UploadPath,
    UploadError,
    WsName,
    WsPath,
    WsCompleteName,
    FontFamily,
    FontPath,
    PluginPath,
    PluginTmp,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum TransferEvent {
    UploadStopped,
    UploadUnsafe,
    UploadStart,
    UploadHeap,
    UploadCollision,
    UploadOom,
    UploadCreateFailed,
    UploadCreated,
    UploadFlushFailed,
    UploadProgress,
    UploadComplete,
    UploadDiagnostics,
    UploadAborted,
    WsDisconnect,
    WsConnect,
    WsText,
    WsUnsafe,
    WsBadSize,
    WsCollision,
    WsStart,
    WsEmpty,
    WsComplete,
    WsRemoved,
    WsRemoveFailed,
}
struct Transfer<'a> {
    r: Request<'a>,
    t: &'a Transfers,
}
impl Transfer<'_> {
    #[inline(never)]
    fn get(&self, field: Field) -> &[u8] {
        unsafe { (self.t.get)(self.r.ctx, field as u32).bytes() }
    }
    #[inline(never)]
    fn set(&self, field: Field, value: &[u8]) {
        unsafe {
            (self.t.set)(self.r.ctx, field as u32, Span::new(value));
        }
    }
    fn time(&self) -> u32 {
        unsafe { (self.t.time)(self.r.ctx) }
    }
    #[inline(never)]
    fn diagnostic(&self, event: TransferEvent, x: &[u8], y: &[u8], a: usize, b: usize, c: usize) {
        unsafe {
            (self.t.diagnostic)(
                self.r.ctx,
                event as u32,
                Span::new(x),
                Span::new(y),
                a,
                b,
                c,
            )
        }
    }
    fn path(&self, ws: bool) -> Result<Text> {
        let mut path = Text::new(self.get(if ws { Field::WsPath } else { Field::UploadPath }))?;
        path.join(self.get(if ws { Field::WsName } else { Field::UploadName }))?;
        Ok(path)
    }
    fn free_buffer(&self, state: &mut Upload) {
        unsafe {
            (self.r.services.buffer)(self.r.ctx, false);
        }
        state.buffer = core::ptr::null_mut();
    }
    fn remove_upload(&self, ws: bool) -> (Span, bool) {
        let path = unsafe { (self.t.joined_path)(self.r.ctx, ws) };
        let removed = !path.data.is_null()
            && self
                .r
                .io(Io::Remove, 0, path.data.cast(), core::ptr::null_mut(), 0)
                != 0;
        (path, removed)
    }
    fn flush(&self, state: &mut Upload) -> bool {
        if state.buffer_pos > 0 && self.r.file(Io::IsOpen, 0) != 0 {
            self.r.watchdog();
            let started = self.time();
            let wrote = self.r.io(
                Io::Write,
                0,
                core::ptr::null(),
                state.buffer,
                state.buffer_pos,
            );
            state.write_time = state
                .write_time
                .wrapping_add(self.time().wrapping_sub(started));
            state.writes = state.writes.wrapping_add(1);
            self.r.watchdog();
            let expected = core::mem::take(&mut state.buffer_pos);
            if wrote != expected {
                self.diagnostic(
                    TransferEvent::UploadFlushFailed,
                    b"",
                    b"",
                    expected,
                    wrote,
                    0,
                );
                return false;
            }
        }
        true
    }
    fn upload(&self, state: &mut Upload, status: u32, name: &[u8], data: &[u8]) -> Result<()> {
        if status == 4 {
            if state.success {
                let mut body = Text::new(b"File uploaded successfully: ")?;
                body.append(self.get(Field::UploadName))?;
                self.r.send(200, body.bytes());
            } else {
                let error = self.get(Field::UploadError);
                self.r.send(
                    400,
                    if error.is_empty() {
                        b"Unknown error during upload"
                    } else {
                        error
                    },
                );
            }
            return Ok(());
        }
        self.r.watchdog();
        if self.r.flags() & 8 == 0 {
            self.diagnostic(TransferEvent::UploadStopped, b"", b"", 0, 0, 0);
            return Ok(());
        }
        match status {
            0 => {
                self.r.watchdog();
                self.set(Field::UploadName, name);
                state.size = 0;
                state.success = false;
                self.set(Field::UploadError, b"");
                state.started_at = self.time();
                state.last_logged = 0;
                state.buffer_pos = 0;
                state.write_time = 0;
                state.writes = 0;
                self.free_buffer(state);
                if !safe(name) {
                    self.set(Field::UploadError, b"Invalid file name");
                    self.diagnostic(TransferEvent::UploadUnsafe, name, b"", 0, 0, 0);
                    return Ok(());
                }
                let path = match self.r.arg(b"path\0")? {
                    Some(path) => path.normalized()?,
                    None => Text::new(b"/")?,
                };
                self.set(Field::UploadPath, path.bytes());
                self.diagnostic(TransferEvent::UploadStart, name, path.bytes(), 0, 0, 0);
                self.diagnostic(TransferEvent::UploadHeap, b"", b"", 0, 0, 0);
                let path = self.path(false)?;
                if self.r.sensitive(&path) {
                    self.set(Field::UploadError, b"Cannot write protected items");
                    return Ok(());
                }
                self.r.watchdog();
                if self.r.fs(Io::Exists, 0, &path) != 0 {
                    let mut error = Text::new(b"File already exists: ")?;
                    error.append(name)?;
                    self.set(Field::UploadError, error.bytes());
                    self.diagnostic(TransferEvent::UploadCollision, path.bytes(), b"", 0, 0, 0);
                    return Ok(());
                }
                state.buffer = unsafe { (self.r.services.buffer)(self.r.ctx, true) };
                if state.buffer.is_null() {
                    self.set(Field::UploadError, self.r.memory_error());
                    self.diagnostic(TransferEvent::UploadOom, b"", b"", 0, 0, 0);
                    return Ok(());
                }
                self.r.watchdog();
                if self.r.fs(Io::Create, 0, &path) == 0 {
                    self.set(Field::UploadError, b"Failed to create file on SD card");
                    self.diagnostic(
                        TransferEvent::UploadCreateFailed,
                        path.bytes(),
                        b"",
                        0,
                        0,
                        0,
                    );
                    self.free_buffer(state);
                    return Ok(());
                }
                self.r.watchdog();
                self.diagnostic(TransferEvent::UploadCreated, path.bytes(), b"", 0, 0, 0);
            }
            1 => {
                if unsafe { (self.t.cancel)(self.r.ctx) } {
                    return Ok(());
                }
                if self.r.file(Io::IsOpen, 0) == 0 || !self.get(Field::UploadError).is_empty() {
                    return Ok(());
                }
                let mut remaining = data;
                while !remaining.is_empty() {
                    let space = BUFFER_CAPACITY.checked_sub(state.buffer_pos).ok_or(())?;
                    let count = space.min(remaining.len());
                    if count == 0 || state.buffer.is_null() {
                        return Err(());
                    }
                    unsafe {
                        core::ptr::copy_nonoverlapping(
                            remaining.as_ptr(),
                            state.buffer.add(state.buffer_pos),
                            count,
                        );
                    }
                    state.buffer_pos += count;
                    remaining = remaining.get(count..).ok_or(())?;
                    if state.buffer_pos >= BUFFER_CAPACITY && !self.flush(state) {
                        self.set(
                            Field::UploadError,
                            b"Failed to write to SD card - disk may be full",
                        );
                        self.r.file(Io::Close, 0);
                        self.free_buffer(state);
                        self.remove_upload(false);
                        return Ok(());
                    }
                }
                state.size = state.size.wrapping_add(data.len());
                if state.size.wrapping_sub(state.last_logged) >= 102400 {
                    self.diagnostic(
                        TransferEvent::UploadProgress,
                        b"",
                        b"",
                        state.size,
                        self.time().wrapping_sub(state.started_at) as usize,
                        state.writes,
                    );
                    state.last_logged = state.size;
                }
            }
            2 => {
                if self.r.flags() & 16 == 0 {
                    return Ok(());
                }
                if self.r.file(Io::IsOpen, 0) != 0 {
                    if !self.flush(state) {
                        self.set(Field::UploadError, b"Failed to write final data to SD card");
                    }
                    self.r.file(Io::Close, 0);
                    if !self.get(Field::UploadError).is_empty() {
                        self.remove_upload(false);
                    } else {
                        state.success = true;
                        let elapsed = self.time().wrapping_sub(state.started_at) as usize;
                        self.diagnostic(
                            TransferEvent::UploadComplete,
                            self.get(Field::UploadName),
                            b"",
                            state.size,
                            elapsed,
                            0,
                        );
                        self.diagnostic(
                            TransferEvent::UploadDiagnostics,
                            b"",
                            b"",
                            state.writes,
                            state.write_time as usize,
                            elapsed,
                        );
                        let path = self.path(false)?;
                        self.r.book(0, &path, None);
                        self.r.book(3, &path, None);
                    }
                }
                self.free_buffer(state);
            }
            3 => {
                state.buffer_pos = 0;
                self.free_buffer(state);
                if self.r.file(Io::IsOpen, 0) != 0 {
                    self.r.file(Io::Close, 0);
                    self.remove_upload(false);
                }
                self.set(Field::UploadError, b"Upload aborted");
                self.diagnostic(TransferEvent::UploadAborted, b"", b"", 0, 0, 0);
            }
            _ => {}
        }
        Ok(())
    }
    #[inline(never)]
    fn ws_send(&self, state: &mut WsUpload, client: u8, bytes: &[u8]) {
        if unsafe { (self.t.ws_send)(self.r.ctx, client, Span::new(bytes)) } {
            self.ws_disconnected(state, client);
        }
    }
    fn ws_disconnected(&self, state: &mut WsUpload, client: u8) {
        self.diagnostic(TransferEvent::WsDisconnect, b"", b"", client as usize, 0, 0);
        if client == state.client && state.active && self.r.file(Io::IsOpen, 0) != 0 {
            self.abort_ws(state, false);
        }
    }
    fn abort_ws(&self, state: &mut WsUpload, web: bool) {
        self.r.file(Io::Close, 0);
        let (path, removed) = self.remove_upload(true);
        self.diagnostic(
            if removed {
                TransferEvent::WsRemoved
            } else {
                TransferEvent::WsRemoveFailed
            },
            unsafe { path.bytes() },
            if web { b"WEB" } else { b"WS" },
            0,
            0,
            0,
        );
        state.active = false;
        state.client = 255;
        state.last_progress = 0;
    }
    fn ws(&self, state: &mut WsUpload, client: u8, status: u32, data: &[u8]) -> Result<()> {
        match status {
            0 => {
                self.diagnostic(TransferEvent::WsDisconnect, b"", b"", client as usize, 0, 0);
                if client == state.client && state.active && self.r.file(Io::IsOpen, 0) != 0 {
                    self.abort_ws(state, false);
                }
            }
            1 => self.diagnostic(TransferEvent::WsConnect, b"", b"", client as usize, 0, 0),
            2 => {
                let msg = data.split(|&b| b == 0).next().unwrap_or(&[]);
                self.diagnostic(TransferEvent::WsText, msg, b"", client as usize, 0, 0);
                let Some(rest) = msg.strip_prefix(b"START:") else {
                    return Ok(());
                };
                if state.active {
                    self.ws_send(state, client, b"ERROR:Upload already in progress");
                    return Ok(());
                }
                let Some(first) = rest.iter().position(|&b| b == b':') else {
                    self.ws_send(state, client, b"ERROR:Invalid START format");
                    return Ok(());
                };
                let tail = rest.get(first + 1..).ok_or(())?;
                let Some(second) = tail.iter().position(|&b| b == b':') else {
                    self.ws_send(state, client, b"ERROR:Invalid START format");
                    return Ok(());
                };
                let name = rest.get(..first).ok_or(())?;
                self.set(Field::WsName, name);
                if !safe(name) {
                    self.diagnostic(TransferEvent::WsUnsafe, name, b"", 0, 0, 0);
                    self.ws_send(state, client, b"ERROR:Invalid file name");
                    return Ok(());
                }
                let token = tail.get(..second).ok_or(())?;
                let digits = token.strip_prefix(b"+").unwrap_or(token);
                if digits.is_empty() || !digits.iter().all(u8::is_ascii_digit) {
                    self.diagnostic(TransferEvent::WsBadSize, token, b"", 0, 0, 0);
                    self.ws_send(state, client, b"ERROR:Invalid START format");
                    return Ok(());
                }
                let mut total = 0usize;
                for &d in digits {
                    total = total
                        .saturating_mul(10)
                        .saturating_add((d - b'0') as usize)
                        .min(i32::MAX as usize);
                }
                state.total = total;
                let path = Text::new(tail.get(second + 1..).ok_or(())?)?.normalized()?;
                self.set(Field::WsPath, path.bytes());
                state.received = 0;
                state.last_progress = 0;
                state.started_at = self.time();
                let path = self.path(true)?;
                if self.r.sensitive(&path) {
                    self.ws_send(state, client, b"ERROR:Cannot write protected items");
                    return Ok(());
                }
                self.r.watchdog();
                if self.r.fs(Io::Exists, 0, &path) != 0 {
                    self.diagnostic(TransferEvent::WsCollision, path.bytes(), b"", 0, 0, 0);
                    let mut error = Text::new(b"ERROR:File already exists: ")?;
                    error.append(name)?;
                    self.ws_send(state, client, error.bytes());
                    return Ok(());
                }
                self.diagnostic(
                    TransferEvent::WsStart,
                    name,
                    path.bytes(),
                    state.total,
                    0,
                    0,
                );
                self.r.watchdog();
                if self.r.fs(Io::Create, 0, &path) == 0 {
                    self.ws_send(state, client, b"ERROR:Failed to create file");
                    state.active = false;
                    state.client = 255;
                    return Ok(());
                }
                self.r.watchdog();
                if state.total == 0 {
                    self.r.file(Io::Close, 0);
                    self.set(Field::WsCompleteName, name);
                    state.complete_size = 0;
                    state.completed_at = self.time();
                    self.diagnostic(TransferEvent::WsEmpty, path.bytes(), b"", 0, 0, 0);
                    self.r.book(0, &path, None);
                    self.r.book(3, &path, None);
                    self.ws_send(state, client, b"DONE");
                    state.last_progress = 0;
                } else {
                    state.client = client;
                    state.active = true;
                    self.ws_send(state, client, b"READY");
                }
            }
            3 => {
                if !state.active || self.r.file(Io::IsOpen, 0) == 0 || client != state.client {
                    self.ws_send(state, client, b"ERROR:No upload in progress");
                    return Ok(());
                }
                if data.len() > state.total.wrapping_sub(state.received) {
                    self.abort_ws(state, false);
                    self.ws_send(state, client, b"ERROR:Upload overflow");
                    return Ok(());
                }
                self.r.watchdog();
                let wrote = self.r.io(
                    Io::Write,
                    0,
                    core::ptr::null(),
                    data.as_ptr().cast_mut(),
                    data.len(),
                );
                self.r.watchdog();
                if wrote != data.len() {
                    self.abort_ws(state, false);
                    self.ws_send(state, client, b"ERROR:Write failed - disk full?");
                    return Ok(());
                }
                state.received = state.received.wrapping_add(wrote);
                if state.received.wrapping_sub(state.last_progress) >= 65536
                    || state.received >= state.total
                {
                    let mut bytes = [0u8; 64];
                    let mut out = Output::new(&mut bytes);
                    out.add(b"PROGRESS:");
                    out.number(state.received);
                    out.add(b":");
                    out.number(state.total);
                    self.ws_send(state, client, out.bytes());
                    state.last_progress = state.received;
                }
                if state.received >= state.total {
                    let path = self.path(true)?;
                    self.r.file(Io::Close, 0);
                    state.active = false;
                    state.client = 255;
                    self.set(Field::WsCompleteName, self.get(Field::WsName));
                    state.complete_size = state.total;
                    state.completed_at = self.time();
                    self.diagnostic(
                        TransferEvent::WsComplete,
                        self.get(Field::WsName),
                        b"",
                        state.total,
                        self.time().wrapping_sub(state.started_at) as usize,
                        0,
                    );
                    self.r.book(0, &path, None);
                    self.r.book(3, &path, None);
                    self.ws_send(state, client, b"DONE");
                    state.last_progress = 0;
                }
            }
            4 => self.abort_ws(state, true),
            _ => {}
        }
        Ok(())
    }
}
/// # Safety
/// The state and buffers must be exclusive and callbacks must preserve the descriptor loan.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_upload(
    ctx: *mut c_void,
    transfers: *const Transfers,
    state: *mut Upload,
    status: u32,
    name: Span,
    data: *const u8,
    len: usize,
) {
    let (Some(t), Some(state)) = (unsafe { transfers.as_ref() }, unsafe { state.as_mut() }) else {
        return;
    };
    let Some(services) = (unsafe { t.files.as_ref() }) else {
        return;
    };
    let transfer = Transfer {
        r: Request { ctx, services },
        t,
    };
    if transfer
        .upload(state, status, unsafe { name.bytes() }, unsafe {
            Span { data, len }.bytes()
        })
        .is_err()
    {
        if status == 4 {
            transfer.r.send(503, transfer.r.memory_error());
        } else {
            let owned = transfer.r.file(Io::IsOpen, 0) != 0 || state.success;
            transfer.r.file(Io::Close, 0);
            transfer.free_buffer(state);
            if owned {
                transfer.remove_upload(false);
            }
            state.success = false;
        }
        transfer.set(Field::UploadError, transfer.r.memory_error());
        transfer.r.event(Event::Oom, b"", b"");
    }
}
/// # Safety
/// The state and payload must remain valid and exclusive throughout this synchronous call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_ws(
    ctx: *mut c_void,
    transfers: *const Transfers,
    state: *mut WsUpload,
    client: u8,
    status: u32,
    data: *const u8,
    len: usize,
) {
    let (Some(t), Some(state)) = (unsafe { transfers.as_ref() }, unsafe { state.as_mut() }) else {
        return;
    };
    let Some(services) = (unsafe { t.files.as_ref() }) else {
        return;
    };
    let transfer = Transfer {
        r: Request { ctx, services },
        t,
    };
    let was_active = state.active;
    if transfer
        .ws(state, client, status, unsafe { Span { data, len }.bytes() })
        .is_err()
    {
        if was_active || state.active || transfer.r.file(Io::IsOpen, 0) != 0 {
            transfer.abort_ws(state, status == 4);
        }
        transfer.r.event(Event::Oom, b"", b"");
        if status != 4 && unsafe { (transfer.t.ws_memory_error)(transfer.r.ctx, client) } {
            transfer.ws_disconnected(state, client);
        }
    }
}

/// # Safety
/// Slot zero must lend an open file; callbacks preserve its ownership and the descriptor.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_stream(ctx: *mut c_void, services: *const Services) {
    let Some(services) = (unsafe { services.as_ref() }) else {
        return;
    };
    Request { ctx, services }.stream();
}

fn c_bytes(bytes: &[u8]) -> &[u8] {
    bytes.split(|&b| b == 0).next().unwrap_or(&[])
}
fn component(bytes: &[u8]) -> bool {
    // Arduino String tests emptiness by length, but indexOf uses C-string searches.
    let visible = c_bytes(bytes);
    !bytes.is_empty()
        && !visible.contains(&b'/')
        && !visible.contains(&b'\\')
        && !visible.windows(2).any(|p| p == b"..")
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_component(data: *const u8, len: usize) -> bool {
    if data.is_null() && len != 0 {
        return false;
    }
    component(unsafe { Span { data, len }.bytes() })
}
