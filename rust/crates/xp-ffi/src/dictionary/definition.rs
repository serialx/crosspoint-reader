use super::File;
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
        return 1;
    }
    let size = io.size.min(65536);
    if size == 0 {
        io.log(0, 0, 0, 0);
        return 4;
    }
    let original = if io.append { io.length } else { 0 };
    if io.append && (original > 65536 || size as usize > 65536 - original) {
        io.log(1, 65536, 0, 0);
        return 4;
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
        io.log(2, required as u32, 0, 0);
        return 2;
    }
    let offset;
    if io.plain {
        offset = io.offset;
        if !io.open(0) {
            return 4;
        }
    } else {
        offset = 0;
        if !io.open(1) {
            return 4;
        }
        let error = (io.extract)(io.context, io.offset, size);
        if error != 0 {
            io.log(4, u32::from(error), 0, 0);
            return match error {
                1 => 2,
                2 => 4,
                _ => 3,
            };
        }
        (io.close_tmp)(io.context);
        if !io.open(2) {
            return 4;
        }
    }
    let file = &*io.file;
    let dict_size = file.size() as u32;
    if offset > dict_size || size > dict_size - offset {
        io.log(5, offset, size, dict_size);
        return 4;
    }
    if !file.seek(offset as usize) {
        io.log(6, offset, 0, 0);
        return 4;
    }
    let out = (io.resize)(io.context, required, !io.append);
    if out.is_null() {
        return 2;
    }
    if file.read(slice::from_raw_parts_mut(out.add(original), size as usize)) != size as i32 {
        (io.resize)(io.context, original, false);
        return 4;
    }
    if io.html && io.heap() < required + 8192 {
        io.log(7, required as u32, 0, 0);
        return 2;
    }
    0
}
