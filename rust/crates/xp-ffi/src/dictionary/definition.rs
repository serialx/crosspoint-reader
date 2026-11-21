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
    inline_types: bool,
    length: usize,
    capacity: usize,
    heap: unsafe extern "C" fn() -> usize,
    open: unsafe extern "C" fn(*mut c_void, u8) -> bool,
    extract: unsafe extern "C" fn(*mut c_void, u32, u32) -> u8,
    close_tmp: unsafe extern "C" fn(*mut c_void),
    resize: unsafe extern "C" fn(*mut c_void, usize, bool) -> *mut u8,
    decoded: unsafe extern "C" fn(*mut c_void, usize) -> *mut u8,
    commit_decoded: unsafe extern "C" fn(*mut c_void),
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
        return XP_DICTIONARY_LOOKUP_ENTRY_CAP;
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
    let required = if io.inline_types {
        let source = slice::from_raw_parts_mut(out, required);
        match decode_inline(io, source, original, io.size > size) {
            Ok(Decoded::Compacted(length)) => {
                (io.resize)(io.context, length, false);
                length
            }
            Ok(Decoded::Buffered(length)) => {
                (io.commit_decoded)(io.context);
                length
            }
            Err(status) => {
                (io.resize)(io.context, original, false);
                return status;
            }
        }
    } else {
        required
    };
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

use core::ops::Range;
use xp_core::book_io::txt;

struct InlineFields {
    offset: usize,
    truncated: bool,
}

impl InlineFields {
    fn next(&mut self, source: &[u8]) -> Result<Option<(bool, Range<usize>)>, u8> {
        let invalid = XP_DICTIONARY_LOOKUP_READ_ERROR;
        while let Some(&kind) = source.get(self.offset) {
            self.offset += 1;
            let tail = &source[self.offset..];
            if kind.is_ascii_uppercase() {
                let size = u32::from_be_bytes(tail.first_chunk().copied().ok_or(invalid)?) as usize;
                if size > tail.len() - 4 {
                    return Err(invalid);
                }
                self.offset += 4 + size;
            } else if kind.is_ascii_lowercase() {
                let end = tail.iter().position(|&byte| byte == 0);
                if end.is_none() && !self.truncated {
                    return Err(invalid);
                }
                let start = self.offset;
                let len = end.unwrap_or(tail.len());
                self.offset += len + usize::from(end.is_some());
                if len != 0 {
                    return Ok(Some((matches!(kind, b'h' | b'x'), start..start + len)));
                }
            } else {
                return Err(invalid);
            }
        }
        Ok(None)
    }
}

enum Decoded {
    Compacted(usize),
    Buffered(usize),
}

unsafe fn decode_inline(
    io: &Definition,
    source: &mut [u8],
    start: usize,
    truncated: bool,
) -> Result<Decoded, u8> {
    const OPEN: &[u8] = b"<pre>";
    const CLOSE: &[u8] = b"</pre>";
    let limit = XP_DICTIONARY_MAX_DEFINITION_BYTES * if io.append { 1 } else { 6 };
    let mut length = start;
    let mut all_markup = true;
    let mut fields = InlineFields {
        offset: start,
        truncated,
    };
    while let Some((markup, range)) = fields.next(source)? {
        all_markup &= markup;
        length += if markup {
            range.len()
        } else {
            OPEN.len() + txt::escaped_size(&source[range]) + CLOSE.len()
        };
        if length > limit {
            return Err(if io.append {
                XP_DICTIONARY_LOOKUP_ENTRY_CAP
            } else {
                XP_DICTIONARY_LOOKUP_READ_ERROR
            });
        }
    }
    if length == start {
        return Err(XP_DICTIONARY_LOOKUP_NOT_FOUND);
    }
    fields.offset = start;
    let mut written = start;
    if all_markup {
        while let Some((_, range)) = fields.next(source)? {
            let count = range.len();
            source.copy_within(range, written);
            written += count;
        }
        return Ok(Decoded::Compacted(written));
    }
    if io.heap() < length + 1 + 8192 {
        io.log(XP_DICTIONARY_DEFINITION_EVENT_LOW_HEAP, length as u32, 0, 0);
        return Err(XP_DICTIONARY_LOOKUP_LOW_MEMORY);
    }
    let decoded = (io.decoded)(io.context, length);
    if decoded.is_null() {
        return Err(XP_DICTIONARY_LOOKUP_LOW_MEMORY);
    }
    let decoded = slice::from_raw_parts_mut(decoded, length);
    decoded[..start].copy_from_slice(&source[..start]);
    while let Some((markup, range)) = fields.next(source)? {
        let field = &source[range];
        if markup {
            decoded[written..written + field.len()].copy_from_slice(field);
            written += field.len();
        } else {
            decoded[written..written + OPEN.len()].copy_from_slice(OPEN);
            written += OPEN.len();
            for byte in field {
                let text = txt::escape(*byte).unwrap_or(slice::from_ref(byte));
                decoded[written..written + text.len()].copy_from_slice(text);
                written += text.len();
            }
            decoded[written..written + CLOSE.len()].copy_from_slice(CLOSE);
            written += CLOSE.len();
        }
    }
    Ok(Decoded::Buffered(length))
}
