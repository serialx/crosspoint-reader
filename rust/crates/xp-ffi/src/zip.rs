use core::slice;
use xp_core::zip::{self, Details, Entry, ReadAt, Step, Walker};

unsafe fn input<'a>(data: *const u8, len: usize) -> Option<&'a [u8]> {
    if len > isize::MAX as usize || (len != 0 && data.is_null()) {
        return None;
    }
    Some(if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    })
}

fn writable<T>(ptr: *mut T) -> bool {
    !ptr.is_null() && ptr.is_aligned()
}

/// # Safety
/// out points to one aligned writable ReadAt. No pointer is retained.
#[no_mangle]
pub unsafe extern "C" fn xp_zip_eocd_request(size: u32, out: *mut ReadAt) -> i32 {
    if !writable(out) {
        return -1;
    }
    let Some(request) = zip::eocd_request(size) else {
        return -1;
    };
    out.write(request);
    0
}

/// # Safety
/// data borrows len readable bytes; out is aligned writable and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_zip_eocd(data: *const u8, len: usize, out: *mut Details) -> i32 {
    if !writable(out) {
        return -1;
    }
    let Some(bytes) = input(data, len) else {
        return -1;
    };
    let Some(details) = zip::eocd(bytes) else {
        return -1;
    };
    out.write(details);
    0
}

/// # Safety
/// out points to one aligned writable Walker, exclusively owned by the caller.
#[no_mangle]
pub unsafe extern "C" fn xp_zip_begin(central: u32, start: u32, out: *mut Walker) -> i32 {
    if !writable(out) {
        return -1;
    }
    out.write(Walker::new(central, start));
    0
}

/// # Safety
/// walker is initialized by xp_zip_begin and unchanged except through this API.
/// out is aligned writable and disjoint from walker.
#[no_mangle]
pub unsafe extern "C" fn xp_zip_request(walker: *const Walker, out: *mut ReadAt) -> i32 {
    if walker.is_null() || !walker.is_aligned() || !writable(out) {
        return -1;
    }
    let Some(request) = (*walker).request() else {
        return 0;
    };
    out.write(request);
    1
}

/// # Safety
/// walker is initialized, aligned and exclusively borrowed. data borrows len
/// readable bytes (NULL only for zero len). out is aligned writable. All regions
/// are disjoint; no input pointer is retained. Short replies end or wrap the scan.
#[no_mangle]
pub unsafe extern "C" fn xp_zip_next(
    walker: *mut Walker,
    data: *const u8,
    len: usize,
    out: *mut Entry,
) -> i32 {
    if !writable(walker) || !writable(out) {
        return -1;
    }
    let Some(bytes) = input(data, len) else {
        return -1;
    };
    match (*walker).advance(bytes) {
        Some(Step::Entry(entry)) => {
            out.write(entry);
            1
        }
        Some(Step::Read(_)) => 2,
        Some(Step::Done) => 0,
        None => -1,
    }
}

/// # Safety
/// out points to one aligned writable ReadAt.
#[no_mangle]
pub unsafe extern "C" fn xp_zip_local_request(offset: u32, out: *mut ReadAt) -> i32 {
    if !writable(out) {
        return -1;
    }
    out.write(zip::local_request(offset));
    0
}

/// # Safety
/// data borrows len readable bytes; out is aligned writable and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_zip_data_offset(
    offset: u32,
    data: *const u8,
    len: usize,
    out: *mut u64,
) -> i32 {
    if !writable(out) {
        return -1;
    }
    let Some(bytes) = input(data, len) else {
        return -1;
    };
    let Some(value) = zip::data_offset(offset, bytes) else {
        return -1;
    };
    out.write(value);
    0
}

/// # Safety
/// Each input borrows its length in readable unchanged bytes; NULL only at zero
/// length. Neither pointer is retained. Names are compared as raw bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_zip_name_matches(
    name: *const u8,
    len: usize,
    target: *const u8,
    target_len: usize,
) -> i32 {
    let (Some(name), Some(target)) = (input(name, len), input(target, target_len)) else {
        return -1;
    };
    i32::from(zip::name_matches(name, target))
}
