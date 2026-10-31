use core::{
    ffi::c_void,
    mem::{align_of, size_of},
    slice,
};
use xp_core::inflate::{InflateState, SmallInflateState};

const BAD_PARAM: i32 = -3;

#[no_mangle]
pub extern "C" fn xp_inflate_state_size() -> usize {
    size_of::<InflateState>()
}

#[no_mangle]
pub extern "C" fn xp_inflate_state_align() -> usize {
    align_of::<InflateState>()
}

/// # Safety
/// State must point to state_bytes writable bytes with the queried alignment.
/// It must be exclusively owned until all inflate calls finish.
#[no_mangle]
pub unsafe extern "C" fn xp_inflate_init(
    state: *mut c_void,
    state_bytes: usize,
    zlib_wrapped: u8,
) -> i32 {
    if state.is_null()
        || state_bytes < size_of::<InflateState>()
        || !(state as usize).is_multiple_of(align_of::<InflateState>())
        || zlib_wrapped > 1
    {
        return BAD_PARAM;
    }
    InflateState::init_in_place(state.cast(), zlib_wrapped != 0);
    0
}

/// # Safety
/// State must have been initialized and remain exclusively owned. Input and
/// output must be disjoint initialized allocations (lengths <= isize::MAX), and may
/// be null only for zero length. Output history must persist between calls.
/// Counter pointers must be writable, aligned, and disjoint from all buffers
/// and each other. No pointers are retained.
#[no_mangle]
pub unsafe extern "C" fn xp_inflate_step(
    state: *mut c_void,
    input: *const u8,
    in_len: usize,
    more_input: u8,
    output: *mut u8,
    out_len: usize,
    out_pos: *mut usize,
    wrapping_window: u8,
    consumed: *mut usize,
    produced: *mut usize,
) -> i32 {
    if state.is_null()
        || !(state as usize).is_multiple_of(align_of::<InflateState>())
        || out_pos.is_null()
        || consumed.is_null()
        || produced.is_null()
    {
        return BAD_PARAM;
    }
    *consumed = 0;
    *produced = 0;
    if (input.is_null() && in_len != 0)
        || (output.is_null() && out_len != 0)
        || in_len > isize::MAX as usize
        || out_len > isize::MAX as usize
        || *out_pos > out_len
        || more_input > 1
        || wrapping_window > 1
    {
        return BAD_PARAM;
    }
    let input = if in_len == 0 {
        &[]
    } else {
        slice::from_raw_parts(input, in_len)
    };
    let output = if out_len == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(output, out_len)
    };
    let (status, used, written) = (&mut *state.cast::<InflateState>()).step(
        input,
        more_input != 0,
        output,
        *out_pos,
        wrapping_window != 0,
    );
    *consumed = used;
    *produced = written;
    *out_pos += written;
    status
}

#[no_mangle]
pub extern "C" fn xp_inflate_small_state_size() -> usize {
    size_of::<SmallInflateState>()
}

#[no_mangle]
pub extern "C" fn xp_inflate_small_state_align() -> usize {
    align_of::<SmallInflateState>()
}

/// # Safety
/// State must point to state_bytes writable bytes with the queried alignment.
/// It must be exclusively owned until all inflate calls finish.
#[no_mangle]
pub unsafe extern "C" fn xp_inflate_small_init(
    state: *mut c_void,
    state_bytes: usize,
    zlib_wrapped: u8,
) -> i32 {
    if state.is_null()
        || state_bytes < size_of::<SmallInflateState>()
        || !(state as usize).is_multiple_of(align_of::<SmallInflateState>())
        || zlib_wrapped > 1
    {
        return BAD_PARAM;
    }
    SmallInflateState::init_in_place(state.cast(), zlib_wrapped != 0);
    0
}

/// # Safety
/// State must have been initialized and remain exclusively owned. Input and
/// output must be disjoint initialized allocations (lengths <= isize::MAX), and may
/// be null only for zero length. Output history must persist between calls.
/// Counter pointers must be writable, aligned, and disjoint from all buffers
/// and each other. No pointers are retained.
#[no_mangle]
pub unsafe extern "C" fn xp_inflate_small_step(
    state: *mut c_void,
    input: *const u8,
    in_len: usize,
    more_input: u8,
    output: *mut u8,
    out_len: usize,
    out_pos: *mut usize,
    wrapping_window: u8,
    consumed: *mut usize,
    produced: *mut usize,
    out_max: usize,
) -> i32 {
    if state.is_null()
        || !(state as usize).is_multiple_of(align_of::<SmallInflateState>())
        || out_pos.is_null()
        || consumed.is_null()
        || produced.is_null()
    {
        return BAD_PARAM;
    }
    *consumed = 0;
    *produced = 0;
    if (input.is_null() && in_len != 0)
        || (output.is_null() && out_len != 0)
        || in_len > isize::MAX as usize
        || out_len > isize::MAX as usize
        || *out_pos > out_len
        || more_input > 1
        || wrapping_window > 1
    {
        return BAD_PARAM;
    }
    let input = if in_len == 0 {
        &[]
    } else {
        slice::from_raw_parts(input, in_len)
    };
    let output = if out_len == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(output, out_len)
    };
    let (status, used, written) = (&mut *state.cast::<SmallInflateState>()).step_with_limit(
        input,
        more_input != 0,
        output,
        *out_pos,
        wrapping_window != 0,
        out_max,
    );
    *consumed = used;
    *produced = written;
    *out_pos += written;
    status
}
