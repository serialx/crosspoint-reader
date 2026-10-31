//! Book adapter ABI. All callback buffers are borrowed for one call.
use core::{ffi::c_void, slice};
use xp_core::book_io::txt;

unsafe fn bytes<'a>(data: *const u8, len: usize) -> Option<&'a [u8]> {
    if len > isize::MAX as usize || (len != 0 && data.is_null()) {
        return None;
    }
    Some(if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    })
}

/// # Safety
/// Nonempty text is readable for len bytes and unchanged during the call.
#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_escaped_size(text: *const u8, len: usize) -> usize {
    bytes(text, len).map_or(0, txt::escaped_size)
}

/// # Safety
/// Nonempty title and tag are readable for their lengths. Input and output each
/// point to 8192 initialized writable bytes, disjoint from all other buffers.
/// Callbacks obey the same contract as txt::stream; no pointers are retained.
#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_stream(
    title: *const u8,
    title_len: usize,
    tag: *const u8,
    tag_len: usize,
    input: *mut u8,
    output: *mut u8,
    context: *mut c_void,
    read: Option<txt::Read>,
    write: Option<txt::Write>,
) -> i32 {
    let (Some(title), Some(tag), Some(read), Some(write)) =
        (bytes(title, title_len), bytes(tag, tag_len), read, write)
    else {
        return -1;
    };
    if input.is_null() || output.is_null() {
        return -1;
    }
    txt::stream(
        title,
        tag,
        &mut *input.cast(),
        &mut *output.cast(),
        context,
        read,
        write,
    )
}

/// # Safety
/// Text is readable for len bytes. Emit borrows each fragment for its call only,
/// does not modify text or reenter, and context remains valid for the whole call.
#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_escape(
    text: *const u8,
    len: usize,
    context: *mut c_void,
    emit: Option<unsafe extern "C" fn(*mut c_void, *const u8, usize)>,
) {
    let (Some(text), Some(emit)) = (bytes(text, len), emit) else {
        return;
    };
    for byte in text {
        let fragment = txt::escape(*byte).unwrap_or_else(|| slice::from_ref(byte));
        emit(context, fragment.as_ptr(), fragment.len());
    }
}

use xp_core::book_io::xtc_image::{self, Thumbnail};

/// # Safety
/// Out points to writable Thumbnail storage.
#[no_mangle]
pub unsafe extern "C" fn xp_book_xtc_thumbnail(
    width: u16,
    height: u16,
    target: i32,
    out: *mut Thumbnail,
) {
    if !out.is_null() {
        out.write(xtc_image::thumbnail(width, height, target));
    }
}

/// # Safety
/// Pixels is readable for len bytes; row is initialized writable row_len bytes.
/// Both are disjoint and callbacks obey the TXT write contract. Plan, when nonnull,
/// is a readable thumbnail plan. Its dimensions and scale came from thumbnail().
#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub unsafe extern "C" fn xp_book_xtc_image(
    pixels: *const u8,
    len: usize,
    width: u16,
    height: u16,
    depth: u8,
    row: *mut u8,
    row_len: usize,
    plan: *const Thumbnail,
    context: *mut c_void,
    write: Option<txt::Write>,
    yield_now: Option<xtc_image::Yield>,
) {
    let (Some(pixels), Some(write)) = (bytes(pixels, len), write) else {
        return;
    };
    if len < xp_core::xtc::bitmap_size(width, height, depth) as usize
        || row_len > isize::MAX as usize
        || (row_len != 0 && row.is_null())
    {
        return;
    }
    let row = if row_len == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(row, row_len)
    };
    if let Some(plan) = plan.as_ref() {
        let Some(yield_now) = yield_now else {
            return;
        };
        if width == 0 || height == 0 || row_len != usize::from(plan.width).div_ceil(32) * 4 {
            return;
        }
        xtc_image::thumb(
            pixels, width, height, depth, *plan, row, context, write, yield_now,
        );
    } else {
        if depth == 2 && row_len != (usize::from(width) * 2).div_ceil(32) * 4 {
            return;
        }
        xtc_image::cover(pixels, width, height, depth, row, context, write);
    }
}

use xp_core::book_io::key;
/// # Safety
/// Key is readable for len bytes; services is an initialized callback table and
/// obeys key::write's contract. Status 2 means crypto failed; all others except 0
/// are silent failures or errors already diagnosed by the storage leaf.
#[no_mangle]
pub unsafe extern "C" fn xp_book_key_write(
    key: *const u8,
    len: usize,
    expires: i64,
    services: *const key::Services,
) -> i32 {
    if !matches!(len, 16 | 32) {
        return 1;
    }
    let (Some(key), Some(services)) = (bytes(key, len), services.as_ref()) else {
        return 1;
    };
    key::write(key, expires, services)
}
/// # Safety
/// Key points to 32 writable bytes; expiry is writable and disjoint.
/// Services obey key::read's contract; result -1 means authentication failed.
#[no_mangle]
pub unsafe extern "C" fn xp_book_key_read(
    key: *mut u8,
    expires: *mut i64,
    services: *const key::Services,
) -> i32 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    if key.is_null() || expires.is_null() {
        return 0;
    }
    key::read(key, expires, services)
}

