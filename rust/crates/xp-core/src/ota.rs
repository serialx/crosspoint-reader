//! OTA policy without allocation or platform access.

// Like sscanf("%d.%d.%d"): signed decimal, leading whitespace per component,
// literal dots, suffix ignored. Missing components are never read.
fn component(input: &mut &[u8]) -> Option<i32> {
    while input
        .first()
        .is_some_and(|b| b.is_ascii_whitespace() || *b == 0x0b)
    {
        *input = input.get(1..)?;
    }
    let negative = input.first() == Some(&b'-');
    if negative || input.first() == Some(&b'+') {
        *input = input.get(1..)?;
    }
    let mut value = 0i32;
    let mut digits = false;
    while let Some(&byte) = input.first().filter(|b| b.is_ascii_digit()) {
        digits = true;
        value = value.checked_mul(10)?.checked_sub(i32::from(byte - b'0'))?;
        *input = input.get(1..)?;
    }
    if !digits {
        return None;
    }
    if negative {
        Some(value)
    } else {
        value.checked_neg()
    }
}

pub fn newer(available: bool, current: &[u8], latest: &[u8]) -> bool {
    if !available || latest.is_empty() || current == latest {
        return false;
    }
    let (mut c, mut l) = (current, latest);
    for part in 0..3 {
        let (Some(cv), Some(lv)) = (component(&mut c), component(&mut l)) else {
            return false;
        };
        if cv != lv {
            return lv > cv;
        }
        if part < 2 {
            if c.first() != Some(&b'.') || l.first() != Some(&b'.') {
                return false;
            }
            c = &c[1..];
            l = &l[1..];
        }
    }
    current.windows(3).any(|w| w == b"-rc")
}

/// ESP ROM crc32_le(UINT32_MAX, little_endian_seq, 4).
pub fn seq_crc(seq: u32) -> u32 {
    let mut crc = 0u32;
    for byte in seq.to_le_bytes() {
        crc ^= u32::from(byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320 & 0u32.wrapping_sub(crc & 1));
        }
    }
    !crc
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn defined_partial_and_malformed_versions() {
        assert!(newer(true, b"1.2.3", b"2"));
        assert!(newer(true, b"1.2.3", b"1.3"));
        for bad in [
            b"v2.4.1".as_slice(),
            b"1.2",
            b"1.x",
            b"2147483648.0.0",
            b"-2147483649.0.0",
        ] {
            assert!(!newer(true, b"1.2.3", bad));
        }
        assert!(newer(true, b"1.2.3-rc+hash", b"1.2.3"));
        assert!(!newer(true, b"1.2.3-dev-branch-sha", b"1.2.3"));
    }
}
