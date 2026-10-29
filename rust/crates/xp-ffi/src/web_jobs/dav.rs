use core::{ffi::c_void, slice};
#[repr(C)]
pub struct State {
    pub ok: bool,
    pub existed: bool,
}
#[repr(C)]
pub struct Services {
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, u32, usize, usize, *const u8) -> usize,
    response: unsafe extern "C" fn(*mut c_void, u32, *const u8, *const u8),
    content: unsafe extern "C" fn(*mut c_void, u32, *const u8, usize),
    header: unsafe extern "C" fn(*mut c_void, *const u8, *const u8),
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, usize, usize)>,
    path: *mut u8,
    path_len: usize,
    destination: *mut u8,
    destination_len: usize,
    temp: *const u8,
    depth: u32,
    overwrite: bool,
    body: bool,
}
#[repr(u32)]
enum Op {
    Exists,
    Open,
    Close,
    Directory,
    Size,
    Next,
    Name,
    Read,
    Write,
    Available,
    Remove,
    Rmdir,
    Mkdir,
    Rename,
    OpenWrite,
    RemoveBook,
    ClearCache,
    MoveBook,
    Stream,
    Watchdog,
    Yield,
    Length,
    IsOpen,
}
const PLAIN: &[u8] = b"text/plain\0";
const HTML: &[u8] = b"text/html\0";
const XML: &[u8] = b"application/xml; charset=\"utf-8\"\0";
const EMPTY: &[u8] = b"\0";
impl Services {
    unsafe fn op(&self, op: Op, a: usize, b: usize, p: *const u8) -> usize {
        (self.operation)(self.ctx, op as u32, a, b, p)
    }
    unsafe fn file(&self, op: Op, slot: usize) -> usize {
        self.op(op, slot, 0, core::ptr::null())
    }
    unsafe fn path_op(&self, op: Op, p: *const u8) -> usize {
        self.op(op, 0, 0, p)
    }
    unsafe fn send(&self, status: u32, mime: &[u8], body: &[u8]) {
        (self.response)(self.ctx, status, mime.as_ptr(), body.as_ptr());
    }
    unsafe fn status(&self, status: u32) {
        self.send(status, HTML, EMPTY);
    }
    unsafe fn text(&self, status: u32, body: &[u8]) {
        self.send(status, PLAIN, body);
    }
    unsafe fn event(&self, event: u32, a: usize, b: usize) {
        if let Some(f) = self.diagnostic {
            f(self.ctx, event, a, b);
        }
    }
    unsafe fn content(&self, op: u32, data: &[u8]) {
        (self.content)(self.ctx, op, data.as_ptr(), data.len());
    }
    unsafe fn header(&self, key: &[u8], value: &[u8]) {
        (self.header)(self.ctx, key.as_ptr(), value.as_ptr());
    }
    unsafe fn path(&self) -> &[u8] {
        slice::from_raw_parts(self.path, self.path_len)
    }
    unsafe fn dest(&self) -> &[u8] {
        slice::from_raw_parts(self.destination, self.destination_len)
    }
    unsafe fn parent_exists(&self, p: *mut u8, len: usize) -> bool {
        let slash = slice::from_raw_parts(p, len)
            .iter()
            .rposition(|&b| b == b'/')
            .unwrap_or(0);
        if slash == 0 {
            return true;
        }
        *p.add(slash) = 0;
        let exists = self.path_op(Op::Exists, p) != 0;
        *p.add(slash) = b'/';
        exists
    }
}
fn hidden(path: &[u8]) -> bool {
    path.split(|&b| b == b'/')
        .any(|s| s.starts_with(b".") || s == b"System Volume Information" || s == b"XTCache")
}
fn mime(path: &[u8]) -> &'static [u8] {
    let ext = path
        .iter()
        .rposition(|&b| b == b'.')
        .map_or(&[][..], |i| &path[i..]);
    const TYPES: &[(&[u8], &[u8])] = &[
        (b".epub".as_slice(), b"application/epub+zip\0".as_slice()),
        (b".pdf", b"application/pdf\0"),
        (b".txt", PLAIN),
        (b".html", HTML),
        (b".htm", HTML),
        (b".css", b"text/css\0"),
        (b".js", b"application/javascript\0"),
        (b".json", b"application/json\0"),
        (b".xml", b"application/xml\0"),
        (b".jpg", b"image/jpeg\0"),
        (b".jpeg", b"image/jpeg\0"),
        (b".png", b"image/png\0"),
        (b".gif", b"image/gif\0"),
        (b".svg", b"image/svg+xml\0"),
        (b".zip", b"application/zip\0"),
        (b".gz", b"application/gzip\0"),
    ];
    for &(suffix, mime) in TYPES {
        if ext.eq_ignore_ascii_case(suffix) {
            return mime;
        }
    }
    b"application/octet-stream\0"
}
unsafe fn entry(s: &Services, path: &[u8], name: &[u8], dir: bool, size: usize) {
    s.content(0, b"");
    s.content(1, b"<D:response><D:href>");
    encode(s, path);
    if !name.is_empty() {
        if !path.ends_with(b"/") {
            s.content(1, b"/");
        }
        encode(s, name);
    }
    if dir && !(if name.is_empty() { path } else { name }).ends_with(b"/") {
        s.content(1, b"/");
    }
    s.content(1, b"</D:href><D:propstat><D:prop>");
    if dir {
        s.content(1, b"<D:resourcetype><D:collection/></D:resourcetype>");
    } else {
        s.content(1, b"<D:resourcetype/><D:getcontentlength>");
        let mut digits = [0; 20];
        let mut n = size;
        let mut pos = 20;
        loop {
            pos -= 1;
            digits[pos] = b'0' + (n % 10) as u8;
            n /= 10;
            if n == 0 {
                break;
            }
        }
        s.content(1, &digits[pos..]);
        s.content(1, b"</D:getcontentlength><D:getcontenttype>");
        let m = mime(if name.is_empty() { path } else { name });
        s.content(1, &m[..m.len() - 1]);
        s.content(1, b"</D:getcontenttype>");
    }

    s.content(1,b"<D:getlastmodified>Thu, 01 Jan 2024 00:00:00 GMT</D:getlastmodified></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>\n");
    s.content(2, b"");
}
unsafe fn encode(s: &Services, path: &[u8]) {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    for &c in path {
        if matches!(c, b' ' | b'%' | b'#' | b'?' | b'&') || c > 127 {
            s.content(1, &[b'%', HEX[(c >> 4) as usize], HEX[(c & 15) as usize]]);
        } else {
            s.content(1, &[c]);
        }
    }
}
unsafe fn multistart(s: &Services) {
    s.file(Op::Length, usize::MAX);
    s.send(207, XML, EMPTY);
    s.content(
        3,
        b"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:multistatus xmlns:D=\"DAV:\">\n",
    );
}
unsafe fn multiend(s: &Services) {
    s.content(3, b"</D:multistatus>\n");
    s.content(3, b"");
}
unsafe fn propfind(s: &Services) {
    s.event(1, s.depth as usize, 0);
    if s.path_op(Op::Exists, s.path) == 0 && s.path() != b"/" {
        s.text(404, b"Not Found\0");
        return;
    }
    if s.path_op(Op::Open, s.path) == 0 {
        if s.path() == b"/" {
            multistart(s);
            entry(s, b"/", b"", true, 0);
            multiend(s);
        } else {
            s.text(500, b"Failed to open\0");
        }
        return;
    }
    let dir = s.file(Op::Directory, 0) != 0;
    multistart(s);
    entry(
        s,
        s.path(),
        b"",
        dir,
        if dir { 0 } else { s.file(Op::Size, 0) },
    );
    if !dir {
        s.file(Op::Close, 0);
        multiend(s);
        return;
    }
    if s.depth > 0 {
        while s.file(Op::Next, 0) != 0 {
            let mut name = [0; 500];
            s.op(Op::Name, 1, 500, name.as_mut_ptr());
            let len = name.iter().position(|&b| b == 0).unwrap_or(500);
            if !hidden(&name[..len]) {
                let dir = s.file(Op::Directory, 1) != 0;
                entry(
                    s,
                    s.path(),
                    &name[..len],
                    dir,
                    if dir { 0 } else { s.file(Op::Size, 1) },
                );
            }
            s.file(Op::Close, 1);
            s.file(Op::Yield, 0);
            s.file(Op::Watchdog, 0);
        }
    }
    s.file(Op::Close, 0);
    multiend(s);
}
unsafe fn get(s: &Services, head: bool) {
    s.event(if head { 3 } else { 2 }, 0, 0);
    if hidden(s.path()) {
        s.text(403, if head { EMPTY } else { b"Forbidden\0" });
        return;
    }
    if s.path_op(Op::Exists, s.path) == 0 {
        s.text(404, if head { EMPTY } else { b"Not Found\0" });
        return;
    }
    if s.path_op(Op::Open, s.path) == 0 {
        s.text(
            500,
            if head {
                EMPTY
            } else {
                b"Failed to open file\0"
            },
        );
        return;
    }
    if s.file(Op::Directory, 0) != 0 {
        s.file(Op::Close, 0);
        if head {
            s.status(200);
        } else {
            s.text(405, b"Method Not Allowed\0");
        }
        return;
    }
    let size = s.file(Op::Size, 0);
    if !head {
        s.file(Op::Close, 0);
    }
    s.file(Op::Length, size);
    s.send(200, mime(s.path()), EMPTY);
    if head {
        s.file(Op::Close, 0);
    } else if s.path_op(Op::Stream, s.path) == 0 {
        s.event(4, 0, 0);
    }
}
unsafe fn delete(s: &Services) {
    s.event(7, 0, 0);
    if s.path() == b"/" || s.path_len == 0 {
        s.text(403, b"Cannot delete root\0");
        return;
    }
    if hidden(s.path()) {
        s.text(403, b"Forbidden\0");
        return;
    }
    if s.path_op(Op::Exists, s.path) == 0 {
        s.text(404, b"Not Found\0");
        return;
    }
    if s.path_op(Op::Open, s.path) == 0 {
        s.text(500, b"Failed to open\0");
        return;
    }
    if s.file(Op::Directory, 0) != 0 {
        if s.file(Op::Next, 0) != 0 {
            s.file(Op::Close, 1);
            s.file(Op::Close, 0);
            s.text(409, b"Directory not empty\0");
            return;
        }
        s.file(Op::Close, 0);
        if s.path_op(Op::Rmdir, s.path) != 0 {
            s.status(204);
        } else {
            s.text(500, b"Failed to remove directory\0");
        }
    } else {
        s.file(Op::Close, 0);
        if s.path_op(Op::RemoveBook, s.path) != 0 {
            s.status(204);
        } else {
            s.text(500, b"Failed to delete file\0");
        }
    }
}
unsafe fn mkdir(s: &Services) {
    s.event(8, 0, 0);
    if hidden(s.path()) {
        s.text(403, b"Forbidden\0");
        return;
    }
    if s.body {
        s.text(415, b"Unsupported Media Type\0");
        return;
    }
    if s.path_op(Op::Exists, s.path) != 0 {
        s.text(405, b"Already exists\0");
        return;
    }
    if !s.parent_exists(s.path, s.path_len) {
        s.text(409, b"Parent directory does not exist\0");
        return;
    }
    if s.path_op(Op::Mkdir, s.path) != 0 {
        s.status(201);
        s.event(9, 0, 0);
    } else {
        s.text(500, b"Failed to create directory\0");
    }
}
unsafe fn transfer(s: &Services, copy: bool) {
    s.event(if copy { 11 } else { 10 }, usize::from(s.overwrite), 0);
    if !copy && (s.path() == b"/" || s.path_len == 0) {
        s.text(403, b"Cannot move root\0");
        return;
    }
    if hidden(s.path()) || hidden(s.dest()) {
        s.text(403, b"Forbidden\0");
        return;
    }
    if s.destination_len == 0 {
        s.text(400, b"Missing Destination header\0");
        return;
    }
    if s.path() == s.dest() {
        s.status(204);
        return;
    }
    if s.path_op(Op::Exists, s.path) == 0 {
        s.text(404, b"Source not found\0");
        return;
    }
    if copy {
        if s.path_op(Op::Open, s.path) == 0 {
            s.text(500, b"Failed to open source\0");
            return;
        }
        if s.file(Op::Directory, 0) != 0 {
            s.file(Op::Close, 0);
            s.text(403, b"Cannot copy directories\0");
            return;
        }
    }
    if !s.parent_exists(s.destination, s.destination_len) {
        if copy {
            s.file(Op::Close, 0);
        }
        s.text(409, b"Destination parent does not exist\0");
        return;
    }
    let existed = s.path_op(Op::Exists, s.destination) != 0;
    if existed && !s.overwrite {
        if copy {
            s.file(Op::Close, 0);
        }
        s.text(412, b"Destination exists and Overwrite is F\0");
        return;
    }
    if copy {
        if existed {
            s.path_op(Op::Remove, s.destination);
        }
        if s.op(Op::OpenWrite, 1, 0, s.destination) == 0 {
            s.file(Op::Close, 0);
            s.text(500, b"Failed to create destination\0");
            return;
        }
        let ok = copy_file(s);
        s.file(Op::Close, 0);
        s.file(Op::Close, 1);
        if ok {
            s.status(if existed { 204 } else { 201 });
        } else {
            s.path_op(Op::Remove, s.destination);
            s.text(500, b"Copy failed - disk full?\0");
        }
    } else {
        if existed && s.path_op(Op::RemoveBook, s.destination) == 0 {
            s.text(500, b"Failed to remove destination\0");
            return;
        }
        if s.path_op(Op::Open, s.path) == 0 {
            s.text(500, b"Failed to open source\0");
            return;
        }
        s.path_op(Op::ClearCache, s.path);
        s.file(Op::Close, 0);
        if s.file(Op::MoveBook, 0) != 0 {
            s.status(if existed { 204 } else { 201 });
        } else {
            s.text(500, b"Move failed\0");
        }
    }
}
#[inline(never)]
unsafe fn copy_file(s: &Services) -> bool {
    let mut buf = [0; 4096];
    while s.file(Op::Available, 0) != 0 {
        s.file(Op::Watchdog, 0);
        let n = s.op(Op::Read, 0, 4096, buf.as_mut_ptr()) as isize;
        if n <= 0 {
            break;
        }
        if s.op(Op::Write, 1, n as usize, buf.as_ptr()) != n as usize {
            return false;
        }
    }
    true
}
// Methods are stable application IDs, independent of Arduino's enum.
#[no_mangle]
pub unsafe extern "C" fn xp_dav_handle(
    s: *const Services,
    state: *const State,
    method: u32,
) -> bool {
    let s = &*s;
    match method {
        0 => {
            s.header(b"DAV\0", b"1\0");
            s.header(
                b"Allow\0",
                b"OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK\0",
            );
            s.header(b"MS-Author-Via\0", b"DAV\0");
            s.status(200);
            s.event(0, 0, 0);
        }
        1 => propfind(s),
        2 => get(s, false),
        3 => get(s, true),
        4 => {
            s.event(5, 0, 0);
            if hidden(s.path()) {
                s.text(403, b"Forbidden\0");
            } else if !(*state).ok {
                s.path_op(Op::Remove, s.temp);
                s.text(500, b"Write failed - incomplete upload or disk full\0");
            } else {
                s.path_op(Op::ClearCache, s.path);
                s.status(if (*state).existed { 204 } else { 201 });
                s.event(6, 0, 0);
            }
        }
        5 => delete(s),
        6 => mkdir(s),
        7 => transfer(s, false),
        8 => transfer(s, true),
        9 => {
            s.event(12, 0, 0);
            s.header(b"Lock-Token\0", b"<urn:uuid:dummy-lock-token>\0");
            s.send(200, XML, LOCK);
        }
        10 => {
            s.event(13, 0, 0);
            s.status(204);
        }
        _ => return false,
    }
    true
}
const LOCK:&[u8]=b"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:prop xmlns:D=\"DAV:\">\n<D:lockdiscovery><D:activelock>\n<D:locktype><D:write/></D:locktype>\n<D:lockscope><D:exclusive/></D:lockscope>\n<D:depth>infinity</D:depth>\n<D:owner><D:href>crosspoint</D:href></D:owner>\n<D:timeout>Second-3600</D:timeout>\n<D:locktoken><D:href>urn:uuid:dummy-lock-token</D:href></D:locktoken>\n<D:lockroot><D:href>/</D:href></D:lockroot>\n</D:activelock></D:lockdiscovery>\n</D:prop>\n\0";
#[no_mangle]
pub unsafe extern "C" fn xp_dav_raw(
    s: *const Services,
    state: *mut State,
    event: u32,
    bytes: *const u8,
    len: usize,
    total: usize,
) {
    let s = &*s;
    match event {
        0 => {
            if hidden(s.path()) || !s.parent_exists(s.path, s.path_len) {
                (*state).ok = false;
                return;
            }
            s.file(Op::Close, 2);
            (*state).existed = s.path_op(Op::Exists, s.path) != 0;
            if (*state).existed && s.path_op(Op::Open, s.path) != 0 {
                let dir = s.file(Op::Directory, 0) != 0;
                s.file(Op::Close, 0);
                if dir {
                    (*state).ok = false;
                    return;
                }
            }
            s.path_op(Op::Remove, s.temp);
            (*state).ok = s.op(Op::OpenWrite, 2, 0, s.temp) != 0;
            s.event(14, 0, 0);
        }
        1 => {
            if (*state).ok && s.file(Op::IsOpen, 2) != 0 {
                s.file(Op::Watchdog, 0);
                if s.op(Op::Write, 2, len, bytes) != len {
                    (*state).ok = false;
                }
            }
        }
        2 => {
            s.file(Op::Close, 2);
            if (*state).ok {
                if (*state).existed {
                    s.path_op(Op::Remove, s.path);
                }
                if s.path_op(Op::Open, s.temp) != 0 {
                    (*state).ok = s.path_op(Op::Rename, s.path) != 0;
                    s.file(Op::Close, 0);
                } else {
                    (*state).ok = false;
                }
                if !(*state).ok {
                    s.path_op(Op::Remove, s.temp);
                }
            }
            s.event(15, total, usize::from((*state).ok));
        }
        3 => {
            s.file(Op::Close, 2);
            s.path_op(Op::Remove, s.temp);
            (*state).ok = false;
        }
        _ => {}
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_dav_normalize(data: *const u8, len: usize, out: *mut u8) -> usize {
    let input = slice::from_raw_parts(data, len);
    let output = slice::from_raw_parts_mut(out, len + 2);
    let mut n = 0;
    for seg in input.split(|&b| b == b'/') {
        if seg == b".." {
            n = output[..n].iter().rposition(|&b| b == b'/').unwrap_or(0);
        } else if !seg.is_empty() {
            output[n] = b'/';
            n += 1;
            output[n..n + seg.len()].copy_from_slice(seg);
            n += seg.len();
        }
    }
    if n == 0 {
        output[0] = b'/';
        n = 1;
    }
    output[n] = 0;
    n
}
#[no_mangle]
pub unsafe extern "C" fn xp_dav_destination_offset(data: *const u8, len: usize) -> usize {
    let s = slice::from_raw_parts(data, len);
    if let Some(pos) = s.windows(3).position(|w| w == b"://") {
        s[pos + 3..]
            .iter()
            .position(|&b| b == b'/')
            .map_or(len, |i| pos + 3 + i)
    } else {
        0
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_dav_headers(
    depth: *const u8,
    depth_len: usize,
    overwrite: *const u8,
    overwrite_len: usize,
) -> u32 {
    let depth = slice::from_raw_parts(depth, depth_len);
    let overwrite = slice::from_raw_parts(overwrite, overwrite_len);
    u32::from(depth != b"0") | (u32::from(overwrite != b"F" && overwrite != b"f") << 1)
}
