use core::{ffi::c_void, slice};
use xp_core::{text, utf8};

// The text ABI borrows caller buffers for one call; it retains no pointers.
macro_rules! predicate {
    ($name:ident, $function:path) => {
        #[no_mangle]
        pub extern "C" fn $name(cp: u32) -> u8 {
            u8::from($function(cp))
        }
    };
}
predicate!(xp_text_no_break_before, text::no_break_before);
predicate!(xp_text_no_break_after, text::no_break_after);
predicate!(xp_text_cjk, text::cjk);
predicate!(xp_text_hangul, text::hangul);
predicate!(xp_text_combining, text::combining);
predicate!(xp_text_break_after_hyphen, text::break_after_hyphen);
predicate!(xp_text_explicit_hyphen, text::explicit);
#[no_mangle]
pub extern "C" fn xp_text_upper(cp: u32) -> u32 {
    text::small_caps_upper(cp)
}
#[no_mangle]
pub extern "C" fn xp_text_decomposed_base(cp: u32) -> u32 {
    text::decomposed_base(cp)
}
#[no_mangle]
pub extern "C" fn xp_text_boundary(left: u32, right: u32) -> u8 {
    u8::from(text::boundary(left, right))
}
#[no_mangle]
pub extern "C" fn xp_text_opportunity(left: u32, right: u32) -> u8 {
    u8::from(text::opportunity(left, right))
}

/// # Safety
/// data borrows len readable bytes (len <= isize::MAX), or null for zero len.
/// consumed is aligned writable storage disjoint from data.
#[no_mangle]
pub unsafe extern "C" fn xp_utf8_next(data: *const u8, len: usize, consumed: *mut usize) -> u32 {
    let bytes = if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    };
    let mut pos = 0;
    let cp = utf8::next(bytes, &mut pos).unwrap_or(0);
    consumed.write(pos);
    cp
}
/// # Safety
/// out points to four writable bytes; only the returned byte count is written.
#[no_mangle]
pub unsafe extern "C" fn xp_utf8_encode(cp: u32, out: *mut u8) -> usize {
    let mut bytes = [0; 4];
    let n = utf8::encode(cp, &mut bytes);
    core::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
    n
}
/// # Safety
/// data borrows len readable bytes (len <= isize::MAX), or null for zero len.
#[no_mangle]
pub unsafe extern "C" fn xp_utf8_safe_truncate(data: *const u8, len: usize) -> usize {
    utf8::safe_truncate(if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    })
}
/// # Safety
/// data borrows len readable bytes (len <= isize::MAX), or null for zero len.
#[no_mangle]
pub unsafe extern "C" fn xp_utf8_remove_last(data: *const u8, len: usize) -> usize {
    utf8::remove_last(if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    })
}
/// # Safety
/// data borrows len readable bytes (len <= isize::MAX), or null for zero len.
#[no_mangle]
pub unsafe extern "C" fn xp_utf8_may_compose(data: *const u8, len: usize) -> u8 {
    u8::from(utf8::may_compose(if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    }))
}
/// # Safety
/// data borrows len readable bytes (len <= isize::MAX). Callbacks and context
/// remain valid, never mutate input, retain emitted bytes, or unwind. Emitted
/// bytes are borrowed only for that callback. Null data is allowed for zero len.
#[no_mangle]
pub unsafe extern "C" fn xp_utf8_compose(
    data: *const u8,
    len: usize,
    emit: unsafe extern "C" fn(*mut c_void, *const u8, usize),
    absorbed: unsafe extern "C" fn(*mut c_void, u32) -> u8,
    context: *mut c_void,
) -> u8 {
    let bytes = if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    };
    u8::from(utf8::compose(
        bytes,
        &mut |s| emit(context, s.as_ptr(), s.len()),
        &mut |n| absorbed(context, n) != 0,
    ))
}
/// # Safety
/// data exclusively borrows len writable bytes (len <= isize::MAX), excluding
/// the terminator. Null is allowed only for zero len. Returns the new length.
#[no_mangle]
pub unsafe extern "C" fn xp_utf8_compose_in_place(data: *mut u8, len: usize) -> usize {
    utf8::compose_in_place(if len == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(data, len)
    })
}
/// # Safety
/// data borrows len readable bytes (len <= isize::MAX), or null for zero len.
/// out is disjoint aligned storage for capacity usize values, or null for zero
/// capacity. Returns required count; insufficient capacity leaves out untouched.
#[no_mangle]
pub unsafe extern "C" fn xp_text_offsets(
    data: *const u8,
    len: usize,
    out: *mut usize,
    capacity: usize,
) -> usize {
    let bytes = if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    };
    let mut index = 0;
    text::character_offsets(bytes, capacity, &mut |offset| {
        out.add(index).write(offset);
        index += 1;
    })
}
