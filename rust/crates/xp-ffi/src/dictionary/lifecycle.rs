use super::abi::*;
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
        (io.diagnostic)(io.context, XP_DICTIONARY_OPEN_EVENT_MISSING_FOLDER, 0);
        return false;
    }
    if !(io.exists)(io.context, XP_DICTIONARY_EXISTS_INDEX) {
        (io.diagnostic)(io.context, XP_DICTIONARY_OPEN_EVENT_MISSING_INDEX, 0);
        return false;
    }
    *io.plain = (io.exists)(io.context, XP_DICTIONARY_EXISTS_PLAIN);
    if !*io.plain && !(io.exists)(io.context, XP_DICTIONARY_EXISTS_COMPRESSED) {
        (io.diagnostic)(io.context, XP_DICTIONARY_OPEN_EVENT_MISSING_DATA, 0);
        return false;
    }
    let facts = (io.ifo)(io.context);
    if facts & XP_DICTIONARY_IFO_OFFSETS64 != 0 {
        (io.diagnostic)(io.context, XP_DICTIONARY_OPEN_EVENT_OFFSETS64, 0);
        return false;
    }
    let n = (io.length)(io.context);
    if n > XP_DICTIONARY_PATH_BYTES - XP_DICTIONARY_LONGEST_SUFFIX_BYTES - 1 {
        (io.diagnostic)(io.context, XP_DICTIONARY_OPEN_EVENT_PATH_LENGTH, n);
        return false;
    }
    *io.syn = (io.exists)(io.context, XP_DICTIONARY_EXISTS_SYNONYMS);
    *io.html = facts & XP_DICTIONARY_IFO_HTML != 0;
    if !(io.allocate)(io.context) {
        (io.diagnostic)(io.context, XP_DICTIONARY_OPEN_EVENT_ALLOCATION, 0);
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
        return if build {
            XP_DICTIONARY_INDEX_READ_ERROR
        } else {
            XP_DICTIONARY_INDEX_OK
        };
    }
    if (io.stale)(io.context, false) {
        if !build {
            return 1;
        }
        let status = (io.build)(io.context, false);
        if status != XP_DICTIONARY_INDEX_OK {
            return status;
        }
    }
    if io.syn && (io.stale)(io.context, true) {
        if !build {
            return 1;
        }
        if (io.build)(io.context, true) != XP_DICTIONARY_INDEX_OK {
            (io.diagnostic)(io.context);
        }
    }
    XP_DICTIONARY_INDEX_OK
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
        if !io.open || !(io.open_file)(io.context, XP_DICTIONARY_SESSION_INDEX) {
            return false;
        }
        state.idx_size = (io.size)(io.context, XP_DICTIONARY_SESSION_INDEX);
        if (io.open_file)(io.context, XP_DICTIONARY_SESSION_SAMPLES) {
            let h = (io.header)(io.context, XP_DICTIONARY_SESSION_SAMPLES);
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
    if !(io.open_file)(io.context, XP_DICTIONARY_SESSION_SYNONYMS) {
        state.syn_failed = true;
        return false;
    }
    state.syn_size = (io.size)(io.context, XP_DICTIONARY_SESSION_SYNONYMS);
    if (io.open_file)(io.context, XP_DICTIONARY_SESSION_SYNONYM_SAMPLES) {
        let h = (io.header)(io.context, XP_DICTIONARY_SESSION_SYNONYM_SAMPLES);
        if h.valid && h.source_size == state.syn_size {
            state.syn_samples = h.samples;
        }
    }
    if state.syn_samples == 0 {
        (io.diagnostic)(io.context);
        state.syn_failed = true;
        state.syn_size = 0;
        (io.close)(io.context, XP_DICTIONARY_SESSION_SYNONYM_SAMPLES);
        (io.close)(io.context, XP_DICTIONARY_SESSION_SYNONYMS);
        return false;
    }
    true
}
#[repr(C)]
pub struct Append {
    context: *mut c_void,
    count: usize,
    entry_capacity: usize,
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
            return XP_DICTIONARY_APPEND_OK;
        }
    }
    if io.count >= io.entry_capacity {
        return XP_DICTIONARY_APPEND_FULL;
    }
    if io.bytes > io.capacity
        && (io.free < io.bytes.saturating_add(8192)
            || io.largest < io.bytes
            || !(io.allocate)(io.context, io.bytes))
    {
        return XP_DICTIONARY_APPEND_LOW_MEMORY;
    }
    (io.finish)(io.context);
    XP_DICTIONARY_APPEND_OK
}
