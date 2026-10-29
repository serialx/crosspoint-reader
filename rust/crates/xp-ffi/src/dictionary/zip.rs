use super::{abi::*, File};
use core::{ffi::c_void, slice};
const READ: u8 = XP_DICTIONARY_ZIP_READ_ERROR;
const BAD: u8 = XP_DICTIONARY_ZIP_DECOMPRESS;
#[repr(C)]
pub struct Info {
    data_offset: u32,
    total_size: u32,
    chunk_length: u16,
    valid: bool,
}
#[repr(C)]
pub struct Table {
    context: *mut c_void,
    reserve: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    push: unsafe extern "C" fn(*mut c_void, u32),
}
impl Table {
    fn reserve(&self, n: usize) -> bool {
        unsafe { (self.reserve)(self.context, n) }
    }
    fn push(&self, n: u32) {
        unsafe { (self.push)(self.context, n) }
    }
}
fn parse(file: &File, info: &mut Info, table: &Table) -> Result<(), u8> {
    let mut header = [0; 10];
    if file.read(&mut header) != 10 {
        return Err(READ);
    }
    if header[..3] != [0x1f, 0x8b, 8] || header[3] & 4 == 0 {
        return Err(BAD);
    }
    let flags = header[3];
    let xlen = u32::from(file.le16().ok_or(READ)?);
    let mut extra = 0;
    let mut found = false;
    while extra + 4 <= xlen {
        let mut sub = [0; 4];
        if file.read(&mut sub) != 4 {
            return Err(READ);
        }
        extra += 4;
        let len = u32::from(u16::from_le_bytes([sub[2], sub[3]]));
        if extra + len > xlen {
            return Err(BAD);
        }
        if sub[..2] == *b"RA" {
            if found || len < 6 {
                return Err(BAD);
            }
            let version = file.le16().ok_or(READ)?;
            let chunk = file.le16().ok_or(READ)?;
            let count = file.le16().ok_or(READ)?;
            extra += 6;
            if version != 1
                || chunk == 0
                || count == 0
                || count > 8192
                || len != 6 + u32::from(count) * 2
            {
                return Err(BAD);
            }
            info.chunk_length = chunk;
            if !table.reserve(usize::from(count) + 1) {
                return Err(XP_DICTIONARY_ZIP_LOW_MEMORY);
            }
            table.push(0);
            let mut cumulative = 0;
            for _ in 0..count {
                cumulative += u32::from(file.le16().ok_or(READ)?);
                extra += 2;
                table.push(cumulative);
            }
            found = true;
        } else {
            if !file.seek(file.tell().wrapping_add(len as usize)) {
                return Err(READ);
            }
            extra += len;
        }
    }
    if extra != xlen || !found {
        return Err(BAD);
    }
    for mask in [8, 16] {
        if flags & mask != 0 {
            while file.byte().ok_or(READ)? != 0 {}
        }
    }
    if flags & 2 != 0 {
        let mut crc = [0; 2];
        if file.read(&mut crc) != 2 {
            return Err(READ);
        }
    }
    info.data_offset = file.tell() as u32;
    let size = file.size() as u32;
    if size < 4 {
        return Err(BAD);
    }
    if !file.seek((size - 4) as usize) {
        return Err(READ);
    }
    let mut raw = [0; 4];
    if file.read(&mut raw) != 4 {
        return Err(READ);
    }
    info.total_size = u32::from_le_bytes(raw);
    if info.total_size == 0 {
        return Err(BAD);
    }
    info.valid = true;
    Ok(())
}
/// # Safety
/// Descriptors and writable info are valid for this call. Table callbacks retain only copied values.
#[no_mangle]
pub unsafe extern "C" fn xp_dictzip_parse(
    file: *const File,
    info: *mut Info,
    table: *const Table,
) -> u8 {
    parse(&*file, &mut *info, &*table).err().unwrap_or(0)
}
type Extract = unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32) -> u8;
/// # Safety
/// Info and offsets describe initialized caller-owned data, callback context is live for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_dictzip_range(
    info: *const Info,
    offsets: *const u32,
    count: usize,
    offset: u32,
    size: u32,
    ctx: *mut c_void,
    extract: Extract,
) -> u8 {
    if size == 0 {
        return 0;
    }
    let info = &*info;
    if offset > info.total_size
        || size > info.total_size - offset
        || info.chunk_length == 0
        || count < 2
    {
        return READ;
    }
    let length = u32::from(info.chunk_length);
    let start = offset / length;
    let end = (offset + size - 1) / length;
    if end as usize + 1 >= count {
        return READ;
    }
    let offsets = slice::from_raw_parts(offsets, count);
    let mut remaining = size;
    for chunk in start..=end {
        let mut out = length;
        if chunk as usize == count - 2 {
            out = info.total_size.wrapping_sub(chunk.wrapping_mul(length));
        }
        if out == 0 || out > length {
            out = length;
        }
        let local = if chunk == start { offset % length } else { 0 };
        let take = remaining.min(out.wrapping_sub(local));
        let at = chunk as usize;
        let status = extract(
            ctx,
            info.data_offset.wrapping_add(offsets[at]),
            offsets[at + 1].wrapping_sub(offsets[at]),
            local,
            take,
        );
        if status != 0 {
            return status;
        }
        remaining -= take;
        if remaining == 0 {
            break;
        }
    }
    if remaining == 0 {
        0
    } else {
        BAD
    }
}
/// # Safety
/// Buffer holds XP_DICTIONARY_ZIP_OUTPUT_BYTES initialized writable bytes. Read returns 0 on success or an ExtractError.
#[no_mangle]
pub unsafe extern "C" fn xp_dictzip_slice(
    ctx: *mut c_void,
    read: unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> u8,
    buffer: *mut u8,
    mut discard: u32,
    mut count: u32,
    out: *const File,
) -> u8 {
    while discard > 0 {
        let n = discard.min(XP_DICTIONARY_ZIP_OUTPUT_BYTES as u32);
        let error = read(ctx, buffer, n as usize);
        if error != 0 {
            return error;
        }
        discard -= n;
    }
    while count > 0 {
        let n = count.min(XP_DICTIONARY_ZIP_OUTPUT_BYTES as u32);
        let error = read(ctx, buffer, n as usize);
        if error != 0 {
            return error;
        }
        if !(*out).write(slice::from_raw_parts(buffer, n as usize)) {
            return READ;
        }
        count -= n;
    }
    0
}

/// # Safety
/// Scalars and buffer_bytes initialized writable bytes are exclusively borrowed; buffer_bytes > 0.
/// Returned data lives until the next refill.
#[no_mangle]
pub unsafe extern "C" fn xp_dictzip_refill(
    file: *const File,
    remaining: *mut u32,
    failed: *mut bool,
    buffer: *mut u8,
    buffer_bytes: usize,
    data: *mut *const u8,
) -> usize {
    if *remaining == 0 {
        return 0;
    }
    let count = (*remaining as usize).min(buffer_bytes);
    let n = (*file).read(slice::from_raw_parts_mut(buffer, count));
    if n <= 0 {
        *failed = true;
        return 0;
    }
    *remaining -= n as u32;
    *data = buffer;
    n as usize
}
