use core::ffi::c_void;
#[repr(u32)]
#[derive(Clone, Copy, PartialEq)]
pub enum Mode {
    Fetch,
    Catalog,
    Form,
    Download,
    PostFile,
}

#[repr(C)]
pub struct State {
    pub bytes: usize,
    pub total: usize,
    pub status: i32,
    pub mode: Mode,
    pub capture: bool,
    pub known: bool,
    pub aborted: bool,
    pub stopped: bool,
    pub complete: bool,
    pub same_origin: bool,
}
#[repr(C)]
pub struct Services {
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, u32, usize, usize, *const u8) -> usize,
    request: unsafe extern "C" fn(*mut c_void, *mut State) -> i32,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, usize, usize)>,
}
#[repr(u32)]
enum Op {
    Remove,
    Open,
    Close,
    Replace,
    Write,
    Progress,
    Sink,
    Append,
    Resolve,
    SameOrigin,
    LastStatus,
    LastRedirected,
    GuardEnter,
    GuardExit,
}
impl Services {
    unsafe fn op(&self, op: Op, a: usize, b: usize, p: *const u8) -> usize {
        (self.operation)(self.ctx, op as u32, a, b, p)
    }
    unsafe fn simple(&self, op: Op) -> usize {
        self.op(op, 0, 0, core::ptr::null())
    }
    unsafe fn event(&self, event: u32, a: usize, b: usize) {
        if let Some(f) = self.diagnostic {
            f(self.ctx, event, a, b);
        }
    }
}
fn success(status: i32) -> bool {
    (200..300).contains(&status)
}
#[no_mangle]
pub extern "C" fn xp_http_redirect(status: i32) -> bool {
    matches!(status, 301 | 302 | 303 | 307 | 308)
}
// Called only by the active request; no State reference lives across that callback.
#[no_mangle]
pub unsafe extern "C" fn xp_http_chunk(
    s: *const Services,
    state: *mut State,
    data: *const u8,
    len: usize,
) -> bool {
    let s = &*s;
    match (*state).mode {
        Mode::Fetch => s.op(Op::Sink, len, 0, data) != 0,
        Mode::Catalog => {
            if !success((*state).status) && !((*state).capture && (*state).status == 401) {
                return true;
            }
            if s.op(Op::Sink, len, 0, data) == 0 {
                (*state).stopped = true;
                return false;
            }
            true
        }
        Mode::Form => {
            let n = len.min(4096usize.saturating_sub((*state).bytes));
            s.op(Op::Append, n, 0, data);
            (*state).bytes += n;
            true
        }
        Mode::Download => s.op(Op::Write, len, 0, data) == len,
        Mode::PostFile => {
            if !success((*state).status) {
                return true;
            }
            if s.op(Op::Write, len, 0, data) != len {
                (*state).stopped = true;
                return false;
            }
            (*state).bytes = (*state).bytes.wrapping_add(len);
            if (*state).known {
                s.op(
                    Op::Progress,
                    (*state).bytes,
                    (*state).total,
                    core::ptr::null(),
                );
            }
            true
        }
    }
}
unsafe fn get_result(s: &Services, state: &State) -> u32 {
    if state.aborted {
        return 3;
    }
    if state.stopped {
        return 2;
    }
    if matches!(state.status, 401 | 403) {
        s.event(0, state.status as usize, 0);
        return 4;
    }
    if !success(state.status) {
        s.event(1, state.status as usize, 0);
        return 1;
    }
    if !state.complete {
        s.event(2, state.bytes, state.total);
        return 1;
    }
    0
}
// Modes: simple GET=0, catalog GET=1, form POST=2, file GET=3, file POST=4.
#[no_mangle]
pub unsafe extern "C" fn xp_http_run(s: *const Services, state: *mut State) -> u32 {
    let s = &*s;
    let mode = (*state).mode;
    let file = matches!(mode, Mode::Download | Mode::PostFile);
    if file {
        s.simple(Op::Remove);
        if s.simple(Op::Open) == 0 {
            s.event(3, 0, 0);
            return 2;
        }
    }
    if mode == Mode::Fetch || mode == Mode::Download {
        s.simple(Op::GuardEnter);
    }
    for hop in 0..=5 {
        (*state).same_origin = s.simple(Op::SameOrigin) != 0;
        let status = (s.request)(s.ctx, state);
        (*state).status = status;
        if mode != Mode::Catalog || !xp_http_redirect(status) || s.simple(Op::Resolve) == 0 {
            break;
        }
        if hop == 5 {
            break;
        }
    }
    if matches!(mode, Mode::Fetch | Mode::Catalog | Mode::Download) {
        s.op(
            Op::LastStatus,
            (*state).status.max(0) as usize,
            0,
            core::ptr::null(),
        );
        let same = if mode == Mode::Catalog {
            s.simple(Op::SameOrigin) != 0
        } else {
            (*state).same_origin
        };
        s.op(Op::LastRedirected, usize::from(!same), 0, core::ptr::null());
    }
    if mode == Mode::Catalog || mode == Mode::Form {
        return u32::from(!success((*state).status) || (*state).stopped);
    }
    // A malformed POST URL returns before the explicit close/remove in the original.
    if mode == Mode::PostFile && !(*state).complete && (*state).status == i32::MIN {
        return 1;
    }
    if mode == Mode::PostFile {
        s.simple(Op::Close);
    }
    let result = if mode == Mode::PostFile {
        if (*state).aborted {
            3
        } else if (*state).stopped {
            2
        } else if !success((*state).status) {
            s.event(4, (*state).status as usize, 0);
            1
        } else if (*state).bytes == 0 {
            s.event(5, 0, 0);
            1
        } else if (*state).known && (*state).bytes != (*state).total {
            s.event(6, (*state).bytes, (*state).total);
            1
        } else {
            0
        }
    } else {
        get_result(s, &*state)
    };
    if mode == Mode::Fetch || mode == Mode::Download {
        s.simple(Op::GuardExit);
    }
    if mode == Mode::Download {
        s.simple(Op::Close);
    }
    if !file {
        return result;
    }
    if result != 0 {
        s.simple(Op::Remove);
        return result;
    }
    if (*state).bytes == 0 {
        s.event(5, 0, 0);
        s.simple(Op::Remove);
        return 1;
    }
    if s.simple(Op::Replace) == 0 {
        s.event(7, 0, 0);
        s.simple(Op::Remove);
        return 2;
    }
    s.event(8, (*state).bytes, 0);
    0
}

#[no_mangle]
pub extern "C" fn xp_http_auth(
    same_origin: bool,
    bearer_len: usize,
    user_len: usize,
    password_len: usize,
) -> u32 {
    if !same_origin {
        0
    } else if bearer_len != 0 {
        2
    } else {
        u32::from(user_len != 0 && password_len != 0)
    }
}
