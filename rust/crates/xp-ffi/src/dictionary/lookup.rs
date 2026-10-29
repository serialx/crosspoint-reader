use super::{bytes, engine::Location};
use core::ffi::c_void;
#[repr(C)]
pub struct View {
    data: *const u8,
    len: usize,
}
#[repr(C)]
pub struct Group {
    count: usize,
    low_memory: bool,
}
#[repr(C)]
pub struct Lookup {
    context: *mut c_void,
    word_bytes: usize,
    string_size: usize,
    free: usize,
    largest: usize,
    open: bool,
    entries: bool,
    synonyms: bool,
    html: bool,
    prepare: unsafe extern "C" fn(*mut c_void, u8) -> bool,
    view: unsafe extern "C" fn(*mut c_void, u8, usize) -> View,
    group: unsafe extern "C" fn(*mut c_void) -> Group,
    probe: unsafe extern "C" fn(*mut c_void, *const u8, usize, bool) -> bool,
    close: unsafe extern "C" fn(*mut c_void),
    title: unsafe extern "C" fn(*mut c_void, bool),
    location: unsafe extern "C" fn(*mut c_void, usize) -> Location,
    read: unsafe extern "C" fn(*mut c_void, usize, bool) -> u8,
    output: unsafe extern "C" fn(*mut c_void, u8, *const u8, usize),
    heap: unsafe extern "C" fn() -> usize,
    diagnostic: unsafe extern "C" fn(*mut c_void, u8, u32, u32),
}
impl Lookup {
    fn prepare(&self, kind: u8) -> bool {
        unsafe { (self.prepare)(self.context, kind) }
    }
    fn view(&self, kind: u8, index: usize) -> &[u8] {
        unsafe {
            let v = (self.view)(self.context, kind, index);
            bytes(v.data, v.len)
        }
    }
    fn group(&self) -> Group {
        unsafe { (self.group)(self.context) }
    }
    fn probe(&self, word: &[u8], synonyms: bool) -> bool {
        unsafe { (self.probe)(self.context, word.as_ptr(), word.len(), synonyms) }
    }
    fn title(&self, synonyms: bool) {
        unsafe { (self.title)(self.context, synonyms) }
    }
    fn log(&self, event: u8, a: u32, b: u32) {
        unsafe { (self.diagnostic)(self.context, event, a, b) }
    }
    fn output(&self, op: u8, data: &[u8]) {
        unsafe { (self.output)(self.context, op, data.as_ptr(), data.len()) }
    }
}
fn escaped_size(text: &[u8]) -> usize {
    xp_core::book_io::txt::escaped_size(text)
}
fn escape(io: &Lookup, text: &[u8]) {
    for b in text {
        io.output(
            2,
            xp_core::book_io::txt::escape(*b).unwrap_or(core::slice::from_ref(b)),
        );
    }
}
/// # Safety
/// Callbacks borrow the dictionary/session/output and return views valid until their next mutation.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_lookup(io: *const Lookup) -> u8 {
    let io = &*io;
    let working = io
        .word_bytes
        .saturating_mul(5)
        .saturating_add(6 * io.string_size)
        .saturating_add(8192);
    if io.free < working || io.largest < io.word_bytes.saturating_mul(2) {
        io.log(0, working as u32, 0);
        return 2;
    }
    io.prepare(0);
    if io.view(0, 0).is_empty() || !io.open {
        return 1;
    }
    if !io.entries {
        io.log(1, 0, 0);
        return 4;
    }
    io.prepare(1);
    if !io.prepare(2) {
        return 4;
    }
    let mut from_syn = false;
    let mut failed = io.probe(io.view(0, 0), false);
    let group = io.group();
    if group.count > 0 && !group.low_memory {
        io.title(false);
    }
    if group.count == 0 && !group.low_memory && io.synonyms {
        failed |= io.probe(io.view(0, 0), true);
        let group = io.group();
        if group.count > 0 && !group.low_memory {
            from_syn = true;
            io.title(true);
        }
    }
    let group = io.group();
    if group.count == 0 && !group.low_memory {
        io.prepare(3);
        let mut i = 0;
        loop {
            let word = io.view(1, i);
            if word.is_empty() {
                break;
            }
            failed |= io.probe(word, false);
            let group = io.group();
            if group.low_memory {
                break;
            }
            if group.count > 0 {
                io.title(false);
                break;
            }
            i += 1;
        }
    }
    (io.close)(io.context);
    let group = io.group();
    if group.low_memory {
        return 2;
    }
    if group.count == 0 {
        return if failed { 4 } else { 1 };
    }
    if group.count > 1 || from_syn {
        let mut combined = 0usize;
        let mut fit = 0;
        for i in 0..group.count {
            let size = (io.location)(io.context, i).size.min(65536) as usize;
            let separator = if combined == 0 || io.html { 0 } else { 2 };
            let head = io.view(2, i);
            let label = if io.html {
                9 + escaped_size(head)
            } else {
                2 + head.len()
            };
            if combined > 65536 - separator
                || label > 65536 - combined - separator
                || size > 65536 - combined - separator - label
            {
                break;
            }
            combined += separator + label + size;
            fit = i + 1;
        }
        if fit == 0 {
            io.log(2, 0, 0);
            return (io.read)(io.context, 0, false);
        }
        if fit < group.count {
            io.log(3, fit as u32, group.count as u32);
        }
        if (io.heap)() < combined + 8192 {
            io.log(4, combined as u32, 0);
            return 2;
        }
        // Reserve/clear is an output storage operation; no input view is retained across it.
        (io.output)(io.context, 0, core::ptr::null(), combined);
        for i in 0..fit {
            if i != 0 && !io.html {
                io.output(2, b"\n\n");
            }
            if io.html {
                io.output(2, b"<h2>");
                escape(io, io.view(2, i));
                io.output(2, b"</h2>");
            } else {
                io.output(2, b"\x01");
                io.output(2, io.view(2, i));
                io.output(2, b"\n");
            }
            let error = (io.read)(io.context, i, true);
            if error != 0 {
                return error;
            }
        }
        0
    } else {
        (io.read)(io.context, 0, false)
    }
}
