extern crate alloc;
use super::{append, Result};
use alloc::vec::Vec;

fn hex(b: u8) -> Option<u8> {
    match b {
        b'0'..=b'9' => Some(b - b'0'),
        b'a'..=b'f' => Some(b - b'a' + 10),
        b'A'..=b'F' => Some(b - b'A' + 10),
        _ => None,
    }
}
pub fn decode(out: &mut Vec<u8>, bytes: &[u8]) -> Result<()> {
    out.clear();
    out.try_reserve(bytes.len()).map_err(|_| 1u32)?;
    let mut iter = bytes.iter().copied().peekable();
    while let Some(c) = iter.next() {
        if c == b'%' {
            let mut probe = iter.clone();
            if let Some((a, b)) = probe.next().and_then(hex).zip(probe.next().and_then(hex)) {
                out.push(a * 16 + b);
                iter = probe;
                continue;
            }
        }
        out.push(c);
    }
    Ok(())
}
pub fn normalize(out: &mut Vec<u8>, decoded: &[u8]) -> Result<()> {
    out.clear();
    for part in decoded.split(|&c| c == b'/').filter(|p| !p.is_empty()) {
        if part == b".." {
            out.truncate(out.iter().rposition(|&c| c == b'/').unwrap_or(0));
        } else {
            if !out.is_empty() {
                append(out, b"/")?;
            }
            append(out, part)?;
        }
    }
    Ok(())
}
pub fn target(
    base: &[u8],
    raw: &[u8],
    href: &mut Vec<u8>,
    anchor: &mut Vec<u8>,
    scratch: &mut Vec<u8>,
    decoded: &mut Vec<u8>,
) -> Result<()> {
    scratch.clear();
    append(scratch, base)?;
    append(scratch, raw)?;
    let mut parts = scratch.splitn(2, |&c| c == b'#');
    decode(decoded, parts.next().unwrap_or(&[]))?;
    normalize(href, decoded)?;
    decode(anchor, parts.next().unwrap_or(&[]))
}
