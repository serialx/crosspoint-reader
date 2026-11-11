//! The firmware decoder's byte-consumption rules, including malformed UTF-8.

/// Returns a codepoint and advances `position`; NUL and end do not advance.
pub fn next(bytes: &[u8], position: &mut usize) -> Option<u32> {
    let start = *position;
    let lead = *bytes.get(start)?;
    if lead < 0x80 {
        if lead == 0 {
            return None;
        }
        *position += 1;
        return Some(lead as u32);
    }
    let (count, minimum) = match lead {
        0xc0..=0xdf => (2, 0x80),
        0xe0..=0xef => (3, 0x800),
        0xf0..=0xf7 => (4, 0x10000),
        _ => {
            *position += 1;
            return Some(0xfffd);
        }
    };
    let mut cp = u32::from(lead & ((1 << (7 - count)) - 1));
    for i in 1..count {
        let byte = bytes.get(start + i).copied().unwrap_or(0);
        if byte & 0xc0 != 0x80 {
            *position += i;
            return Some(0xfffd);
        }
        cp = (cp << 6) | u32::from(byte & 0x3f);
    }
    if cp < minimum || (0xd800..=0xdfff).contains(&cp) || cp > 0x10ffff {
        *position += 1;
        return Some(0xfffd);
    }
    *position += count;
    Some(cp)
}
