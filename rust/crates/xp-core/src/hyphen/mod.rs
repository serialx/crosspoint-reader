//! Allocation-free Liang hyphenation with firmware-compatible byte offsets.
mod compose;
mod data;
mod predicates;
mod trie;
use predicates::*;

#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Break {
    pub byte_offset: u32,
    pub insert_hyphen: u8,
    pub pad: [u8; 3],
}

/// Stable language identifiers; -1 denotes no pattern language.
pub fn language(tag: &[u8]) -> i32 {
    let end = tag
        .iter()
        .position(|&c| c == b'-' || c == b'_')
        .unwrap_or(tag.len());
    let primary = &tag[..end];
    const TAGS: [&[u8]; 11] = [
        b"en", b"fr", b"de", b"ru", b"es", b"it", b"pl", b"sv", b"uk", b"fi", b"pt",
    ];
    const ALIASES: [(&[u8], i32); 11] = [
        (b"eng", 0),
        (b"fra", 1),
        (b"fre", 1),
        (b"deu", 2),
        (b"ger", 2),
        (b"rus", 3),
        (b"spa", 4),
        (b"ita", 5),
        (b"ukr", 8),
        (b"swe", 7),
        (b"fin", 9),
    ];
    for (i, value) in TAGS.iter().enumerate() {
        if primary.eq_ignore_ascii_case(value) {
            return i as i32;
        }
    }
    for (value, id) in ALIASES {
        if primary.eq_ignore_ascii_case(value) {
            return id;
        }
    }
    -1
}

#[derive(Clone)]
struct Codepoints<'a> {
    bytes: &'a [u8],
    position: usize,
    end: usize,
}
impl<'a> Codepoints<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Self {
            bytes,
            position: 0,
            end: bytes.len(),
        }
    }
}
impl Iterator for Codepoints<'_> {
    type Item = (u32, usize);
    fn next(&mut self) -> Option<Self::Item> {
        if self.position >= self.end {
            return None;
        }
        let offset = self.position;
        let mut cp = crate::utf8::next(self.bytes, &mut self.position)?;
        loop {
            if self.position >= self.end {
                break;
            }
            let mut pos = self.position;
            let Some(mark) = crate::utf8::next(self.bytes, &mut pos) else {
                break;
            };
            let Some(composed) = compose::compose(cp, mark) else {
                break;
            };
            cp = composed;
            self.position = pos;
        }
        Some((cp, offset))
    }
}

fn trimmed(bytes: &[u8]) -> Codepoints<'_> {
    let all = Codepoints::new(bytes);
    let mut count = 0;
    let mut previous = (0, 0);
    let mut digits_start = None;
    let mut footnote = None;
    for (cp, offset) in all.clone() {
        count += 1;
        if (0x30..=0x39).contains(&cp) {
            if !(0x30..=0x39).contains(&previous.0) {
                digits_start = if previous.0 == 0x5b {
                    Some(previous.1)
                } else {
                    None
                };
            }
            footnote = digits_start;
        } else if !punctuation(cp) {
            footnote = None;
        }
        previous = (cp, offset);
    }
    let mut range = all;
    if count >= 3 {
        if let Some(end) = footnote {
            range.end = end;
        }
    }
    let mut first = range.end;
    let mut end = first;
    let mut scan = range.clone();
    while let Some((cp, offset)) = scan.next() {
        if !punctuation(cp) {
            first = first.min(offset);
            end = scan.position;
        }
    }
    range.position = first;
    range.end = end;
    range
}

fn emit(out: &mut dyn FnMut(Break), offset: usize, insert: bool) {
    out(Break {
        byte_offset: offset as u32,
        insert_hyphen: u8::from(insert),
        pad: [0; 3],
    });
}

fn segment_breaks(
    cps: Codepoints<'_>,
    lang: i32,
    fallback: bool,
    segmented: bool,
    out: &mut dyn FnMut(Break),
) {
    let scores = trie::scores(cps.clone(), lang);
    let min = if lang == 0 { 3 } else { 2 };
    let count = cps.clone().count();
    let has_pattern = (min..count.saturating_sub(min).saturating_add(1))
        .any(|i| scores.get(i + 1).is_some_and(|s| s & 1 != 0));
    let mut previous = 0;
    for (i, (cp, offset)) in cps.enumerate() {
        if i >= min
            && count - i >= min
            && ((has_pattern && scores.get(i + 1).is_some_and(|s| s & 1 != 0))
                || (!has_pattern && fallback))
        {
            emit(out, offset, segmented || !(cjk(cp) || cjk(previous)));
        }
        previous = cp;
    }
}

/// Emits ordered, distinct breaks. Input length must fit u32 (checked by the ABI).
/// The callback borrows each value only for the duration of the call.
pub fn breaks(lang: i32, word: &[u8], fallback: bool, out: &mut dyn FnMut(Break)) {
    let cps = trimmed(word);
    let mut has_explicit = false;
    let mut has_apostrophe = false;
    let mut previous = 0;
    let mut scan = cps.clone();
    while let Some((cp, _)) = scan.next() {
        has_apostrophe |= apostrophe(cp);
        has_explicit |= explicit(cp)
            && alphabetic(previous)
            && scan
                .clone()
                .next()
                .is_some_and(|(next, _)| alphabetic(next));
        previous = cp;
    }
    if !has_explicit && !has_apostrophe {
        segment_breaks(cps, lang, fallback, false, out);
        return;
    }
    let mut scan = cps.clone();
    let mut segment = cps;
    let mut left_letters = 0;
    previous = 0;
    while let Some((cp, offset)) = scan.next() {
        if !separator(cp) {
            left_letters += usize::from(alphabetic(cp));
            previous = cp;
            continue;
        }
        segment.end = offset;
        if lang >= 0 {
            segment_breaks(segment.clone(), lang, fallback && !has_explicit, true, out);
        }
        let next = scan.clone().next();
        if let Some((next_cp, next_offset)) = next {
            if alphabetic(previous) && alphabetic(next_cp) {
                if explicit(cp) {
                    emit(out, next_offset, cp == 0xad);
                } else if left_letters >= 3 {
                    let right_letters = scan
                        .clone()
                        .take_while(|(c, _)| !separator(*c))
                        .filter(|(c, _)| alphabetic(*c))
                        .count();
                    if right_letters >= 3 {
                        emit(out, next_offset, false);
                    }
                }
            }
        }
        segment = scan.clone();
        left_letters = 0;
        previous = cp;
    }
    if lang >= 0 {
        segment_breaks(segment, lang, fallback && !has_explicit, true, out);
    }
}
