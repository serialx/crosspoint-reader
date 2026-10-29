use super::{abi::*, bytes, File};
use core::{ffi::c_void, slice};
#[repr(C)]
pub struct Header {
    pub(super) samples: u32,
    pub(super) source_size: u32,
    pub(super) entries: u32,
    pub(super) valid: bool,
}
fn header(file: &File, magic: u32, interval: u32) -> Header {
    let mut out = Header {
        samples: 0,
        source_size: 0,
        entries: 0,
        valid: false,
    };
    let mut raw = [0; 24];
    if !file.seek(0) || file.read(&mut raw) != 24 {
        return out;
    }
    let mut words = [0; 6];
    for (word, b) in words.iter_mut().zip(raw.chunks_exact(4)) {
        *word = u32::from_le_bytes([b[0], b[1], b[2], b[3]]);
    }
    if words[0] != magic || words[1] != 2 || words[2] != interval {
        return out;
    }
    out.samples = words[3];
    out.source_size = words[4];
    out.entries = words[5];
    out.valid = true;
    out
}
fn sample(file: &File, index: u32) -> Option<u32> {
    let mut b = [0; 4];
    if !file.seek(24usize.wrapping_add((index as usize).wrapping_mul(4))) || file.read(&mut b) != 4
    {
        None
    } else {
        Some(u32::from_le_bytes(b))
    }
}
fn word(file: &File, buf: &mut [u8]) -> i32 {
    if buf.is_empty() {
        return -1;
    }
    let last = buf.len() - 1;
    for (i, b) in buf[..last].iter_mut().enumerate() {
        let Some(c) = file.byte() else {
            return -1;
        };
        *b = c;
        if c == 0 {
            return i as i32;
        }
    }
    buf[last] = 0;
    while file.byte().is_some_and(|b| b > 0) {}
    last as i32
}
fn text(buf: &[u8]) -> &[u8] {
    &buf[..buf.iter().position(|b| *b == 0).unwrap_or(buf.len())]
}
fn cmp(a: &[u8], b: &[u8]) -> core::cmp::Ordering {
    a.iter()
        .map(u8::to_ascii_lowercase)
        .cmp(b.iter().map(u8::to_ascii_lowercase))
}
fn bisect(sidecar: &File, source: &File, count: u32, target: &[u8], buf: &mut [u8]) -> u32 {
    if count == 0 {
        return 0;
    }
    let mut lo = 0;
    let mut hi = count - 1;
    while lo < hi {
        let mid = lo + (hi - lo).div_ceil(2);
        let Some(at) = sample(sidecar, mid) else {
            lo = 0;
            break;
        };
        if !source.seek(at as usize) || word(source, buf) < 0 {
            lo = 0;
            break;
        }
        if cmp(text(buf), target).is_lt() {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    // The original final sample read keeps any bytes delivered by a short read.
    let mut raw = [0; 4];
    if sidecar.seek(24usize.wrapping_add((lo as usize).wrapping_mul(4))) {
        let _ = sidecar.read(&mut raw);
    }
    u32::from_le_bytes(raw)
}
/// # Safety
/// File descriptor and output remain valid for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_header(
    file: *const File,
    magic: u32,
    interval: u32,
    out: *mut Header,
) {
    out.write(header(&*file, magic, interval));
}
#[repr(C)]
pub struct Scan {
    samples: u32,
    entries: u32,
    position: u32,
}
/// # Safety
/// File descriptors are live, buffer holds buffer_bytes initialized writable bytes (buffer_bytes > 0), callbacks borrow their context.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_scan(
    src: *const File,
    out: *const File,
    magic: u32,
    suffix: u32,
    buffer: *mut u8,
    buffer_bytes: usize,
    ctx: *mut c_void,
    yield_fn: Option<unsafe extern "C" fn(*mut c_void)>,
    result: *mut Scan,
) -> u8 {
    let src = &*src;
    let out = &*out;
    let result = &mut *result;
    *result = Scan {
        samples: 0,
        entries: 0,
        position: 0,
    };
    let size = src.size() as u32;
    let mut ok = out.write(&[0; 24]);
    if ok {
        ok = out.write(&0u32.to_le_bytes());
        result.samples = 1;
    }
    let buffer = slice::from_raw_parts_mut(buffer, buffer_bytes);
    let mut left = 0;
    let mut since = 0u32;
    while ok && result.position < size {
        let n = src.read(buffer);
        if n <= 0 {
            return XP_DICTIONARY_SCAN_READ_ERROR;
        }
        let n = n as usize;
        if n > buffer.len() {
            return XP_DICTIONARY_SCAN_READ_ERROR;
        }
        for (i, b) in buffer[..n].iter().enumerate() {
            if !ok {
                break;
            }
            if left == 0 {
                if *b == 0 {
                    left = suffix;
                }
            } else {
                left -= 1;
                if left == 0 {
                    result.entries = result.entries.wrapping_add(1);
                    let next = result.position.wrapping_add(i as u32).wrapping_add(1);
                    if result.entries.is_multiple_of(XP_DICTIONARY_SAMPLE_INTERVAL) && next < size {
                        ok = out.write(&next.to_le_bytes());
                        result.samples = result.samples.wrapping_add(1);
                    }
                }
            }
        }
        result.position = result.position.wrapping_add(n as u32);
        since = since.wrapping_add(n as u32);
        if since >= 65536 {
            if let Some(f) = yield_fn {
                since = 0;
                f(ctx);
            }
        }
    }
    if ok {
        let mut raw = [0; 24];
        for (dst, value) in raw.chunks_exact_mut(4).zip([
            magic,
            2,
            XP_DICTIONARY_SAMPLE_INTERVAL,
            result.samples,
            size,
            result.entries,
        ]) {
            dst.copy_from_slice(&value.to_le_bytes());
        }
        ok = out.seek(0) && out.write(&raw);
    }
    if ok {
        XP_DICTIONARY_SCAN_OK
    } else {
        XP_DICTIONARY_SCAN_WRITE_ERROR
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Location {
    pub(super) offset: u32,
    pub(super) size: u32,
    pub(super) found: bool,
    pub(super) read_error: bool,
}
fn be(b: &[u8]) -> u32 {
    u32::from_be_bytes([b[0], b[1], b[2], b[3]])
}
fn ordinal(
    idx: &File,
    qidx: &File,
    count: u32,
    entries: u32,
    idx_size: u32,
    ordinal: u32,
    buf: &mut [u8],
) -> Location {
    let mut result = Location::default();
    let mut start = 0;
    let mut start_ordinal = 0;
    if count > 0 {
        if entries != 0 && ordinal >= entries {
            return result;
        }
        let sample_index = (ordinal / XP_DICTIONARY_SAMPLE_INTERVAL).min(count - 1);
        if let Some(at) = sample(qidx, sample_index) {
            start = at;
            start_ordinal = sample_index * XP_DICTIONARY_SAMPLE_INTERVAL;
        }
    }
    if !idx.seek(start as usize) {
        result.offset = start;
        result.read_error = true;
        return result;
    }
    let mut suffix = [0; 8];
    for _ in start_ordinal..ordinal {
        if word(idx, buf) < 0 || idx.read(&mut suffix) != 8 || idx.tell() as u32 >= idx_size {
            return result;
        }
    }
    if idx.tell() as u32 >= idx_size || word(idx, buf) < 0 || idx.read(&mut suffix) != 8 {
        return result;
    }
    result.offset = be(&suffix[..4]);
    result.size = be(&suffix[4..]);
    result.found = true;
    result
}
#[repr(C)]
pub struct Search {
    idx: *const File,
    qidx: *const File,
    syn: *const File,
    sidx: *const File,
    idx_size: u32,
    samples: u32,
    entries: u32,
    syn_size: u32,
    syn_samples: u32,
    word: *mut u8,
    context: *mut c_void,
    append: unsafe extern "C" fn(*mut c_void, *const u8, Location) -> bool,
    seek_error: unsafe extern "C" fn(*mut c_void, bool, u32),
}
/// # Safety
/// Search borrows all file descriptors, one 256-byte scratch buffer and append context.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_search(
    search: *const Search,
    target: *const u8,
    len: usize,
    synonyms: bool,
) -> bool {
    let s = &*search;
    let target = bytes(target, len);
    let buf = slice::from_raw_parts_mut(s.word, XP_DICTIONARY_WORD_BYTES);
    let source = if synonyms { &*s.syn } else { &*s.idx };
    let sidecar = if synonyms { &*s.sidx } else { &*s.qidx };
    let count = if synonyms { s.syn_samples } else { s.samples };
    let size = if synonyms { s.syn_size } else { s.idx_size };
    let start = bisect(sidecar, source, count, target, buf);
    if !source.seek(start as usize) {
        (s.seek_error)(s.context, synonyms, start);
        return true;
    }
    let mut failed = false;
    while (source.tell() as u32) < size {
        if word(source, buf) < 0 {
            break;
        }
        let mut raw = [0; 8];
        let n = if synonyms { 4 } else { 8 };
        if source.read(&mut raw[..n]) != n as i32 {
            break;
        }
        let order = cmp(text(buf), target);
        if order.is_gt() {
            break;
        }
        if !order.is_eq() {
            continue;
        }
        let location = if synonyms {
            ordinal(
                &*s.idx,
                &*s.qidx,
                s.samples,
                s.entries,
                s.idx_size,
                be(&raw[..4]),
                buf,
            )
        } else {
            Location {
                offset: be(&raw[..4]),
                size: be(&raw[4..]),
                found: true,
                read_error: false,
            }
        };
        if location.read_error {
            failed = true;
            (s.seek_error)(s.context, false, location.offset);
        }
        if location.found && !(s.append)(s.context, buf.as_ptr(), location) {
            break;
        }
    }
    failed
}
/// # Safety
/// Input is initialized and output offsets are writable. No bytes are retained.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_clean_bounds(
    data: *const u8,
    len: usize,
    start: *mut usize,
    end: *mut usize,
) {
    let b = bytes(data, len);
    let mut lo = 0;
    let mut hi = b.len();
    while lo < hi {
        if b[lo] < 128 && !b[lo].is_ascii_alphanumeric() {
            lo += 1;
        } else if hi - lo >= 3 && b[lo] == 0xe2 && matches!(b[lo + 1], 0x80 | 0x81) {
            lo += 3;
        } else {
            let mut pos = lo;
            let cp = xp_core::utf8::next(b, &mut pos).unwrap_or(0);
            if xp_core::text::no_break_after(cp) {
                lo = pos;
            } else {
                break;
            }
        }
    }
    while hi > lo {
        if b[hi - 1] < 128 && !b[hi - 1].is_ascii_alphanumeric() {
            hi -= 1;
        } else if hi - lo >= 3 && b[hi - 3] == 0xe2 && matches!(b[hi - 2], 0x80 | 0x81) {
            hi -= 3;
        } else {
            let mut last = hi - 1;
            while last > lo && b[last] & 0xc0 == 0x80 {
                last -= 1;
            }
            let mut pos = last;
            let cp = xp_core::utf8::next(b, &mut pos).unwrap_or(0);
            if xp_core::text::no_break_before(cp) {
                hi = last;
            } else {
                break;
            }
        }
    }
    *start = lo;
    *end = hi;
}
/// # Safety
/// Data borrows len writable bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_lower(data: *mut u8, len: usize) {
    if len > 0 {
        for b in slice::from_raw_parts_mut(data, len) {
            *b = b.to_ascii_lowercase();
        }
    }
}
/// # Safety
/// Input and callback context remain live; callback copies each borrowed prefix with its optional suffix.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_stems(
    data: *const u8,
    len: usize,
    ctx: *mut c_void,
    emit: unsafe extern "C" fn(*mut c_void, usize, u8),
) {
    let word = bytes(data, len);
    let ends = |s: &[u8]| word.len() > s.len() && word.ends_with(s);
    if ends(b"'s") {
        emit(ctx, len - 2, 0);
    }
    if ends(b"\xe2\x80\x99s") {
        emit(ctx, len - 4, 0);
    }
    if ends(b"ies") {
        emit(ctx, len - 3, b'y');
    }
    if ends(b"es") {
        emit(ctx, len - 2, 0);
    }
    if ends(b"s") {
        emit(ctx, len - 1, 0);
    }
    if ends(b"ed") {
        emit(ctx, len - 2, 0);
        emit(ctx, len - 1, 0);
        if len >= 4 && word[len - 3] == word[len - 4] {
            emit(ctx, len - 3, 0);
        }
    }
    if ends(b"ing") {
        emit(ctx, len - 3, 0);
        emit(ctx, len - 3, b'e');
        if len >= 5 && word[len - 4] == word[len - 5] {
            emit(ctx, len - 4, 0);
        }
    }
}
/// # Safety
/// Input includes exactly len readable bytes; the routine emulates the original terminated .ifo prefix.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_ifo(data: *const u8, len: usize) -> u8 {
    let b = text(bytes(data, len));
    fn value<'a>(b: &'a [u8], key: &[u8]) -> Option<&'a [u8]> {
        let start = b.windows(key.len()).position(|w| w == key)?;
        let eq = b[start..].iter().position(|b| *b == b'=')?;
        Some(&b[start + eq + 1..])
    }
    let offset = value(b, b"idxoffsetbits").is_some_and(|v| {
        let mut i = 0;
        while v.get(i).is_some_and(|b| matches!(b, 9..=13 | 32)) {
            i += 1;
        }
        let negative = v.get(i) == Some(&b'-');
        if matches!(v.get(i), Some(b'+' | b'-')) {
            i += 1;
        }
        let mut n = 0u64;
        while let Some(c) = v.get(i).filter(|c| c.is_ascii_digit()) {
            n = n.saturating_mul(10).saturating_add(u64::from(*c - b'0'));
            i += 1;
        }
        !negative && n == 64
    });
    let html = value(b, b"sametypesequence").is_some_and(|v| {
        matches!(v.first(), Some(b'h' | b'x')) && matches!(v.get(1), None | Some(b'\r' | b'\n'))
    });
    (u8::from(offset) * XP_DICTIONARY_IFO_OFFSETS64) | (u8::from(html) * XP_DICTIONARY_IFO_HTML)
}