use xp_core::book_io::xtc_reader::{
    self, Services as XtcServices, State as XtcState, Stream as XtcStream,
};
/// # Safety
/// State and IO are valid, aligned initialized objects. Callbacks may inspect the
/// Cell-backed state for diagnostics but must not mutate it or reenter the reader.
#[no_mangle]
pub unsafe extern "C" fn xp_book_xtc_open(state: *const XtcState, io: *const XtcServices) -> i32 {
    let (Some(state), Some(io)) = (state.as_ref(), io.as_ref()) else {
        return 6;
    };
    xtc_reader::open(state, io)
}
/// # Safety
/// Same contract as xp_book_xtc_open.
#[no_mangle]
pub unsafe extern "C" fn xp_book_xtc_close(state: *const XtcState, io: *const XtcServices) {
    if let (Some(state), Some(io)) = (state.as_ref(), io.as_ref()) {
        xtc_reader::close(state, io);
    }
}
/// # Safety
/// Same contract as xp_book_xtc_open.
#[no_mangle]
pub unsafe extern "C" fn xp_book_xtc_chapters(state: *const XtcState, io: *const XtcServices) {
    if let (Some(state), Some(io)) = (state.as_ref(), io.as_ref()) {
        xtc_reader::chapters(state, io);
    }
}
/// # Safety
/// Same contract as xp_book_xtc_open; out is writable Entry storage and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_book_xtc_page_info(
    state: *const XtcState,
    io: *const XtcServices,
    index: u32,
    out: *mut xp_core::xtc::Entry,
) -> bool {
    let (Some(state), Some(io)) = (state.as_ref(), io.as_ref()) else {
        return false;
    };
    if out.is_null() {
        return false;
    }
    let mut entry = xp_core::xtc::Entry::default();
    if !xtc_reader::page_info(state, io, index, &mut entry) {
        return false;
    }
    out.write(entry);
    true
}
/// # Safety
/// Same contract as xp_book_xtc_open; output is writable capacity-byte storage,
/// disjoint from state and IO. Bytes need not be initialized before the HAL read.
#[no_mangle]
pub unsafe extern "C" fn xp_book_xtc_load(
    state: *const XtcState,
    io: *const XtcServices,
    index: u32,
    output: *mut u8,
    capacity: usize,
) -> usize {
    let (Some(state), Some(io)) = (state.as_ref(), io.as_ref()) else {
        return 0;
    };
    if output.is_null() && capacity != 0 {
        return 0;
    }
    xtc_reader::load(state, io, index, output, capacity)
}
/// # Safety
/// Same contract as xp_book_xtc_open; sink obeys xtc_reader::stream's contract.
#[no_mangle]
pub unsafe extern "C" fn xp_book_xtc_stream(
    state: *const XtcState,
    io: *const XtcServices,
    index: u32,
    chunk_size: usize,
    sink: *const XtcStream,
) -> i32 {
    let (Some(state), Some(io), Some(sink)) = (state.as_ref(), io.as_ref(), sink.as_ref()) else {
        return 6;
    };
    xtc_reader::stream(state, io, index, chunk_size, sink)
}

use xp_core::book_io::zip_read;
/// # Safety
/// Services obey zip_read::memory's callback contract; optional size is writable.
#[no_mangle]
pub unsafe extern "C" fn xp_book_zip_memory(
    io: *const zip_read::Services,
    method: u16,
    compressed: u32,
    inflated: u32,
    trailing_nul: bool,
    size: *mut usize,
) -> *mut u8 {
    let Some(io) = io.as_ref() else {
        return core::ptr::null_mut();
    };
    zip_read::memory(io, method, compressed, inflated, trailing_nul, size)
}
/// # Safety
/// Services obey zip_read::stream's callback contract.
#[no_mangle]
pub unsafe extern "C" fn xp_book_zip_stream(
    io: *const zip_read::Services,
    method: u16,
    compressed: u32,
    inflated: u32,
    chunk: usize,
    early_stop: bool,
) -> bool {
    let Some(io) = io.as_ref() else {
        return false;
    };
    zip_read::stream(io, method, compressed, inflated, chunk, early_stop)
}

