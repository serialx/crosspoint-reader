//! Library collation over caller-owned byte buffers; no allocations.
use crate::{text, utf8};
const LETTER_RANGES: &[(u32, u32)] = &[
    (0x0041, 0x005A),
    (0x0061, 0x007A),
    (0x00C0, 0x02AF),
    (0x0370, 0x03FF),
    (0x1F00, 0x1FFF),
    (0x0400, 0x0481),
    (0x048A, 0x052F),
    (0x0531, 0x0556),
    (0x0560, 0x0588),
    (0x05D0, 0x05EA),
    (0x05EF, 0x05F2),
    (0xFB1D, 0xFB4F),
    (0x0620, 0x064A),
    (0x066E, 0x066F),
    (0x0671, 0x06D3),
    (0x06D5, 0x06D5),
    (0x06E5, 0x06E6),
    (0x06EE, 0x06EF),
    (0x06FA, 0x06FC),
    (0x06FF, 0x06FF),
    (0x0750, 0x077F),
    (0x08A0, 0x08C9),
    (0xFB50, 0xFDF9),
    (0xFE70, 0xFEFC),
    (0x10A0, 0x10C5),
    (0x10D0, 0x10FF),
    (0x1C90, 0x1CBF),
    (0x2D00, 0x2D25),
    (0x1200, 0x135A),
    (0x1380, 0x138F),
    (0x2D80, 0x2DDE),
    (0x13A0, 0x13F5),
    (0x13F8, 0x13FD),
    (0xAB70, 0xABBF),
    (0x2D30, 0x2D67),
    (0x3041, 0x3096),
    (0x30A1, 0x30FA),
    (0x3105, 0x312F),
    (0x31A0, 0x31BF),
    (0x3400, 0x4DBF),
    (0x4E00, 0x9FFF),
    (0xF900, 0xFAFF),
    (0x1100, 0x11FF),
    (0x3131, 0x318E),
    (0xA960, 0xA97F),
    (0xAC00, 0xD7FF),
    (0x20000, 0x2EBEF),
    (0x2F800, 0x2FA1F),
    (0x30000, 0x323AF),
];
const NUMBER_RANGES: &[(u32, u32)] = &[
    (0x0030, 0x0039),
    (0x0660, 0x0669),
    (0x06F0, 0x06F9),
    (0x0966, 0x096F),
    (0xFF10, 0xFF19),
];
const MARK_RANGES: &[(u32, u32)] = &[
    (0x0483, 0x0489),
    (0x0591, 0x05BD),
    (0x05BF, 0x05BF),
    (0x05C1, 0x05C2),
    (0x05C4, 0x05C5),
    (0x05C7, 0x05C7),
    (0x0610, 0x061A),
    (0x064B, 0x065F),
    (0x0670, 0x0670),
    (0x06D6, 0x06DC),
    (0x06DF, 0x06E4),
    (0x06E7, 0x06E8),
    (0x06EA, 0x06ED),
    (0x08D3, 0x08FF),
    (0xFB1E, 0xFB1E),
];
fn in_ranges(cp: u32, ranges: &[(u32, u32)]) -> bool {
    ranges.iter().any(|&(a, b)| cp >= a && cp <= b)
}
fn mark(cp: u32) -> bool {
    text::combining(cp) || in_ranges(cp, MARK_RANGES)
}
fn letter(cp: u32) -> bool {
    !mark(cp)
        && !in_ranges(cp, NUMBER_RANGES)
        && !matches!(cp, 0xd7 | 0xf7 | 0x374 | 0x375 | 0x37e | 0x387 | 0x3f6)
        && in_ranges(cp, LETTER_RANGES)
}
// Preserve the shared C++ decoder's three-byte fast path on malformed input.
fn next(bytes: &[u8], pos: &mut usize) -> Option<u32> {
    let lead = *bytes.get(*pos)?;
    if (0xe0..0xf0).contains(&lead) {
        let mut cp = u32::from(lead & 15);
        for i in 1..3 {
            let b = bytes.get(*pos + i).copied().unwrap_or(0);
            if b & 0xc0 != 0x80 {
                break;
            }
            cp = (cp << 6) | u32::from(b & 63);
        }
        if cp >= 0x800 && !(0xd800..=0xdfff).contains(&cp) {
            *pos += 3;
            return Some(cp);
        }
    }
    utf8::next(bytes, pos)
}
fn mapped(cp: u32) -> Option<&'static [u8]> {
    Some(match cp {
        0xd8 | 0xf8 => b"o",
        0xc6 | 0xe6 => b"ae",
        0x152 | 0x153 => b"oe",
        0xdf => b"ss",
        0x141 | 0x142 => b"l",
        0xd0 | 0xf0 | 0x110 | 0x111 => b"d",
        0xde | 0xfe => b"th",
        0x131 => b"i",
        0x27 | 0x2018 | 0x2019 => b"'",
        _ => return None,
    })
}
pub fn fold(buf: &mut [u8]) -> usize {
    let (mut read, mut write) = (0, 0);
    let mut space = false;
    while read < buf.len() {
        let n = match buf[read] {
            0xc0..=0xdf => 2,
            0xe0..=0xef => 3,
            0xf0..=0xf7 => 4,
            _ => 1,
        };
        if n > buf.len() - read {
            break;
        }
        let Some(cp) = next(buf, &mut read) else {
            break;
        };
        if mark(cp) {
            continue;
        }
        let mut enc = [0; 4];
        let chunk = if let Some(s) = mapped(cp) {
            s
        } else {
            let mut base = cp;
            for _ in 0..4 {
                let b = text::decomposed_base(base);
                if b == 0 {
                    break;
                }
                base = b;
            }
            if base < 128 && (base as u8).is_ascii_alphanumeric() {
                enc[0] = (base as u8).to_ascii_lowercase();
                &enc[..1]
            } else if base >= 128 && (letter(base) || in_ranges(base, NUMBER_RANGES)) {
                let n = utf8::encode(base, &mut enc);
                &enc[..n]
            } else {
                if write > 0 {
                    space = true;
                }
                continue;
            }
        };
        if space && write > 0 {
            buf[write] = b' ';
            write += 1;
        }
        space = false;
        buf[write..write + chunk.len()].copy_from_slice(chunk);
        write += chunk.len();
    }
    write
}
pub fn initial(buf: &[u8]) -> u32 {
    let cp = next(buf, &mut 0).unwrap_or(0);
    if letter(cp) {
        cp
    } else {
        0
    }
}
pub fn clean(buf: &mut [u8]) -> usize {
    let (mut depth, mut w) = (0usize, 0usize);
    for i in 0..buf.len() {
        let c = buf[i];
        match c {
            b'[' | b'(' => {
                depth += 1;
                continue;
            }
            b']' | b')' => {
                depth = depth.saturating_sub(1);
                continue;
            }
            b';' => break,
            _ => {}
        }
        if depth > 0 {
            continue;
        }
        // Read the original previous byte before compacting can overwrite it.
        let c = if c == b'_' {
            if i > 0
                && i + 1 < buf.len()
                && buf[i - 1].is_ascii_alphabetic()
                && buf[i + 1].is_ascii_alphabetic()
            {
                b'.'
            } else {
                b' '
            }
        } else {
            c
        };
        buf[w] = c;
        w += 1;
    }
    while w > 0 && matches!(buf[w - 1], b' ' | b',' | b'-' | b'.' | b'_') {
        w -= 1;
    }
    let mut out = 0;
    let mut space = false;
    for i in 0..w {
        if buf[i] == b' ' {
            space = true;
            continue;
        }
        if space && out > 0 {
            buf[out] = b' ';
            out += 1;
        }
        space = false;
        buf[out] = buf[i];
        out += 1;
    }
    let b = &mut buf[..out];
    if let Some(comma) = b.iter().position(|&c| c == b',') {
        if !b[comma + 1..].contains(&b',') {
            let mut first = comma + 1;
            while first < out && b[first] == b' ' {
                first += 1;
            }
            let mut last = comma;
            while last > 0 && b[last - 1] == b' ' {
                last -= 1;
            }
            if first < out && last > 0 {
                b.copy_within(first..out, last + 1);
                let n = last + 1 + out - first;
                b[last] = b' ';
                b[..last].reverse();
                b[last + 1..n].reverse();
                b[..n].reverse();
                return n;
            }
        }
    }
    out
}
pub fn author(buf: &mut [u8]) -> usize {
    let n = clean(buf);
    let n = fold(&mut buf[..n]);
    let mut tokens = [(0usize, 0usize); 12];
    let mut count = 0;
    let mut start = 0;
    while start < n && count < 12 {
        while start < n && buf[start] == b' ' {
            start += 1;
        }
        let mut end = start;
        while end < n && buf[end] != b' ' {
            end += 1;
        }
        if end > start {
            tokens[count] = (start, end);
            count += 1;
        }
        start = end;
    }
    let mut kept = 0;
    for i in 0..count {
        let (a, b) = tokens[i];
        let mut pos = 0;
        next(&buf[a..b], &mut pos);
        if pos != b - a {
            tokens[kept] = (a, b);
            kept += 1;
        }
    }
    for i in 1..kept {
        let mut j = i;
        while j > 0 {
            let (a, b) = tokens[j - 1];
            let (c, d) = tokens[j];
            if buf[a..b] <= buf[c..d] {
                break;
            }
            tokens.swap(j - 1, j);
            j -= 1;
        }
    }
    let mut key = [0; 12];
    let mut w = 0;
    let mut total = 0;
    for &(a, b) in &tokens[..kept] {
        if total > 0 {
            if w < 12 {
                key[w] = b' ';
                w += 1;
            }
            total += 1;
        }
        let copy = (b - a).min(12 - w);
        key[w..w + copy].copy_from_slice(&buf[a..a + copy]);
        w += copy;
        total += b - a;
    }
    if total > 12 {
        w = utf8::safe_truncate(&key);
    }
    while w > 0 && key[w - 1] == b' ' {
        w -= 1;
    }
    buf[..w].copy_from_slice(&key[..w]);
    w
}
pub fn surname(buf: &mut [u8]) -> usize {
    let n = fold(buf);
    if let Some(at) = buf[..n].iter().rposition(|&c| c == b' ') {
        buf[..at].reverse();
        buf[at + 1..n].reverse();
        buf[..n].reverse();
    }
    n
}
pub fn matches(haystack: &[u8], needle: &[u8]) -> bool {
    let split = |&b: &u8| b == b' ' || b == b'\'';
    needle
        .split(split)
        .filter(|w| !w.is_empty())
        .all(|w| haystack.split(split).any(|h| h.starts_with(w)))
}
