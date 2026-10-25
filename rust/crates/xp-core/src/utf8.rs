//! The firmware decoder's byte-consumption rules, including malformed UTF-8.

/// Returns a codepoint and advances `position`; NUL and end do not advance.
pub fn next(bytes: &[u8], position: &mut usize) -> Option<u32> {
    let start = *position;
    let lead = *bytes.get(start)?;
    if lead == 0 {
        return None;
    }
    let count = match lead {
        0xc0..=0xdf => 2,
        0xe0..=0xef => 3,
        0xf0..=0xf7 => 4,
        _ => 1,
    };
    if count == 1 {
        *position += 1;
        return Some(if lead < 0x80 { lead as u32 } else { 0xfffd });
    }
    let mut cp = u32::from(lead & ((1 << (7 - count)) - 1));
    for i in 1..count {
        let byte = bytes.get(start + i).copied().unwrap_or(0);
        if byte & 0xc0 != 0x80 {
            *position += i;
            return Some(0xfffd);
        }
        cp = (cp << 6) | u32::from(byte & 0x3f);
    }
    let overlong =
        (count == 2 && cp < 0x80) || (count == 3 && cp < 0x800) || (count == 4 && cp < 0x10000);
    if overlong || (0xd800..=0xdfff).contains(&cp) || cp > 0x10ffff {
        *position += 1;
        return Some(0xfffd);
    }
    *position += count;
    Some(cp)
}

pub fn encode(cp: u32, out: &mut [u8; 4]) -> usize {
    if cp < 0x80 {
        out[0] = cp as u8;
        return 1;
    }
    let n = if cp < 0x800 {
        out[0] = (0xc0 | (cp >> 6)) as u8;
        2
    } else if cp < 0x10000 {
        out[0] = (0xe0 | (cp >> 12)) as u8;
        out[1] = (0x80 | ((cp >> 6) & 0x3f)) as u8;
        3
    } else {
        out[0] = (0xf0 | (cp >> 18)) as u8;
        out[1] = (0x80 | ((cp >> 12) & 0x3f)) as u8;
        out[2] = (0x80 | ((cp >> 6) & 0x3f)) as u8;
        4
    };
    out[n - 1] = (0x80 | (cp & 0x3f)) as u8;
    n
}
fn codepoint_len(lead: u8) -> usize {
    match lead {
        0xc0..=0xdf => 2,
        0xe0..=0xef => 3,
        0xf0..=0xf7 => 4,
        _ => 1,
    }
}
pub fn safe_truncate(bytes: &[u8]) -> usize {
    if bytes.is_empty() {
        return 0;
    }
    let mut lead = bytes.len() - 1;
    while lead > 0 && bytes[lead] & 0xc0 == 0x80 {
        lead -= 1;
    }
    if lead > 0 && bytes.len() - lead < codepoint_len(bytes[lead]) {
        lead
    } else {
        bytes.len()
    }
}
pub fn remove_last(bytes: &[u8]) -> usize {
    if bytes.is_empty() {
        return 0;
    }
    let mut pos = bytes.len() - 1;
    while pos > 0 && bytes[pos] & 0xc0 == 0x80 {
        pos -= 1;
    }
    pos
}
pub fn may_compose(bytes: &[u8]) -> bool {
    bytes.iter().any(|b| matches!(b, 0xcc | 0xcd | 0xe1))
}
/// Emits borrowed bytes only for the callback duration. A rejected absorbed offset stops emission.
pub fn compose(
    bytes: &[u8],
    emit: &mut impl FnMut(&[u8]),
    absorbed: &mut impl FnMut(u32) -> bool,
) -> bool {
    use crate::text::{combining, compose_pair};
    if !may_compose(bytes) {
        emit(bytes);
        return true;
    }
    let mut pos = 0;
    let mut base = None;
    let mut source = 0u32;
    let mut encoded = [0; 4];
    let mut emit_cp = |cp| {
        let n = encode(cp, &mut encoded);
        emit(&encoded[..n]);
    };
    while let Some(cp) = next(bytes, &mut pos) {
        let current = source;
        source = source.wrapping_add(1);
        if let Some(composed) = base.and_then(|b| compose_pair(b, cp)) {
            if !absorbed(current) {
                return false;
            }
            base = Some(composed);
            continue;
        }
        if let Some(b) = base.take() {
            emit_cp(b);
        }
        if combining(cp) {
            emit_cp(cp);
        } else {
            base = Some(cp);
        }
    }
    if let Some(b) = base {
        emit_cp(b);
    }
    true
}
/// Input excludes the terminator; output never grows and preserves uncomposed raw bytes.
pub fn compose_in_place(bytes: &mut [u8]) -> usize {
    use crate::text::{combining, compose_pair};
    let mut read = 0;
    let mut write = 0;
    let mut base_start = 0;
    let mut base = 0;
    let mut encoded = [0; 4];
    loop {
        let start = read;
        let Some(cp) = next(bytes, &mut read) else {
            break;
        };
        if let Some(composed) = compose_pair(base, cp) {
            write = base_start;
            let n = encode(composed, &mut encoded);
            bytes[write..write + n].copy_from_slice(&encoded[..n]);
            write += n;
            base = composed;
        } else {
            base_start = write;
            bytes.copy_within(start..read, write);
            write += read - start;
            base = if combining(cp) { 0 } else { cp };
        }
    }
    write
}

#[cfg(test)]
mod tests {
    use super::next;
    #[test]
    fn malformed_consumption() {
        for (bytes, consumed) in [
            (b"\x80".as_slice(), 1),
            (b"\xff", 1),
            (b"\xc0\xaf", 1),
            (b"\xe0\x80\xaf", 1),
            (b"\xed\xa0\x80", 1),
            (b"\xf4\x90\x80\x80", 1),
            (b"\xe2\x82x", 2),
            (b"\xf0\x90\x80", 3),
            (b"\xf8\x80", 1),
        ] {
            let mut pos = 0;
            assert_eq!(next(bytes, &mut pos), Some(0xfffd));
            assert_eq!(pos, consumed);
        }
        let mut pos = 0;
        assert_eq!(next(b"\0x", &mut pos), None);
        assert_eq!(pos, 0);
        assert_eq!(next("é".as_bytes(), &mut pos), Some(0xe9));
        assert_eq!(pos, 2);
    }
}
