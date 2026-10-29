//! Fixed browser-executed job queue. The server loans inline storage for its lifetime.
use super::{c_bytes, component, Output, Request, Services, Span};
use core::ffi::{c_char, c_ulong, c_void, CStr};

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
enum State {
    Empty,
    Pending,
    Running,
    Done,
    Error,
}
impl State {
    fn finished(self) -> bool {
        matches!(self, Self::Done | Self::Error)
    }
    fn name(self) -> &'static [u8] {
        match self {
            Self::Empty => b"empty",
            Self::Pending => b"pending",
            Self::Running => b"running",
            Self::Done => b"done",
            Self::Error => b"error",
        }
    }
}
#[repr(C)]
struct Job {
    id: u32,
    claim: u32,
    updated_at: u32,
    state: State,
    plugin: [u8; 24],
    action: [u8; 24],
    args: [u8; 192],
    result: [u8; 192],
}
#[repr(C)]
pub struct Queue {
    jobs: [Job; 6],
    next_id: u32,
    next_claim: u32,
}
const _: () = assert!(core::mem::size_of::<Queue>() == 2696);
const _: () = assert!(core::mem::align_of::<Queue>() == 4);

#[repr(C)]
pub struct JobServices {
    files: *const Services,
    parse: unsafe extern "C" fn(*mut c_void, Span) -> Span,
    string: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    integer: unsafe extern "C" fn(*mut c_void, *const c_char) -> u32,
    boolean: unsafe extern "C" fn(*mut c_void, *const c_char) -> bool,
    // Null output measures; otherwise serializes to the supplied bounded buffer.
    json: unsafe extern "C" fn(*mut c_void, *const c_char, *mut u8, usize) -> usize,
    time: unsafe extern "C" fn(*mut c_void) -> u32,
    send: unsafe extern "C" fn(*mut c_void, u16, *const c_char),
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, Span, Span)>,
}
#[repr(u32)]
enum Event {
    Queued,
    Expired,
    Stale,
    Done,
    Failed,
}
struct JobRequest<'a> {
    r: Request<'a>,
    s: &'a JobServices,
}
fn copy_text(out: &mut [u8], bytes: &[u8]) {
    for (to, from) in out.iter_mut().zip(bytes) {
        *to = *from;
    }
}
// strtoul base 10 semantics, including the platform's unsigned-long width.
fn status_id(mut bytes: &[u8]) -> u32 {
    while bytes
        .first()
        .is_some_and(|b| matches!(*b, b' ' | b'\t'..=b'\r'))
    {
        bytes = &bytes[1..];
    }
    let negative = bytes.first() == Some(&b'-');
    if negative || bytes.first() == Some(&b'+') {
        bytes = &bytes[1..];
    }
    let mut value: c_ulong = 0;
    for &b in bytes {
        if !b.is_ascii_digit() {
            break;
        }
        let Some(n) = value
            .checked_mul(10)
            .and_then(|n| n.checked_add((b - b'0') as c_ulong))
        else {
            return u32::MAX;
        };
        value = n;
    }
    if negative {
        value.wrapping_neg() as u32
    } else {
        value as u32
    }
}
impl JobRequest<'_> {
    fn send(&self, code: u16, text: &CStr) {
        unsafe { (self.s.send)(self.r.ctx, code, text.as_ptr()) }
    }
    fn event(&self, event: Event, job: &Job) {
        if let Some(diagnostic) = self.s.diagnostic {
            unsafe {
                diagnostic(
                    self.r.ctx,
                    event as u32,
                    job.id,
                    Span::new(c_bytes(&job.plugin)),
                    Span::new(c_bytes(&job.action)),
                )
            }
        }
    }
    fn time(&self) -> u32 {
        unsafe { (self.s.time)(self.r.ctx) }
    }
    fn arg(&self, key: &CStr) -> Span {
        unsafe { (self.r.services.argument)(self.r.ctx, key.as_ptr()) }
    }
    fn string(&self, key: &CStr) -> &[u8] {
        unsafe { (self.s.string)(self.r.ctx, key.as_ptr()).bytes() }
    }
    fn integer(&self, key: &CStr) -> u32 {
        unsafe { (self.s.integer)(self.r.ctx, key.as_ptr()) }
    }
    fn measure(&self, key: &CStr) -> usize {
        unsafe { (self.s.json)(self.r.ctx, key.as_ptr(), core::ptr::null_mut(), 0) }
    }
    fn serialize(&self, key: &CStr, out: &mut [u8]) {
        unsafe {
            (self.s.json)(self.r.ctx, key.as_ptr(), out.as_mut_ptr(), out.len());
        }
    }
    fn submit(&self, q: &mut Queue) {
        if !self.r.json_body(self.s.parse, self.s.send) {
            return;
        }
        let plugin = self.string(c"plugin");
        let action = self.string(c"action");
        let args_len = self.measure(c"args");
        if !component(plugin)
            || action.is_empty()
            || !action
                .iter()
                .all(|b| b.is_ascii_alphanumeric() || b"_-.".contains(b))
            || plugin.len() >= 24
            || action.len() >= 24
            || args_len >= 192
        {
            self.send(400, c"{\"error\":\"bad plugin/action/args\"}");
            return;
        }
        let mut best: Option<(usize, u32)> = None;
        for (i, job) in q.jobs.iter().enumerate() {
            if job.state == State::Empty {
                best = Some((i, job.updated_at));
                break;
            }
            if job.state.finished() && best.is_none_or(|(_, updated)| job.updated_at < updated) {
                best = Some((i, job.updated_at));
            }
        }
        let Some(job) = best.and_then(|(i, _)| q.jobs.get_mut(i)) else {
            self.send(503, c"{\"error\":\"job queue full\"}");
            return;
        };
        // Zero is valid for every field, including State::Empty; no large stack temporary.
        unsafe { core::ptr::write_bytes(job, 0, 1) };
        job.id = q.next_id;
        q.next_id = q.next_id.wrapping_add(1);
        job.state = State::Pending;
        job.updated_at = self.time();
        copy_text(&mut job.plugin, plugin);
        copy_text(&mut job.action, action);
        if args_len != 0 {
            self.serialize(c"args", &mut job.args);
        }
        self.event(Event::Queued, job);
        let mut bytes = [0; 48];
        let mut out = Output::new(&mut bytes);
        out.add(b"{\"id\":");
        out.number(job.id as usize);
        out.add(b"}\0");
        unsafe { (self.s.send)(self.r.ctx, 200, bytes.as_ptr().cast()) }
    }
    fn claim(&self, q: &mut Queue) {
        let plugin = self.arg(c"plugin");
        let plugin = c_bytes(unsafe { plugin.bytes() });
        let now = self.time();
        for job in &mut q.jobs {
            if job.state == State::Running && now.wrapping_sub(job.updated_at) > 600_000 {
                job.state = State::Pending;
                job.updated_at = now;
                self.event(Event::Expired, job);
            }
            if job.state != State::Pending || plugin != c_bytes(&job.plugin) {
                continue;
            }
            job.state = State::Running;
            job.claim = q.next_claim;
            q.next_claim = q.next_claim.wrapping_add(1);
            job.updated_at = now;
            self.describe(job, true);
            return;
        }
        self.send(200, c"{\"id\":0}");
    }
    fn complete(&self, q: &mut Queue) {
        if !self.r.json_body(self.s.parse, self.s.send) {
            return;
        }
        let id = self.integer(c"id");
        let claim = self.integer(c"claim");
        for job in &mut q.jobs {
            if job.id != id {
                continue;
            }
            if job.state.finished() {
                self.send(200, c"{\"ok\":true}");
                return;
            }
            if job.state != State::Running {
                break;
            }
            if job.claim != claim {
                self.event(Event::Stale, job);
                self.send(409, c"{\"error\":\"stale claim\"}");
                return;
            }
            job.state = if unsafe { (self.s.boolean)(self.r.ctx, c"ok".as_ptr()) } {
                State::Done
            } else {
                State::Error
            };
            job.updated_at = self.time();
            job.result[0] = 0;
            let length = self.measure(c"result");
            if length >= job.result.len() {
                copy_text(&mut job.result, b"{\"error\":\"result too large\"}\0");
            } else if length != 0 {
                self.serialize(c"result", &mut job.result);
            }
            self.event(
                if job.state == State::Done {
                    Event::Done
                } else {
                    Event::Failed
                },
                job,
            );
            self.send(200, c"{\"ok\":true}");
            return;
        }
        self.send(404, c"{\"error\":\"no such running job\"}");
    }
    fn status(&self, q: &Queue) {
        let id = status_id(unsafe { self.arg(c"id").bytes() });
        for job in &q.jobs {
            if job.id == id && job.state != State::Empty {
                self.describe(job, false);
                return;
            }
        }
        let mut bytes = [0; 64];
        let mut out = Output::new(&mut bytes);
        out.add(b"{\"id\":");
        out.number(id as usize);
        out.add(b",\"state\":\"unknown\",\"result\":null}\0");
        unsafe { (self.s.send)(self.r.ctx, 200, bytes.as_ptr().cast()) }
    }
    #[inline(never)]
    fn describe(&self, job: &Job, claim: bool) {
        // Maximum claim: two 10-digit IDs, 23-byte action, 191-byte JSON, punctuation + NUL = 271 bytes.
        let mut bytes = [0; 272];
        let mut out = Output::new(&mut bytes);
        out.add(b"{\"id\":");
        out.number(job.id as usize);
        let value = if claim {
            out.add(b",\"claim\":");
            out.number(job.claim as usize);
            out.add(b",\"action\":\"");
            out.add(c_bytes(&job.action));
            out.add(b"\",\"args\":");
            if job.args[0] == 0 {
                b"{}"
            } else {
                c_bytes(&job.args)
            }
        } else {
            out.add(b",\"state\":\"");
            out.add(job.state.name());
            out.add(b"\",\"result\":");
            if job.result[0] == 0 {
                b"null"
            } else {
                c_bytes(&job.result)
            }
        };
        out.add(value);
        out.add(b"}\0");
        unsafe { (self.s.send)(self.r.ctx, 200, bytes.as_ptr().cast()) }
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_job_init(q: *mut Queue) {
    if q.is_null() {
        return;
    }
    unsafe {
        core::ptr::write_bytes(q, 0, 1);
        (*q).next_id = 1;
        (*q).next_claim = 1;
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_job_route(
    ctx: *mut c_void,
    services: *const JobServices,
    queue: *mut Queue,
    route: u32,
) {
    let (Some(s), Some(q)) = (unsafe { services.as_ref() }, unsafe { queue.as_mut() }) else {
        return;
    };
    let Some(files) = (unsafe { s.files.as_ref() }) else {
        return;
    };
    let r = JobRequest {
        r: Request {
            ctx,
            services: files,
        },
        s,
    };
    if r.r.flags() & 32 == 0 {
        r.r.send(403, b"plugin system disabled");
        return;
    }
    match route {
        0 => r.submit(q),
        1 => r.claim(q),
        2 => r.complete(q),
        3 => r.status(q),
        _ => {}
    }
}
