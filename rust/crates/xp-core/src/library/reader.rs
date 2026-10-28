use super::format::{self, Header, Record};

pub trait Read {
    fn read(&mut self, at: u32, out: &mut [u8]) -> bool;
    fn string(&mut self, at: u32, len: usize, append: bool) -> Option<u8>;
    fn append(&mut self, bytes: &[u8]);
    fn fail(&mut self);
}
fn record_bytes(r: &mut Record) -> &mut [u8] {
    // Record is POD with no padding and all bit patterns valid.
    unsafe { core::slice::from_raw_parts_mut(core::ptr::from_mut(r).cast(), 128) }
}
pub fn record(h: &Header, io: &mut impl Read, n: u16, r: &mut Record) -> bool {
    if n >= h.books || !io.read(format::record_offset(h, n), record_bytes(r)) {
        return false;
    }
    sanitize(h, r)
}
pub fn sanitize(h: &Header, r: &mut Record) -> bool {
    r.fold_len = r.fold_len.min(96);
    r.author_len = r.author_len.min(12);
    if r.metadata > 2 {
        return false;
    }
    if r.name_off > h.name_len {
        r.name_off = 0;
        r.name_len = 0;
    }
    true
}
pub fn ordinal(h: &Header, io: &mut impl Read, order: u8, row: u16) -> u16 {
    if row >= h.books {
        return u16::MAX;
    }
    match order {
        2 => return row,
        3 => return h.books - 1 - row,
        0 | 1 | 4 | 5 => {}
        _ => return u16::MAX,
    }
    let k = if order & 1 != 0 {
        h.books - 1 - row
    } else {
        row
    };
    let mut bytes = [0; 2];
    if !io.read(format::order_offset(h, k, order < 2), &mut bytes) {
        return u16::MAX;
    }
    let n = u16::from_le_bytes(bytes);
    if n < h.books {
        n
    } else {
        u16::MAX
    }
}
pub fn hash(h: &Header, io: &mut impl Read, r: &Record) -> Option<u64> {
    if r.name_off > h.name_len || h.name_len - r.name_off < 8 {
        io.fail();
        return None;
    }
    let mut b = [0; 8];
    if io.read(h.name_start.wrapping_add(r.name_off), &mut b) {
        Some(u64::from_le_bytes(b))
    } else {
        None
    }
}
fn name_valid(h: &Header, r: &Record) -> bool {
    r.name_len != 0
        && r.name_off <= h.name_len
        && h.name_len - r.name_off >= 8
        && u32::from(r.name_len) <= h.name_len - r.name_off - 8
}
pub fn name(h: &Header, io: &mut impl Read, r: &Record, append: bool) -> bool {
    name_valid(h, r)
        && io
            .string(
                h.name_start.wrapping_add(r.name_off).wrapping_add(8),
                r.name_len as usize,
                append,
            )
            .is_some()
}
pub fn blob(h: &Header, io: &mut impl Read, r: &Record, field: u8) -> bool {
    if !name_valid(h, r) {
        return false;
    }
    let mut at = r.name_off + 8 + u32::from(r.name_len);
    for i in 0..=field {
        if at >= h.name_len {
            return false;
        }
        let mut len = [0];
        if !io.read(h.name_start.wrapping_add(at), &mut len) {
            return false;
        }
        at += 1;
        if u32::from(len[0]) > h.name_len - at {
            return false;
        }
        if i == field {
            return io
                .string(h.name_start.wrapping_add(at), len[0] as usize, false)
                .is_some();
        }
        at += u32::from(len[0]);
    }
    false
}
pub fn path(h: &Header, io: &mut impl Read, r: &Record) -> bool {
    if r.folder >= h.folders {
        return false;
    }
    let mut at = h.folder_start;
    let end = h.folder_start.wrapping_add(h.folder_len);
    for i in 0..=r.folder {
        if at >= end {
            return false;
        }
        let mut len = [0];
        if !io.read(at, &mut len) || len[0] == 0 {
            return false;
        }
        if u32::from(len[0]) > end - at - 1 {
            return false;
        }
        if i == r.folder {
            let Some(last) = io.string(at + 1, len[0] as usize, false) else {
                return false;
            };
            if last != b'/' {
                io.append(b"/");
            }
            return name(h, io, r, true);
        }
        at += 1 + u32::from(len[0]);
        if at >= end {
            return false;
        }
    }
    false
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Identity {
    pub hash: u64,
    pub size: u32,
}
pub fn recent(
    h: &Header,
    io: &mut impl Read,
    books: &[Identity],
    rows: &mut [u16],
    chunk: &mut [u8],
) -> bool {
    if books.len() > 16 || rows.len() < books.len() || chunk.len() < 4096 {
        return false;
    }
    let mut ordinals = [u16::MAX; 16];
    let mut unresolved = books.len();
    for base in (0..h.books).step_by(32) {
        if unresolved == 0 {
            break;
        }
        let n = (h.books - base).min(32) as usize;
        if !io.read(format::record_offset(h, base), &mut chunk[..n * 128]) {
            return false;
        }
        for r in 0..n {
            if unresolved == 0 {
                break;
            }
            let record = unsafe {
                chunk
                    .as_ptr()
                    .add(r * 128)
                    .cast::<Record>()
                    .read_unaligned()
            };
            let mut stored = None;
            for (i, book) in books.iter().enumerate() {
                if ordinals[i] != u16::MAX || (book.size != 0 && book.size != record.size) {
                    continue;
                }
                if stored.is_none() {
                    if record.name_off > h.name_len {
                        break;
                    }
                    stored = hash(h, io, &record);
                    if stored.is_none() {
                        return false;
                    }
                }
                if stored == Some(book.hash) {
                    ordinals[i] = base + r as u16;
                    unresolved -= 1;
                }
            }
        }
    }
    if unresolved < books.len() {
        for base in (0..h.books).step_by(64) {
            let n = (h.books - base).min(64) as usize;
            if !io.read(format::order_offset(h, base, true), &mut chunk[..n * 2]) {
                return false;
            }
            for k in 0..n {
                let ordinal = u16::from_le_bytes([chunk[k * 2], chunk[k * 2 + 1]]);
                for i in 0..books.len() {
                    if ordinals[i] != u16::MAX && ordinals[i] == ordinal {
                        rows[i] = base + k as u16;
                    }
                }
            }
        }
    }
    true
}
