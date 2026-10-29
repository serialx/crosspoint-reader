//! Endpoint policy over the SDK resumable transport; no downloader or retained state.
use super::rpc::{Rpc, RpcRequest};
use super::{Io, Request, Services, Span};
use core::ffi::{c_char, c_void, CStr};

#[repr(C)]
pub struct Result {
    status: i32,
    total: usize,
    complete: bool,
}
#[repr(C)]
pub struct Sink {
    write: unsafe extern "C" fn(*mut c_void, Span) -> bool,
    rewind: unsafe extern "C" fn(*mut c_void) -> bool,
    poll: unsafe extern "C" fn(*mut c_void) -> bool,
    configure: unsafe extern "C" fn(*mut c_void, *mut c_void, bool),
}
#[repr(C)]
pub struct Fetch {
    request: *const Rpc,
    integer: unsafe extern "C" fn(*mut c_void, *const c_char) -> i32,
    prepare: unsafe extern "C" fn(*mut c_void, *const c_char),
    path: unsafe extern "C" fn(*mut c_void, u32) -> Span,
    directory: unsafe extern "C" fn(*mut c_void, Span),
    suspend: unsafe extern "C" fn(*mut c_void, bool),
    run: unsafe extern "C" fn(*mut c_void, usize, *mut c_void, *const Sink) -> Result,
    configure: unsafe extern "C" fn(*mut c_void, *mut c_void, *const c_char, u32, bool),
    time: unsafe extern "C" fn(*mut c_void) -> u32,
    response:
        unsafe extern "C" fn(*mut c_void, u32, *const c_char, i32, usize, bool, usize) -> Span,
    send: unsafe extern "C" fn(*mut c_void, u16),
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, usize, u32, *const c_char, bool)>,
}
#[repr(u32)]
enum Response {
    Offset,
    Truncated,
    Result,
}
#[repr(u32)]
enum Diagnostic {
    Progress,
    Failed,
    Done,
}
struct Transfer<'a> {
    ctx: *mut c_void,
    s: &'a Fetch,
    files: &'a Services,
    offset: usize,
    limit: usize,
    written: usize,
    next_heap_log: usize,
    started: u32,
    heartbeat: u32,
    response_started: bool,
    sd_full: bool,
    segment_boundary: bool,
    range_unsupported: bool,
}
impl Transfer<'_> {
    fn path(&self, slot: u32) -> Span {
        unsafe { (self.s.path)(self.ctx, slot) }
    }
    fn io(&self, op: Io, path: u32) -> usize {
        unsafe {
            (self.files.io)(
                self.ctx,
                op as u32,
                0,
                self.path(path).data.cast(),
                core::ptr::null_mut(),
                0,
            )
        }
    }
    fn connected(&self) -> bool {
        unsafe { (self.files.flags)(self.ctx) & 16 != 0 }
    }
    fn keep_alive(&mut self) {
        let now = unsafe { (self.s.time)(self.ctx) };
        if now.wrapping_sub(self.heartbeat) < 5000 {
            return;
        }
        self.heartbeat = now;
        if !self.connected() {
            return;
        }
        unsafe {
            if !self.response_started {
                (self.files.length)(self.ctx, usize::MAX);
                (self.s.send)(self.ctx, 200);
                self.response_started = true;
            }
            (self.files.chunk)(self.ctx, Span::new(b" \n"));
        }
    }
    fn send(&self, status: u16, body: Span) {
        unsafe {
            if !self.response_started {
                (self.s.send)(self.ctx, status);
            } else if self.connected() {
                (self.files.chunk)(self.ctx, body);
                (self.files.chunk)(self.ctx, Span::new(b""));
            }
        }
    }
    fn diagnostic(&self, event: Diagnostic, text: &CStr, connected: bool) {
        if let Some(diagnostic) = self.s.diagnostic {
            unsafe {
                diagnostic(
                    self.ctx,
                    event as u32,
                    self.written,
                    (self.s.time)(self.ctx).wrapping_sub(self.started),
                    text.as_ptr(),
                    connected,
                );
            }
        }
    }
}
unsafe extern "C" fn write(ctx: *mut c_void, bytes: Span) -> bool {
    let t = unsafe { &mut *ctx.cast::<Transfer<'_>>() };
    unsafe { (t.files.watchdog)(t.ctx) };
    let len = if t.limit > 0 {
        bytes
            .len
            .min(t.offset.wrapping_add(t.limit).wrapping_sub(t.written))
    } else {
        bytes.len
    };
    let wrote = unsafe {
        (t.files.io)(
            t.ctx,
            Io::Write as u32,
            0,
            core::ptr::null(),
            bytes.data.cast_mut(),
            len,
        )
    };
    if wrote != len {
        t.sd_full = true;
        return false;
    }
    t.written = t.written.wrapping_add(len);
    if t.written >= t.next_heap_log {
        // Progress is a per-MiB milestone, matching the original logging cadence.
        if let Some(diagnostic) = t.s.diagnostic {
            unsafe {
                diagnostic(
                    t.ctx,
                    Diagnostic::Progress as u32,
                    t.written,
                    0,
                    c"".as_ptr(),
                    false,
                )
            };
        }
        t.next_heap_log = t.written.wrapping_add(1024 * 1024);
    }
    t.keep_alive();
    if t.limit > 0 && t.written.wrapping_sub(t.offset) >= t.limit {
        t.segment_boundary = true;
        return false;
    }
    true
}
unsafe extern "C" fn rewind(ctx: *mut c_void) -> bool {
    let t = unsafe { &mut *ctx.cast::<Transfer<'_>>() };
    if t.offset > 0 {
        t.range_unsupported = true;
        return false;
    }
    t.io(Io::Close, 2);
    t.written = 0;
    t.io(Io::CreatePlugin, 2) != 0
}
unsafe extern "C" fn poll(ctx: *mut c_void) -> bool {
    let t = unsafe { &mut *ctx.cast::<Transfer<'_>>() };
    unsafe { (t.files.watchdog)(t.ctx) };
    t.keep_alive();
    // Browser disconnect and upload cancellation have never cancelled this endpoint.
    false
}
unsafe extern "C" fn configure(ctx: *mut c_void, http: *mut c_void, same_origin: bool) {
    let t = unsafe { &*ctx.cast::<Transfer<'_>>() };
    unsafe { (t.s.configure)(t.ctx, http, c"CrossPoint".as_ptr(), 60000, same_origin) };
}
static SINK: Sink = Sink {
    write,
    rewind,
    poll,
    configure,
};

/// # Safety
/// The descriptor/context live through this synchronous call. Only the supplied
/// sink callbacks may reenter Rust during run; their state is exclusively loaned.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_fetch(ctx: *mut c_void, services: *const Fetch) {
    let Some(s) = (unsafe { services.as_ref() }) else {
        return;
    };
    let Some(rpc) = (unsafe { s.request.as_ref() }) else {
        return;
    };
    let Some(files) = (unsafe { rpc.files.as_ref() }) else {
        return;
    };
    let r = RpcRequest {
        r: Request {
            ctx,
            services: files,
        },
        s: rpc,
    };
    if !r.begin() {
        return;
    }
    let offset = unsafe { (s.integer)(ctx, c"offset".as_ptr()) } as isize as usize;
    let limit =
        (unsafe { (s.integer)(ctx, c"maxBytes".as_ptr()) } as isize as usize).min(4 * 1024 * 1024);
    if r.string(c"url").is_empty() || !r.r.plugin_path(r.string(c"dest")) {
        r.send(400, c"{\"error\":\"bad url/dest\"}");
        return;
    }
    unsafe {
        (s.prepare)(ctx, core::ptr::null());
        (rpc.release)(ctx, true);
        (s.prepare)(ctx, c".part".as_ptr());
    }
    let mut t = Transfer {
        ctx,
        s,
        files,
        offset,
        limit,
        written: offset,
        next_heap_log: offset,
        started: 0,
        heartbeat: 0,
        response_started: false,
        sd_full: false,
        segment_boundary: false,
        range_unsupported: false,
    };
    if offset == 0 {
        let dest = unsafe { t.path(1).bytes() };
        if let Some(last) = dest.iter().rposition(|&b| b == b'/').filter(|&n| n > 0) {
            unsafe { (s.directory)(ctx, Span::new(dest.get(..last).unwrap_or(&[]))) };
        }
        t.io(Io::Remove, 2);
        if t.io(Io::CreatePlugin, 2) == 0 {
            r.send(500, c"{\"error\":\"cannot create file\"}");
            return;
        }
    } else {
        let open = t.io(Io::OpenAppend, 2) != 0;
        let size = if open { t.io(Io::Size, 2) } else { 0 };
        if !open || size != offset {
            if open {
                t.io(Io::Close, 2);
            }
            let body = unsafe {
                (s.response)(
                    ctx,
                    Response::Offset as u32,
                    c"".as_ptr(),
                    0,
                    size,
                    false,
                    0,
                )
            };
            t.send(409, body);
            return;
        }
    }
    unsafe {
        (s.suspend)(ctx, true);
    }
    t.started = unsafe { (s.time)(ctx) };
    t.heartbeat = t.started;
    // No Rust reference to t is held while the transport calls its sink.
    let result = unsafe { (s.run)(ctx, offset, (&raw mut t).cast(), &SINK) };
    if t.io(Io::IsOpen, 2) != 0 {
        t.io(Io::Flush, 2);
        t.io(Io::Close, 2);
    }
    let mut complete =
        result.complete || (t.segment_boundary && result.total > 0 && t.written >= result.total);
    let ok = (200..300).contains(&result.status);
    let mid_segment = t.segment_boundary && !complete && ok;
    if !complete && ok && !mid_segment {
        t.io(Io::Remove, 2);
        let error = if t.sd_full {
            c"sd write failed"
        } else if t.range_unsupported {
            c"range unsupported"
        } else {
            c"download truncated"
        };
        let body = unsafe {
            (s.response)(
                ctx,
                Response::Truncated as u32,
                error.as_ptr(),
                0,
                t.written,
                false,
                0,
            )
        };
        t.diagnostic(Diagnostic::Failed, c"", false);
        t.send(502, body);
    } else {
        let mut error = core::ptr::null();
        if !ok {
            t.io(Io::Remove, 2);
            error = if result.status < 0 {
                c"transport failure"
            } else {
                c"http status"
            }
            .as_ptr();
        } else if complete {
            let replaced = unsafe {
                (files.io)(
                    ctx,
                    Io::Replace as u32,
                    0,
                    t.path(2).data.cast(),
                    t.path(1).data.cast_mut(),
                    0,
                )
            };
            if replaced == 0 {
                t.io(Io::Remove, 2);
                complete = false;
                error = c"sd write failed".as_ptr();
            } else {
                unsafe {
                    (files.book)(ctx, 3, t.path(1).data.cast(), core::ptr::null());
                }
            }
        }
        let body = unsafe {
            (s.response)(
                ctx,
                Response::Result as u32,
                error,
                result.status,
                t.written,
                complete,
                result.total,
            )
        };
        t.diagnostic(
            Diagnostic::Done,
            if mid_segment {
                c"segment done"
            } else if complete {
                c"complete"
            } else {
                c"failed"
            },
            t.connected(),
        );
        t.send(200, body);
    }
    unsafe {
        (s.suspend)(ctx, false);
    }
}
