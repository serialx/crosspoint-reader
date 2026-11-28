use super::{data, Codepoints};
use crate::text as predicates;

const MAX_BYTES: usize = 160;
const MAX_CHARS: usize = 70;

fn be(bytes: &[u8]) -> usize {
    bytes.iter().fold(0, |v, &b| (v << 8) | usize::from(b))
}

// Returns the levels slice and the matching child, without expanding trie nodes.
fn node(blob: &[u8], addr: usize, letter: u8) -> Option<(usize, &[u8])> {
    let header = *blob.get(addr)?;
    let mut pos = addr + 1;
    let stride = usize::from(((header >> 5) & 3).max(1));
    let mut children = usize::from(header & 31);
    if children == 31 {
        children = usize::from(*blob.get(pos)?);
        pos += 1;
    }
    let levels = if header & 0x80 != 0 {
        let hi = usize::from(*blob.get(pos)?);
        let lo = usize::from(*blob.get(pos + 1)?);
        pos += 2;
        let offset = (hi << 4) | (lo >> 4);
        blob.get(offset..offset + (lo & 15))?
    } else {
        &[]
    };
    let transitions = blob.get(pos..pos + children)?;
    let targets = blob.get(pos + children..pos + children + children * stride)?;
    let Some(index) = transitions.iter().position(|&c| c == letter) else {
        return Some((usize::MAX, levels));
    };
    let delta = be(targets.get(index * stride..(index + 1) * stride)?);
    let delta = match stride {
        1 => (delta as u8 as i8) as i32,
        2 => (delta as u16 as i16) as i32,
        _ => delta as i32 - (1 << 23),
    };
    let child = addr.checked_add_signed(delta as isize)?;
    if child >= blob.len() {
        return None;
    }
    Some((child, levels))
}

pub(super) fn scores(cps: Codepoints<'_>, language: i32) -> [u8; MAX_CHARS] {
    let mut scores = [0; MAX_CHARS];
    let Some(blob) = data::trie(language) else {
        return scores;
    };
    let cyr = language == 3 || language == 8;
    let mut bytes = [0u8; MAX_BYTES];
    let mut offsets = [0u8; MAX_CHARS];
    let mut reverse = [u8::MAX; MAX_BYTES];
    let mut byte_len = 1;
    let mut chars = 1;
    bytes[0] = b'.';
    for (cp, _) in cps {
        if chars >= MAX_CHARS - 1
            || !(if cyr {
                predicates::cyrillic(cp)
            } else {
                predicates::latin(cp)
            })
        {
            return scores;
        }
        let cp = predicates::lower(cp, cyr);
        let Some(ch) = char::from_u32(cp) else {
            return scores;
        };
        let mut encoded = [0; 4];
        let encoded = ch.encode_utf8(&mut encoded).as_bytes();
        if byte_len + encoded.len() > MAX_BYTES {
            return scores;
        }
        offsets[chars] = byte_len as u8;
        chars += 1;
        bytes[byte_len..byte_len + encoded.len()].copy_from_slice(encoded);
        byte_len += encoded.len();
    }
    if chars == 1 || byte_len >= MAX_BYTES {
        return scores;
    }
    offsets[chars] = byte_len as u8;
    chars += 1;
    bytes[byte_len] = b'.';
    byte_len += 1;
    for (i, &offset) in offsets[..chars].iter().enumerate() {
        reverse[usize::from(offset)] = i as u8;
    }
    let Some(root_bytes) = blob.get(..4) else {
        return scores;
    };
    let root = be(root_bytes);
    for &start in &offsets[..chars] {
        let start = usize::from(start);
        let mut addr = root;
        for &letter in &bytes[start..byte_len] {
            let Some((next, _)) = node(blob, addr, letter) else {
                break;
            };
            if next == usize::MAX {
                break;
            }
            addr = next;
            let Some((_, levels)) = node(blob, addr, 0) else {
                break;
            };
            let mut offset = 0;
            for &packed in levels {
                offset += usize::from(packed / 10);
                let split = start + offset;
                if split >= byte_len {
                    continue;
                }
                let boundary = usize::from(reverse[split]);
                if boundary < 2 || boundary + 2 > chars {
                    continue;
                }
                scores[boundary] = scores[boundary].max(packed % 10);
            }
        }
    }
    scores
}
