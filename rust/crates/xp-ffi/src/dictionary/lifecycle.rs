use core::ffi::c_void;
#[repr(C)]
pub struct Open {
    context: *mut c_void,
    plain: *mut bool,
    syn: *mut bool,
    html: *mut bool,
    reset: unsafe extern "C" fn(*mut c_void),
    resolve: unsafe extern "C" fn(*mut c_void) -> bool,
    exists: unsafe extern "C" fn(*mut c_void, u8) -> bool,
    ifo: unsafe extern "C" fn(*mut c_void) -> u8,
    length: unsafe extern "C" fn(*mut c_void) -> usize,
    allocate: unsafe extern "C" fn(*mut c_void) -> bool,
    finish: unsafe extern "C" fn(*mut c_void),
    diagnostic: unsafe extern "C" fn(*mut c_void, u8, usize),
}
/// # Safety
/// Callbacks borrow the live dictionary; scalar pointers address distinct writable fields.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_open(io: *const Open) -> bool {
    let io = &*io;
    (io.reset)(io.context);
    *io.syn = false;
    *io.html = false;
    if !(io.resolve)(io.context) {
        (io.diagnostic)(io.context, 0, 0);
        return false;
    }
    if !(io.exists)(io.context, 0) {
        (io.diagnostic)(io.context, 1, 0);
        return false;
    }
    *io.plain = (io.exists)(io.context, 1);
    if !*io.plain && !(io.exists)(io.context, 2) {
        (io.diagnostic)(io.context, 2, 0);
        return false;
    }
    let facts = (io.ifo)(io.context);
    if facts & 1 != 0 {
        (io.diagnostic)(io.context, 3, 0);
        return false;
    }
    let n = (io.length)(io.context);
    if n > 151 {
        (io.diagnostic)(io.context, 4, n);
        return false;
    }
    *io.syn = (io.exists)(io.context, 3);
    *io.html = facts & 2 != 0;
    if !(io.allocate)(io.context) {
        (io.diagnostic)(io.context, 5, 0);
        return false;
    }
    (io.finish)(io.context);
    true
}
#[repr(C)]
pub struct Index {
    context: *mut c_void,
    open: bool,
    syn: bool,
    stale: unsafe extern "C" fn(*mut c_void, bool) -> bool,
    build: unsafe extern "C" fn(*mut c_void, bool) -> u8,
    diagnostic: unsafe extern "C" fn(*mut c_void),
}
/// # Safety
/// Callbacks and context are valid for this call; they do not retain temporary arguments.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_index(io: *const Index, build: bool) -> u8 {
    let io = &*io;
    if !io.open {
        return if build { 2 } else { 0 };
    }
    if (io.stale)(io.context, false) {
        if !build {
            return 1;
        }
        let status = (io.build)(io.context, false);
        if status != 0 {
            return status;
        }
    }
    if io.syn && (io.stale)(io.context, true) {
        if !build {
            return 1;
        }
        if (io.build)(io.context, true) != 0 {
            (io.diagnostic)(io.context);
        }
    }
    0
}
use super::engine::Header;
#[repr(C)]
pub struct Session {
    idx_size: u32,
    samples: u32,
    entries: u32,
    syn_size: u32,
    syn_samples: u32,
    syn_opened: bool,
    syn_failed: bool,
}
#[repr(C)]
pub struct SessionIo {
    context: *mut c_void,
    state: *mut Session,
    open: bool,
    syn: bool,
    open_file: unsafe extern "C" fn(*mut c_void, u8) -> bool,
    size: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    header: unsafe extern "C" fn(*mut c_void, u8) -> Header,
    close: unsafe extern "C" fn(*mut c_void, u8),
    diagnostic: unsafe extern "C" fn(*mut c_void),
}
/// # Safety
/// State and callbacks borrow distinct metadata and HAL handles for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_session(io: *const SessionIo, synonyms: bool) -> bool {
    let io = &*io;
    let state = &mut *io.state;
    if !synonyms {
        if !io.open || !(io.open_file)(io.context, 0) {
            return false;
        }
        state.idx_size = (io.size)(io.context, 0);
        if (io.open_file)(io.context, 1) {
            let h = (io.header)(io.context, 1);
            if h.valid && h.source_size == state.idx_size {
                state.samples = h.samples;
                state.entries = h.entries;
            }
        }
        return true;
    }
    if state.syn_opened {
        return state.syn_size > 0;
    }
    state.syn_opened = true;
    if !io.syn {
        return false;
    }
    if !(io.open_file)(io.context, 2) {
        state.syn_failed = true;
        return false;
    }
    state.syn_size = (io.size)(io.context, 2);
    if (io.open_file)(io.context, 3) {
        let h = (io.header)(io.context, 3);
        if h.valid && h.source_size == state.syn_size {
            state.syn_samples = h.samples;
        }
    }
    if state.syn_samples == 0 {
        (io.diagnostic)(io.context);
        state.syn_failed = true;
        state.syn_size = 0;
        (io.close)(io.context, 3);
        (io.close)(io.context, 2);
        return false;
    }
    true
}
#[repr(C)]
pub struct Append {
    context: *mut c_void,
    count: usize,
    capacity: usize,
    bytes: usize,
    free: usize,
    largest: usize,
    location: unsafe extern "C" fn(*mut c_void, usize) -> super::engine::Location,
    allocate: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    finish: unsafe extern "C" fn(*mut c_void),
}
/// # Safety
/// Callbacks borrow the entry list and one terminated headword; no borrowed storage is retained.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_append(io: *const Append, offset: u32, size: u32) -> u8 {
    let io = &*io;
    for i in 0..io.count {
        let l = (io.location)(io.context, i);
        if l.offset == offset && l.size == size {
            return 0;
        }
    }
    if io.count >= 128 {
        return 1;
    }
    if io.bytes > io.capacity
        && (io.free < io.bytes.saturating_add(8192)
            || io.largest < io.bytes
            || !(io.allocate)(io.context, io.bytes))
    {
        return 2;
    }
    (io.finish)(io.context);
    0
}
