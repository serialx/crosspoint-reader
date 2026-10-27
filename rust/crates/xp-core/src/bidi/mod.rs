//! Allocation-free compatibility with CrossPoint's pinned MiniBidi and BidiUtils.
//! The caller owns and reuses the workspace; no global state or pointers survive a call.
mod algorithm;
mod tables;
use crate::utf8;
use tables::*;

pub const MAX_LINE: usize = 128;
const L: u8 = 0;
const LRE: u8 = 1;
const LRO: u8 = 2;
const R: u8 = 3;
const AL: u8 = 4;
const RLE: u8 = 5;
const RLO: u8 = 6;
const PDF: u8 = 7;
const EN: u8 = 8;
const ES: u8 = 9;
const ET: u8 = 10;
const AN: u8 = 11;
const CS: u8 = 12;
const NSM: u8 = 13;
const BN: u8 = 14;
const B: u8 = 15;
const S: u8 = 16;
const WS: u8 = 17;
const ON: u8 = 18;
const LRI: u8 = 19;
const RLI: u8 = 20;
const FSI: u8 = 21;
const PDI: u8 = 22;
const SL: u8 = 0;
const SR: u8 = 1;
const SD: u8 = 2;
const SU: u8 = 3;
const SC: u8 = 4;

#[repr(C)]
#[derive(Clone, Copy)]
struct Character {
    pub original: u32,
    pub wc: u32,
    pub index: u16,
    pub joiners: u8,
    pub reserved: u8,
}
#[repr(C)]
struct Scratch {
    types: [u8; MAX_LINE],
    levels: [u8; MAX_LINE],
    skip: [u8; MAX_LINE],
    stack: [[u8; 3]; MAX_LINE + 1],
    openers: [[u32; 2]; 63],
}
#[repr(C)]
pub struct Workspace {
    line: [Character; MAX_LINE],
    shaped: [Character; MAX_LINE],
    scratch: Scratch,
    first_any: [u16; MAX_LINE],
    first_natural: [u16; MAX_LINE],
}

