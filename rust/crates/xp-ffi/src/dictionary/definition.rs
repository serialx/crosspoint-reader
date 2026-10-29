use super::{abi::*, File};
use core::{ffi::c_void, slice};
#[repr(C)]
pub struct Definition {
    context: *mut c_void,
    file: *const File,
    offset: u32,
    size: u32,
    found: bool,
    plain: bool,
    html: bool,
    append: bool,
    length: usize,
    capacity: usize,
    heap: unsafe extern "C" fn() -> usize,
    open: unsafe extern "C" fn(*mut c_void, u8) -> bool,
    extract: unsafe extern "C" fn(*mut c_void, u32, u32) -> u8,
    close_tmp: unsafe extern "C" fn(*mut c_void),
    resize: unsafe extern "C" fn(*mut c_void, usize, bool) -> *mut u8,
    diagnostic: unsafe extern "C" fn(*mut c_void, u8, u32, u32, u32),
}
impl Definition {
    fn log(&self, event: u8, a: u32, b: u32, c: u32) {
        unsafe { (self.diagnostic)(self.context, event, a, b, c) }
    }
    fn heap(&self) -> usize {
        unsafe { (self.heap)() }
    }
    fn open(&self, kind: u8) -> bool {
        unsafe { (self.open)(self.context, kind) }
    }
}
/// # Safety
/// Descriptor and callbacks borrow the dictionary and output; resize returns exclusive initialized bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_definition(io: *const Definition) -> u8 {
    let io = &*io;
    if !io.found {
        return XP_DICTIONARY_LOOKUP_NOT_FOUND;
    }
    let size = io.size.min(XP_DICTIONARY_MAX_DEFINITION_BYTES as u32);
    if size == 0 {
        io.log(XP_DICTIONARY_DEFINITION_EVENT_EMPTY, 0, 0, 0);
        return XP_DICTIONARY_LOOKUP_READ_ERROR;
    }
    let original = if io.append { io.length } else { 0 };
    if io.append
        && (original > XP_DICTIONARY_MAX_DEFINITION_BYTES
            || size as usize > XP_DICTIONARY_MAX_DEFINITION_BYTES - original)
    {
        io.log(
            XP_DICTIONARY_DEFINITION_EVENT_COMBINED_CAP,
            XP_DICTIONARY_MAX_DEFINITION_BYTES as u32,
            0,
            0,
        );
        return XP_DICTIONARY_LOOKUP_READ_ERROR;
    }
    let required = original + size as usize;
    let allocation = if required > io.capacity {
        required
            .max(io.capacity.saturating_mul(2))
            .saturating_add(1)
    } else {
        0
    };
    if io.heap() < allocation.saturating_add(8192) {
        io.log(
            XP_DICTIONARY_DEFINITION_EVENT_LOW_HEAP,
            required as u32,
            0,
            0,
        );
        return XP_DICTIONARY_LOOKUP_LOW_MEMORY;
    }
    let offset;
    if io.plain {
        offset = io.offset;
        if !io.open(XP_DICTIONARY_DATA_OPEN_PLAIN) {
            return XP_DICTIONARY_LOOKUP_READ_ERROR;
        }
    } else {
        offset = 0;
        if !io.open(XP_DICTIONARY_DATA_OPEN_COMPRESSED) {
            return XP_DICTIONARY_LOOKUP_READ_ERROR;
        }
        let error = (io.extract)(io.context, io.offset, size);
        if error != XP_DICTIONARY_ZIP_OK {
            io.log(
                XP_DICTIONARY_DEFINITION_EVENT_EXTRACT,
                u32::from(error),
                0,
                0,
            );
            return match error {
                XP_DICTIONARY_ZIP_LOW_MEMORY => XP_DICTIONARY_LOOKUP_LOW_MEMORY,
                XP_DICTIONARY_ZIP_READ_ERROR => XP_DICTIONARY_LOOKUP_READ_ERROR,
                _ => XP_DICTIONARY_LOOKUP_DECOMPRESS,
            };
        }
        (io.close_tmp)(io.context);
        if !io.open(XP_DICTIONARY_DATA_OPEN_TEMPORARY) {
            return XP_DICTIONARY_LOOKUP_READ_ERROR;
        }
    }
    let file = &*io.file;
    let dict_size = file.size() as u32;
    if offset > dict_size || size > dict_size - offset {
        io.log(
            XP_DICTIONARY_DEFINITION_EVENT_BOUNDS,
            offset,
            size,
            dict_size,
        );
        return XP_DICTIONARY_LOOKUP_READ_ERROR;
    }
    if !file.seek(offset as usize) {
        io.log(XP_DICTIONARY_DEFINITION_EVENT_SEEK, offset, 0, 0);
        return XP_DICTIONARY_LOOKUP_READ_ERROR;
    }
    let out = (io.resize)(io.context, required, !io.append);
    if out.is_null() {
        return XP_DICTIONARY_LOOKUP_LOW_MEMORY;
    }
    if file.read(slice::from_raw_parts_mut(out.add(original), size as usize)) != size as i32 {
        (io.resize)(io.context, original, false);
        return XP_DICTIONARY_LOOKUP_READ_ERROR;
    }
    if io.html && io.heap() < required + 8192 {
        io.log(
            XP_DICTIONARY_DEFINITION_EVENT_FALLBACK,
            required as u32,
            0,
            0,
        );
        return XP_DICTIONARY_LOOKUP_LOW_MEMORY;
    }
    XP_DICTIONARY_LOOKUP_FOUND
}