/// # Safety
/// Buffer is writable for capacity bytes. Callbacks borrow it only for their
/// calls and never write/read beyond their requested counts; context remains valid.
#[no_mangle]
pub unsafe extern "C" fn xp_book_copy_available(
    buffer: *mut u8,
    capacity: usize,
    context: *mut c_void,
    available: Option<unsafe extern "C" fn(*mut c_void) -> bool>,
    read: Option<txt::Read>,
    write: Option<txt::Write>,
) -> bool {
    let (Some(available), Some(read), Some(write)) = (available, read, write) else {
        return false;
    };
    if buffer.is_null() || capacity == 0 {
        return false;
    }
    while available(context) {
        let count = read(context, buffer, capacity);
        if count < 0 || count as usize > capacity {
            return false;
        }
        write(context, buffer, count as usize);
    }
    true
}

/// # Safety
/// Control obeys txt::build_cache's contract and context is valid through return.
#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_build(
    context: *mut c_void,
    control: Option<txt::CacheControl>,
) -> u32 {
    control.map_or(3, |control| txt::build_cache(context, control))
}
/// # Safety
/// Candidate obeys txt::find_cover's contract.
#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_find_cover(
    context: *mut c_void,
    candidate: Option<unsafe extern "C" fn(*mut c_void, *const u8, *const u8) -> bool>,
) {
    if let Some(candidate) = candidate {
        txt::find_cover(context, candidate);
    }
}

#[no_mangle]
pub extern "C" fn xp_book_ascii_space(byte: u8) -> bool {
    matches!(byte, b' ' | b'\t' | b'\n' | b'\r' | b'\x0c')
}
/// # Safety
/// Data borrows len bytes; start and end are disjoint writable size_t objects.
#[no_mangle]
pub unsafe extern "C" fn xp_book_ascii_trim(
    data: *const u8,
    len: usize,
    start: *mut usize,
    end: *mut usize,
) {
    let Some(data) = bytes(data, len) else {
        return;
    };
    let mut a = 0;
    let mut b = len;
    while a < b && xp_book_ascii_space(data[a]) {
        a += 1;
    }
    while b > a && xp_book_ascii_space(data[b - 1]) {
        b -= 1;
    }
    start.write(a);
    end.write(b);
}
/// # Safety
/// Name is null or a readable NUL-terminated string; the returned pointer aliases it.
#[no_mangle]
pub unsafe extern "C" fn xp_book_xml_local(name: *const u8) -> *const u8 {
    if name.is_null() {
        return c"".as_ptr().cast();
    }
    let text = core::ffi::CStr::from_ptr(name.cast()).to_bytes();
    name.add(text.iter().position(|b| *b == b':').map_or(0, |i| i + 1))
}
/// # Safety
/// Data is readable through the first invalid continuation, or the complete
/// character (at most four bytes). Consumed is disjoint writable size_t storage.
#[no_mangle]
pub unsafe extern "C" fn xp_book_utf8_next(data: *const u8, consumed: *mut usize) -> u32 {
    let lead = *data;
    let expected = match lead {
        0xc0..=0xdf => 2,
        0xe0..=0xef => 3,
        0xf0..=0xf7 => 4,
        _ => 1,
    };
    let mut available = 1;
    while available < expected {
        let byte = *data.add(available);
        available += 1;
        if byte & 0xc0 != 0x80 {
            break;
        }
    }
    let mut position = 0;
    let cp =
        xp_core::utf8::next(slice::from_raw_parts(data, available), &mut position).unwrap_or(0);
    consumed.write(position);
    cp
}
/// # Safety
/// Clock borrows context and returns the trusted monotonic time; it is called
/// only for records carrying an expiry, preserving the original gate order.
#[no_mangle]
pub unsafe extern "C" fn xp_book_loan(
    expires: i64,
    context: *mut c_void,
    clock: unsafe extern "C" fn(*mut c_void) -> i64,
) -> u32 {
    if expires == 0 {
        return 0;
    }
    let now = clock(context);
    if now == 0 {
        1
    } else if now > expires {
        2
    } else {
        0
    }
}
/// # Safety
/// IO obeys txt::validate's contract; tag borrows len readable bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_validate(
    io: *const txt::FileServices,
    cached: usize,
    tag: *const u8,
    len: usize,
) -> bool {
    let (Some(io), Some(tag)) = (io.as_ref(), bytes(tag, len)) else {
        return false;
    };
    txt::validate(io, cached, tag)
}
/// # Safety
/// IO obeys txt::convert's contract.
#[no_mangle]
pub unsafe extern "C" fn xp_book_txt_convert(
    io: *const txt::FileServices,
    kind: u32,
    height: i32,
) -> bool {
    io.as_ref().is_some_and(|io| txt::convert(io, kind, height))
}