impl Workspace {
    /// Initialize caller-owned persistent storage; avoid task-stack temporaries.
    pub const EMPTY: Self = Self {
        line: [Character {
            original: 0,
            wc: 0,
            index: 0,
            joiners: 0,
            reserved: 0,
        }; MAX_LINE],
        shaped: [Character {
            original: 0,
            wc: 0,
            index: 0,
            joiners: 0,
            reserved: 0,
        }; MAX_LINE],
        scratch: Scratch {
            types: [0; MAX_LINE],
            levels: [0; MAX_LINE],
            skip: [0; MAX_LINE],
            stack: [[0; 3]; MAX_LINE + 1],
            openers: [[0; 2]; 63],
        },
        first_any: [0; MAX_LINE],
        first_natural: [0; MAX_LINE],
    };
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Status {
    Unchanged = 0,
    Applied = 1,
    BufferTooSmall = -1,
    InvalidArgument = -2,
}

fn range_type(cp: u32, table: &[(u32, u32, u8)], fallback: u8) -> u8 {
    let i = table.partition_point(|row| row.1 < cp);
    table
        .get(i)
        .filter(|row| row.0 <= cp)
        .map_or(fallback, |row| row.2)
}
pub fn class(cp: u32) -> u8 {
    range_type(cp, CLASSES, ON)
}
pub fn is_transparent_mark(cp: u32) -> bool {
    cp >= 0x591 && class(cp) == NSM
}
pub fn mirror(cp: u32) -> u32 {
    PAIRS
        .binary_search_by_key(&cp, |r| r.0)
        .map_or(cp, |i| PAIRS[i].1)
}
fn bracket(cp: u32) -> u32 {
    PAIRS
        .binary_search_by_key(&cp, |r| r.0)
        .map_or(0, |i| match PAIRS[i].2 {
            1 => cp,
            2 => PAIRS[i].1,
            _ => 0,
        })
}
fn shape_type(cp: u32) -> u8 {
    if (0x621..=0x64a).contains(&cp) {
        SHAPES[(cp - 0x621) as usize].0
    } else {
        range_type(cp, JOINS, SU)
    }
}
fn shape_form(cp: u32, form: u8) -> u32 {
    if (0x621..=0x64a).contains(&cp) {
        let base = SHAPES[(cp - 0x621) as usize].1;
        return if base != 0 {
            0xfe00 + u32::from(base) + u32::from(form)
        } else {
            cp
        };
    }
    FORMS.binary_search_by_key(&cp, |r| r.0).map_or(cp, |i| {
        if form < FORMS[i].2 {
            FORMS[i].1 + u32::from(form)
        } else {
            cp
        }
    })
}
fn shape(line: &[Character], out: &mut [Character]) {
    for i in 0..line.len() {
        out[i] = line[i];
        let cp = line[i].wc;
        let t = shape_type(cp);
        if !matches!(t, SR | SD) {
            continue;
        }
        let nx = (i + 1..line.len()).find(|&j| class(line[j].wc) != NSM);
        let next = nx.map_or(SU, |j| shape_type(line[j].wc));
        let right = matches!(next, SL | SD | SC);
        if t == SR {
            out[i].wc = shape_form(cp, u8::from(right));
            continue;
        }
        let pv = (0..i).rev().find(|&j| class(line[j].wc) != NSM);
        if cp == 0x644 {
            if let Some(j) = pv {
                let ligature = match line[j].wc {
                    0x622 => 0xfef5,
                    0x623 => 0xfef7,
                    0x625 => 0xfef9,
                    0x627 => 0xfefb,
                    _ => 0,
                };
                if ligature != 0 {
                    out[i].wc = ligature + u32::from(right);
                    out[j].wc = 0xffff;
                    continue;
                }
            }
        }
        let prev = pv.map_or(SU, |j| shape_type(line[j].wc));
        let left = matches!(prev, SR | SD | SC);
        let joiners = line[i].joiners & 15;
        let prev_joiners = line[i].joiners >> 4;
        let form = match prev_joiners {
            1 => {
                if left {
                    2
                } else {
                    0
                }
            }
            3 => 3,
            2 => 1,
            _ if joiners & 1 != 0 => u8::from(right),
            _ => u8::from(right) + 2 * u8::from(left),
        };
        out[i].wc = shape_form(cp, form);
    }
}

pub fn detect_paragraph_level(text: &[u8], fallback: i32, depth: i32) -> i32 {
    let mut pos = 0;
    for _ in 0..depth {
        let Some(cp) = utf8::next(text, &mut pos) else {
            break;
        };
        if cp == 0xfffd {
            break;
        }
        match class(cp) {
            R | AL => return 1,
            L => return 0,
            _ => {}
        }
    }
    fallback & 1
}
pub fn starts_with_rtl(text: &[u8], depth: i32) -> bool {
    detect_paragraph_level(text, 0, depth) != 0
}

/// Output is unchanged on failure. Success returns its byte length (without NUL).
/// At most 128 decoded codepoints are processed, just as in BidiUtils.
pub fn apply_visual(
    text: &[u8],
    paragraph: i32,
    work: &mut Workspace,
    out: &mut [u8],
) -> (Status, usize) {
    let mut count = 0;
    let mut position = 0;
    let mut last_base: Option<usize> = None;
    let mut pending = 0;
    while text.get(position).is_some_and(|&b| b != 0) {
        // Check before decoding: even an invalid 129th character declines the call.
        if count == MAX_LINE {
            return (Status::Unchanged, 0);
        }
        let Some(cp) = utf8::next(text, &mut position) else {
            break;
        };
        if cp == 0xfffd {
            break;
        }
        work.line[count] = Character {
            original: cp,
            wc: cp,
            index: count as u16,
            joiners: 0,
            reserved: 0,
        };
        if matches!(cp, 0x200c | 0x200d) {
            let joiner = if cp == 0x200d { 2 } else { 1 };
            if let Some(i) = last_base {
                work.line[i].joiners |= joiner;
            }
            pending |= joiner;
        } else {
            work.line[count].joiners = pending << 4;
            pending = 0;
            last_base = Some(count);
        }
        count += 1;
    }
    if count == 0 {
        return (Status::Unchanged, 0);
    }
    algorithm::reorder(
        &mut work.line[..count],
        paragraph < 0,
        if paragraph < 0 {
            0
        } else {
            (paragraph & 1) as u8
        },
        &mut work.scratch,
    );
    shape(&work.line[..count], &mut work.shaped[..count]);
    let shaped = &work.shaped[..count];
    let mut required = 0;
    emit_visual(shaped, &mut |cp| {
        required += utf8::encode(cp, &mut [0; 4]);
    });
    if out.len() < required {
        return (Status::BufferTooSmall, required);
    }
    let mut written = 0;
    emit_visual(shaped, &mut |cp| {
        let mut bytes = [0; 4];
        let n = utf8::encode(cp, &mut bytes);
        out[written..written + n].copy_from_slice(&bytes[..n]);
        written += n;
    });
    (Status::Applied, written)
}
fn filtered(cp: u32) -> bool {
    matches!(cp, 0xffff | 0x200c | 0x200d)
}
fn emit_visual(shaped: &[Character], emit: &mut impl FnMut(u32)) {
    let count = shaped.len();
    let mut i = 0;
    while i < count {
        let cp = shaped[i].wc;
        if filtered(cp) {
            i += 1;
            continue;
        }
        let mut end = if cp == 0x5d4 || is_transparent_mark(cp) {
            i
        } else {
            count
        };
        let mut matched = 0;
        while end < count && matched < 4 {
            if !is_transparent_mark(shaped[end].wc) {
                matched = if shaped[end].wc == [0x5d4, 0x5d5, 0x5d4, 0x5d9][matched] {
                    matched + 1
                } else {
                    5
                };
            }
            end += 1;
        }
        if matched == 4 {
            emit(0x27);
            emit(0x5d4);
            i = end;
            continue;
        }
        if !is_transparent_mark(cp) {
            emit(cp);
            i += 1;
            continue;
        }
        let mut j = i;
        while j < count && (filtered(shaped[j].wc) || is_transparent_mark(shaped[j].wc)) {
            j += 1;
        }
        if j < count && shaped[j].index < shaped[i].index {
            emit(shaped[j].wc);
            for ch in shaped[i..j].iter().rev() {
                if is_transparent_mark(ch.wc) {
                    emit(ch.wc);
                }
            }
            i = j + 1;
        } else {
            for ch in &shaped[i..j] {
                if is_transparent_mark(ch.wc) {
                    emit(ch.wc);
                }
            }
            i = j;
        }
    }
}

/// Packed NUL-terminated words, matching the layout callback. On Unchanged,
/// output may contain partial anchors, as in the C++ span overload; ignore it.
pub fn visual_word_order(
    text: &[u8],
    offsets: &[u16],
    rtl: bool,
    work: &mut Workspace,
    out: &mut [u16],
) -> Status {
    let words = offsets.len();
    if out.len() != words {
        return Status::BufferTooSmall;
    }
    for &offset in offsets {
        if !text.get(offset as usize..).is_some_and(|s| s.contains(&0)) {
            return Status::InvalidArgument;
        }
    }
    if words <= 1 || words > MAX_LINE {
        return Status::Unchanged;
    }
    let mut count = 0;
    for (w, &offset) in offsets.iter().enumerate() {
        let mut pos = offset as usize;
        while text[pos] != 0 {
            if count == MAX_LINE {
                return Status::Unchanged;
            }
            let Some(cp) = utf8::next(text, &mut pos) else {
                break;
            };
            if cp == 0xfffd {
                break;
            }
            work.line[count] = Character {
                original: cp,
                wc: cp,
                index: w as u16,
                joiners: 0,
                reserved: 0,
            };
            count += 1;
        }
        if w + 1 < words {
            if count == MAX_LINE {
                return Status::Unchanged;
            }
            work.line[count] = Character {
                original: 32,
                wc: 32,
                index: words as u16,
                joiners: 0,
                reserved: 0,
            };
            count += 1;
        }
    }
    if count == 0 {
        return Status::Unchanged;
    }
    let line = &mut work.line[..count];
    let has_l = line.iter().any(|c| matches!(class(c.wc), L | EN | AN));
    let has_r = line.iter().any(|c| matches!(class(c.wc), R | AL));
    if !has_l && has_r && rtl {
        for (i, slot) in out.iter_mut().enumerate() {
            *slot = (words - i - 1) as u16;
        }
        return Status::Applied;
    }
    if !has_r {
        if !rtl {
            return Status::Unchanged;
        }
        for (i, slot) in out.iter_mut().enumerate() {
            *slot = i as u16;
        }
        return Status::Applied;
    }
    algorithm::reorder(line, false, u8::from(rtl), &mut work.scratch);
    work.first_any[..words].fill(u16::MAX);
    work.first_natural[..words].fill(u16::MAX);
    for (i, ch) in line.iter().enumerate() {
        let w = ch.index as usize;
        if w >= words {
            continue;
        }
        if work.first_any[w] == u16::MAX {
            work.first_any[w] = i as u16;
        }
        if work.first_natural[w] == u16::MAX && matches!(class(ch.wc), L | R | AL | EN | AN) {
            work.first_natural[w] = i as u16;
        }
    }
    let mut written = 0;
    for (i, ch) in line.iter().enumerate() {
        let w = ch.index as usize;
        if w >= words {
            continue;
        }
        let anchor = if work.first_natural[w] != u16::MAX {
            work.first_natural[w]
        } else {
            work.first_any[w]
        };
        if anchor == u16::MAX {
            return Status::Unchanged;
        }
        if anchor == i as u16 {
            out[written] = w as u16;
            written += 1;
        }
    }
    if written != words || out.iter().enumerate().all(|(i, &w)| i == w as usize) {
        Status::Unchanged
    } else {
        Status::Applied
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn short_output_and_limit_leave_output_unchanged() {
        let mut work = Workspace::EMPTY;
        let mut out = [0xa5; 8];
        assert_eq!(
            apply_visual("ب".as_bytes(), -1, &mut work, &mut out[..2]),
            (Status::BufferTooSmall, 3)
        );
        assert_eq!(out, [0xa5; 8]);
        assert_eq!(
            apply_visual(&[b'a'; 129], -1, &mut work, &mut out),
            (Status::Unchanged, 0)
        );
        assert_eq!(out, [0xa5; 8]);
        assert_eq!(
            apply_visual(b"a\xff", -1, &mut work, &mut out),
            (Status::Applied, 1)
        );
        assert_eq!(out[0], b'a');
    }

    #[test]
    fn independent_workspaces_and_packed_validation() {
        let mut first = Workspace::EMPTY;
        let mut second = Workspace::EMPTY;
        let mut a = [0; 32];
        let mut b = [0; 32];
        let text = "لَا".as_bytes();
        let result = apply_visual(text, -1, &mut first, &mut a);
        assert_eq!(result, apply_visual(text, -1, &mut second, &mut b));
        assert_eq!(a, b);
        assert_eq!(&a[..result.1], "ﻻَ".as_bytes());
        let mut out = [99; 2];
        assert_eq!(
            visual_word_order(b"a\0b\0", &[0, 2], true, &mut first, &mut out),
            Status::Applied
        );
        assert_eq!(out, [0, 1]);
        assert_eq!(
            visual_word_order(b"a\0b", &[0, 2], true, &mut first, &mut out),
            Status::InvalidArgument
        );
    }
}
