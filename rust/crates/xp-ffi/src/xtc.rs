//! XTC ABI: all storage is borrowed for one call; no allocation or retained pointers.
use core::{ptr, slice};
use xp_core::xtc::{self, Chapter, Entry, Header};

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
/// Nonempty input is readable for len bytes. Output points to a writable Header,
/// disjoint from input. Pointers are borrowed for this call only.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_header(data: *const u8, len: usize, out: *mut Header) -> i32 {
    if out.is_null() {
        return xtc::READ_ERROR;
    }
    let Some(data) = bytes(data, len) else {
        return xtc::READ_ERROR;
    };
    match xtc::header(data) {
        Ok(header) => {
            ptr::write_unaligned(out, header);
            xtc::OK
        }
        Err(error) => error,
    }
}

/// # Safety
/// Nonempty input is readable for len bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_bit_depth(data: *const u8, len: usize) -> u8 {
    bytes(data, len).map_or(0, xtc::bit_depth)
}

/// # Safety
/// Header points to a readable initialized Header.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_table_valid(header: *const Header, file_size: u64) -> bool {
    !header.is_null() && xtc::table_valid(&*header, file_size)
}

/// # Safety
/// Header points to a readable initialized Header.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_has_chapters(header: *const Header) -> bool {
    !header.is_null() && xtc::has_chapters(&*header)
}

/// # Safety
/// Nonempty input is readable for len bytes; out is aligned writable Entry storage,
/// disjoint from input. No pointers are retained.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_entry(data: *const u8, len: usize, out: *mut Entry) -> i32 {
    if out.is_null() || !out.is_aligned() {
        return xtc::READ_ERROR;
    }
    let Some(data) = bytes(data, len) else {
        return xtc::READ_ERROR;
    };
    match xtc::entry(data) {
        Ok(entry) => {
            out.write(entry);
            xtc::OK
        }
        Err(error) => error,
    }
}

#[no_mangle]
pub extern "C" fn xp_xtc_bitmap_size(width: u16, height: u16, depth: u8) -> u32 {
    xtc::bitmap_size(width, height, depth)
}

/// # Safety
/// Input is readable for len bytes. size is an aligned writable u32, disjoint
/// from input. streaming selects the legacy streaming API's magic error code.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_page(
    data: *const u8,
    len: usize,
    depth: u8,
    streaming: bool,
    size: *mut u32,
) -> i32 {
    if size.is_null() || !size.is_aligned() {
        return xtc::READ_ERROR;
    }
    let Some(data) = bytes(data, len) else {
        return xtc::READ_ERROR;
    };
    match xtc::page(data, depth, streaming) {
        Ok(value) => {
            size.write(value);
            xtc::OK
        }
        Err(error) => error,
    }
}

/// # Safety
/// Nonempty input is readable for len bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_text_len(data: *const u8, len: usize) -> usize {
    bytes(data, len).map_or(0, xtc::text_len)
}

/// # Safety
/// Header is readable; input is readable for len bytes. offset and count are
/// aligned writable u64 objects, all disjoint. Pointers are not retained.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_chapter_span(
    header: *const Header,
    data: *const u8,
    len: usize,
    file_size: u64,
    offset: *mut u64,
    count: *mut u64,
) {
    if header.is_null()
        || offset.is_null()
        || !offset.is_aligned()
        || count.is_null()
        || !count.is_aligned()
    {
        return;
    }
    let Some(data) = bytes(data, len) else {
        return;
    };
    let (start, length) = xtc::chapter_span(&*header, data, file_size);
    offset.write(start);
    count.write(length);
}

/// # Safety
/// Input is readable for len bytes; out is aligned writable Chapter storage,
/// disjoint from input. The name is the first out.name_len input bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_chapter(
    data: *const u8,
    len: usize,
    page_count: u16,
    out: *mut Chapter,
) -> i32 {
    if out.is_null() || !out.is_aligned() {
        return xtc::READ_ERROR;
    }
    let Some(data) = bytes(data, len) else {
        return xtc::READ_ERROR;
    };
    match xtc::chapter(data, page_count) {
        Ok(value) => {
            out.write(value);
            xtc::OK
        }
        Err(error) => error,
    }
}

#[no_mangle]
pub extern "C" fn xp_xtc_metadata_offset(author: bool) -> u32 {
    xtc::metadata_offset(author)
}

#[no_mangle]
pub extern "C" fn xp_xtc_chapter_flag(flag: u8) -> bool {
    xtc::chapter_flag(flag)
}
