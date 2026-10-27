use core::{ffi::c_void, mem, slice};
use xp_core::bidi::{self, Status, Workspace};

fn valid<T>(ptr: *const T, len: usize) -> bool {
    len == 0
        || (!ptr.is_null()
            && (ptr as usize).is_multiple_of(mem::align_of::<T>())
            && len <= isize::MAX as usize / mem::size_of::<T>())
}
unsafe fn bytes<'a>(data: *const u8, len: usize) -> &'a [u8] {
    if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    }
}
fn valid_workspace(work: *mut c_void, size: usize) -> bool {
    !work.is_null()
        && (work as usize).is_multiple_of(mem::align_of::<Workspace>())
        && size >= mem::size_of::<Workspace>()
}
#[no_mangle]
pub extern "C" fn xp_bidi_workspace_size() -> usize {
    mem::size_of::<Workspace>()
}
#[no_mangle]
pub extern "C" fn xp_bidi_workspace_align() -> usize {
    mem::align_of::<Workspace>()
}
/// # Safety
/// work is exclusive storage of size bytes. No references survive this call.
#[no_mangle]
pub unsafe extern "C" fn xp_bidi_workspace_init(work: *mut c_void, size: usize) -> i32 {
    if !valid_workspace(work, size) {
        return Status::InvalidArgument as i32;
    }
    work.cast::<u8>()
        .write_bytes(0, mem::size_of::<Workspace>());
    Status::Applied as i32
}
#[no_mangle]
pub extern "C" fn xp_bidi_class(cp: u32) -> u8 {
    bidi::class(cp)
}
#[no_mangle]
pub extern "C" fn xp_bidi_mirror(cp: u32) -> u32 {
    bidi::mirror(cp)
}
#[no_mangle]
pub extern "C" fn xp_bidi_transparent_mark(cp: u32) -> u8 {
    u8::from(bidi::is_transparent_mark(cp))
}
/// # Safety
/// data borrows len readable bytes, or null for zero length.
#[no_mangle]
pub unsafe extern "C" fn xp_bidi_starts_rtl(data: *const u8, len: usize, depth: i32) -> u8 {
    if !valid(data, len) {
        return 0;
    }
    u8::from(bidi::starts_with_rtl(bytes(data, len), depth))
}
/// # Safety
/// data borrows len readable bytes, or null for zero length.
#[no_mangle]
pub unsafe extern "C" fn xp_bidi_paragraph_level(
    data: *const u8,
    len: usize,
    fallback: i32,
    depth: i32,
) -> i32 {
    if !valid(data, len) {
        return fallback & 1;
    }
    bidi::detect_paragraph_level(bytes(data, len), fallback, depth)
}
/// # Safety
/// All spans are valid, disjoint allocations, borrowed only for this call.
/// work was initialized by xp_bidi_workspace_init and is exclusively borrowed.
/// out and written are writable; written is aligned. Null spans require zero length.
#[no_mangle]
pub unsafe extern "C" fn xp_bidi_apply_visual(
    data: *const u8,
    len: usize,
    paragraph: i32,
    work: *mut c_void,
    work_size: usize,
    out: *mut u8,
    capacity: usize,
    written: *mut usize,
) -> i32 {
    if written.is_null() || !valid(written, 1) {
        return Status::InvalidArgument as i32;
    }
    written.write(0);
    if !valid(data, len) || !valid(out, capacity) || !valid_workspace(work, work_size) {
        return Status::InvalidArgument as i32;
    }
    let output = if capacity == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(out, capacity)
    };
    let (status, n) = bidi::apply_visual(bytes(data, len), paragraph, &mut *work.cast(), output);
    written.write(n);
    status as i32
}
/// # Safety
/// All spans are valid, aligned and disjoint, borrowed only for this call.
/// work was initialized and is exclusive. out holds capacity writable u16 values.
#[no_mangle]
pub unsafe extern "C" fn xp_bidi_word_order(
    data: *const u8,
    len: usize,
    offsets: *const u16,
    count: usize,
    rtl: u8,
    work: *mut c_void,
    work_size: usize,
    out: *mut u16,
    capacity: usize,
) -> i32 {
    if !valid(data, len)
        || !valid(offsets, count)
        || !valid(out, capacity)
        || !valid_workspace(work, work_size)
    {
        return Status::InvalidArgument as i32;
    }
    if capacity != count {
        return Status::BufferTooSmall as i32;
    }
    let offsets = if count == 0 {
        &[]
    } else {
        slice::from_raw_parts(offsets, count)
    };
    let output = if capacity == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(out, capacity)
    };
    bidi::visual_word_order(
        bytes(data, len),
        offsets,
        rtl != 0,
        &mut *work.cast(),
        output,
    ) as i32
}

/// # Safety
/// work is initialized exclusive storage. Callback spans remain live for this call;
/// callbacks cannot reenter bidi. The consumer cannot retain the visual pointer.
#[no_mangle]
pub unsafe extern "C" fn xp_bidi_visual_visit(
    data: *const u8,
    len: usize,
    direction: i32,
    work: *mut c_void,
    context: *mut c_void,
    visit: unsafe extern "C" fn(*mut c_void, *const u8, usize),
) -> u8 {
    if let Some(text) = bidi::visual_including_empty(bytes(data, len), direction, &mut *work.cast())
    {
        visit(context, text.as_ptr(), text.len());
        1
    } else {
        0
    }
}
/// # Safety
/// Same workspace and callback contract as xp_bidi_visual_visit.
#[no_mangle]
pub unsafe extern "C" fn xp_bidi_rtl_tokens(
    data: *const u8,
    len: usize,
    work: *mut c_void,
    context: *mut c_void,
    visit: unsafe extern "C" fn(*mut c_void, *const u8, usize),
) {
    for token in bytes(data, len).split(|b| matches!(b, b' ' | b'\n' | b'\r' | b'\t')) {
        if token.iter().any(|b| (0xd6..=0xdb).contains(b)) {
            if let Some(text) = bidi::visual(token, -1, &mut *work.cast()) {
                visit(context, text.as_ptr(), text.len());
            }
        }
    }
}
/// # Safety
/// work is initialized and exclusive, output holds count u16 values. Word callback
/// supplies readable spans valid for the call and cannot reenter bidi.
#[no_mangle]
pub unsafe extern "C" fn xp_bidi_words(
    count: usize,
    context: *const c_void,
    word: unsafe extern "C" fn(*const c_void, usize, *mut usize) -> *const u8,
    rtl: u8,
    work: *mut c_void,
    output: *mut u16,
) -> i32 {
    if count <= 1 || count > bidi::MAX_LINE {
        return Status::Unchanged as i32;
    }
    bidi::visual_words(
        count,
        |i| {
            let mut len = 0;
            let data = word(context, i, &mut len);
            bytes(data, len)
        },
        rtl != 0,
        &mut *work.cast(),
        slice::from_raw_parts_mut(output, count),
    ) as i32
}
