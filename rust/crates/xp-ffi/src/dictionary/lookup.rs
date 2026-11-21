use super::{abi::*, bytes, engine::Location};
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
    read: unsafe extern "C" fn(*mut c_void, usize, bool, *mut usize) -> u8,
    output: unsafe extern "C" fn(*mut c_void, u8, *const u8, usize) -> usize,
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
        unsafe { (self.output)(self.context, op, data.as_ptr(), data.len()) };
    }
}
fn escaped_size(text: &[u8]) -> usize {
    xp_core::book_io::txt::escaped_size(text)
}
fn escape(io: &Lookup, text: &[u8]) {
    for b in text {
        io.output(
            XP_DICTIONARY_OUTPUT_APPEND,
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
        io.log(XP_DICTIONARY_LOOKUP_EVENT_WORKING_SET, working as u32, 0);
        return XP_DICTIONARY_LOOKUP_LOW_MEMORY;
    }
    io.prepare(XP_DICTIONARY_PREPARE_CLEAN);
    if io.view(XP_DICTIONARY_VIEW_CLEANED, 0).is_empty() || !io.open {
        return XP_DICTIONARY_LOOKUP_NOT_FOUND;
    }
    if !io.entries {
        io.log(XP_DICTIONARY_LOOKUP_EVENT_MISSING_ENTRIES, 0, 0);
        return XP_DICTIONARY_LOOKUP_READ_ERROR;
    }
    io.prepare(XP_DICTIONARY_PREPARE_CLEAR);
    if !io.prepare(XP_DICTIONARY_PREPARE_OPEN) {
        return XP_DICTIONARY_LOOKUP_READ_ERROR;
    }
    let mut from_syn = false;
    let mut failed = io.probe(io.view(XP_DICTIONARY_VIEW_CLEANED, 0), false);
    let group = io.group();
    if group.count > 0 && !group.low_memory {
        io.title(false);
    }
    if group.count == 0 && !group.low_memory && io.synonyms {
        failed |= io.probe(io.view(XP_DICTIONARY_VIEW_CLEANED, 0), true);
        let group = io.group();
        if group.count > 0 && !group.low_memory {
            from_syn = true;
            io.title(true);
        }
    }
    let group = io.group();
    if group.count == 0 && !group.low_memory {
        io.prepare(XP_DICTIONARY_PREPARE_STEM);
        let mut i = 0;
        loop {
            let word = io.view(XP_DICTIONARY_VIEW_VARIANT, i);
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
        return XP_DICTIONARY_LOOKUP_LOW_MEMORY;
    }
    if group.count == 0 {
        return if failed {
            XP_DICTIONARY_LOOKUP_READ_ERROR
        } else {
            XP_DICTIONARY_LOOKUP_NOT_FOUND
        };
    }
    if group.count > 1 || from_syn {
        #[inline(never)]
        unsafe fn combine(io: &Lookup, count: usize) -> u8 {
            let entry_size = |i, separator: usize| {
                let head = io.view(XP_DICTIONARY_VIEW_HEADWORD, i);
                let label = if io.html {
                    9 + escaped_size(head)
                } else {
                    separator + 2 + head.len()
                };
                let raw = (io.location)(io.context, i).size as usize;
                label + raw.min(XP_DICTIONARY_MAX_DEFINITION_BYTES)
            };
            let mut reserve = 0;
            for i in 0..count {
                let size = entry_size(i, 2);
                if size <= XP_DICTIONARY_MAX_DEFINITION_BYTES - reserve {
                    reserve += size;
                }
            }
            if (io.heap)() < reserve + 8192 {
                io.log(XP_DICTIONARY_LOOKUP_EVENT_LOW_HEAP, reserve as u32, 0);
                return XP_DICTIONARY_LOOKUP_LOW_MEMORY;
            }
            let mut capacity = (io.output)(
                io.context,
                XP_DICTIONARY_OUTPUT_RESERVE,
                core::ptr::null(),
                reserve,
            );
            if capacity < reserve {
                return XP_DICTIONARY_LOOKUP_LOW_MEMORY;
            }
            let mut length = 0;
            for i in 0..count {
                let separator = if length == 0 { 0 } else { 2 };
                let required = length + entry_size(i, separator);
                if required > XP_DICTIONARY_MAX_DEFINITION_BYTES {
                    io.log(XP_DICTIONARY_LOOKUP_EVENT_SKIP_ENTRY, 0, 0);
                    continue;
                }
                if required > capacity {
                    let allocation = required.max(capacity.saturating_mul(2)).saturating_add(1);
                    if (io.heap)() < allocation.saturating_add(8192) {
                        io.log(XP_DICTIONARY_LOOKUP_EVENT_LOW_HEAP, required as u32, 0);
                        return XP_DICTIONARY_LOOKUP_LOW_MEMORY;
                    }
                    let grown = (io.output)(
                        io.context,
                        XP_DICTIONARY_OUTPUT_GROW,
                        core::ptr::null(),
                        required,
                    );
                    if grown < required {
                        return XP_DICTIONARY_LOOKUP_LOW_MEMORY;
                    }
                }
                if io.html {
                    io.output(XP_DICTIONARY_OUTPUT_APPEND, b"<h2>");
                    escape(io, io.view(XP_DICTIONARY_VIEW_HEADWORD, i));
                    io.output(XP_DICTIONARY_OUTPUT_APPEND, b"</h2>");
                } else {
                    if separator != 0 {
                        io.output(XP_DICTIONARY_OUTPUT_APPEND, b"\n\n");
                    }
                    io.output(XP_DICTIONARY_OUTPUT_APPEND, b"\x01");
                    io.output(
                        XP_DICTIONARY_OUTPUT_APPEND,
                        io.view(XP_DICTIONARY_VIEW_HEADWORD, i),
                    );
                    io.output(XP_DICTIONARY_OUTPUT_APPEND, b"\n");
                }
                let previous = length;
                let status = (io.read)(io.context, i, true, &mut length);
                if status != XP_DICTIONARY_LOOKUP_FOUND {
                    length = previous;
                    (io.output)(
                        io.context,
                        XP_DICTIONARY_OUTPUT_RESTORE,
                        core::ptr::null(),
                        length,
                    );
                    if !matches!(
                        status,
                        XP_DICTIONARY_LOOKUP_ENTRY_CAP | XP_DICTIONARY_LOOKUP_NOT_FOUND
                    ) {
                        return status;
                    }
                }
                capacity = (io.output)(
                    io.context,
                    XP_DICTIONARY_OUTPUT_CAPACITY,
                    core::ptr::null(),
                    0,
                );
            }
            if length != 0 {
                return XP_DICTIONARY_LOOKUP_FOUND;
            }
            (io.read)(io.context, 0, false, &mut length)
        }
        combine(io, group.count)
    } else {
        (io.read)(io.context, 0, false, &mut 0)
    }
}
