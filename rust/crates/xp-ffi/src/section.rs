//! Allocation-free section wire records; buffers and objects stay with C++.
#[cfg(feature = "section-test")]
use core::ptr;
#[cfg(feature = "section-test")]
use core::slice;
#[cfg(feature = "section-test")]
use xp_core::section::Kind;
use xp_core::section::{self};
#[cfg(feature = "section-test")]
use xp_core::section::{Error, Record};

#[cfg(feature = "section-test")]
unsafe fn bytes<'a>(data: *const u8, len: usize) -> Option<&'a [u8]> {
    if len > isize::MAX as usize || (data.is_null() && len != 0) {
        None
    } else if len == 0 {
        Some(&[])
    } else {
        Some(slice::from_raw_parts(data, len))
    }
}

#[cfg(feature = "section-test")]
#[repr(C)]
pub struct Decoder {
    input: *const u8,
    len: usize,
    position: usize,
    kind: u32,
}

#[cfg(feature = "section-test")]
#[no_mangle]
pub extern "C" fn xp_sec_record_size(kind: u32) -> usize {
    Kind::from_raw(kind).map_or(0, Kind::size)
}

/// # Safety
/// Record and required point to valid nonoverlapping objects. Output has
/// capacity writable bytes and does not alias either object.
#[cfg(feature = "section-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_sec_encode_record(
    record: *const Record,
    output: *mut u8,
    capacity: usize,
    required: *mut usize,
) -> u32 {
    if record.is_null()
        || required.is_null()
        || capacity > isize::MAX as usize
        || (output.is_null() && capacity != 0)
    {
        return 3;
    }
    let output = if capacity == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(output, capacity)
    };
    match section::encode(&*record, output) {
        Ok(size) => {
            *required = size;
            0
        }
        Err(Error::NeedOutput(size)) => {
            *required = size;
            2
        }
        Err(_) => {
            *required = 0;
            3
        }
    }
}

/// # Safety
/// Decoder and record are writable, disjoint objects. Decoder's input points
/// to len immutable bytes in one live allocation and does not alias them.
#[cfg(feature = "section-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_sec_decode_next(decoder: *mut Decoder, record: *mut Record) -> u32 {
    if decoder.is_null() || record.is_null() {
        return 3;
    }
    let decoder = &mut *decoder;
    let Some(kind) = Kind::from_raw(decoder.kind) else {
        return 3;
    };
    let Some(input) = bytes(decoder.input, decoder.len) else {
        return 3;
    };
    let Some(input) = input.get(decoder.position..) else {
        return 3;
    };
    match section::decode(kind, input) {
        Ok((value, consumed)) => {
            ptr::write(record, value);
            decoder.position += consumed;
            0
        }
        Err(Error::NeedInput(_)) => 1,
        Err(_) => 3,
    }
}

#[no_mangle]
pub extern "C" fn xp_sec_arena_size(words: u16, focus: u8, text_bytes: u16) -> usize {
    section::arena_size(words, focus != 0, text_bytes)
}

/// # Safety
/// Result points to one writable u32.
#[cfg(feature = "section-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_sec_table_entry(
    offset: u32,
    index: u32,
    width: u32,
    size: u32,
    minimum: u32,
    result: *mut u32,
) -> u8 {
    if result.is_null() {
        return 0;
    }
    if let Some(position) = section::table_entry(offset, index, width, size, minimum) {
        *result = position;
        1
    } else {
        0
    }
}
