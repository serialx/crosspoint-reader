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
