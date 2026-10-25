//! Discretionary splitting with renderer-supplied prefix and suffix advances.
use super::{layout::Widths, Tokens, Word, WordStore};
use core::ffi::c_void;

#[repr(C)]
pub struct SplitRequest {
    pub context: *mut c_void,
    pub measure: unsafe extern "C" fn(*mut c_void, *const u8, usize, u8, u8, u8) -> u16,
    pub boundary: unsafe extern "C" fn(u32, u32) -> u8,
    pub inserted: unsafe extern "C" fn(*mut c_void, usize),
    pub index: usize,
    pub available: i32,
    pub language: i32,
    pub style: u8,
    pub fallback: u8,
}
#[repr(C)]
#[derive(Default)]
pub struct SplitResult {
    pub offset: usize,
    pub width: i32,
    pub remainder_offset: u32,
    pub remainder_width: u16,
    pub hyphen: u8,
}

use crate::text::hangul;

unsafe fn choose(word: &[u8], focus: u8, r: &SplitRequest) -> Option<(usize, i32, bool)> {
    let mut chosen = None;
    let mut best = -1;
    let mut candidate = |offset: usize, hyphen: bool| {
        if offset == 0 || offset >= word.len() {
            return;
        }
        let width = i32::from((r.measure)(
            r.context,
            word.as_ptr(),
            offset,
            r.style,
            usize::from(focus).min(offset) as u8,
            u8::from(hyphen),
        ));
        if width > r.available || width <= best {
            return;
        }
        best = width;
        chosen = Some((offset, width, hyphen));
    };
    let mut position = 0;
    if let Some(mut previous) = crate::utf8::next(word, &mut position) {
        loop {
            let offset = position;
            let Some(current) = crate::utf8::next(word, &mut position) else {
                break;
            };
            if (hangul(previous) || hangul(current)) && (r.boundary)(previous, current) != 0 {
                candidate(offset, false);
            }
            previous = current;
        }
    }
    // Hangul candidates precede hyphenation, retaining the first equal-width choice.
    crate::hyphen::breaks(r.language, word, r.fallback != 0, &mut |b| {
        candidate(b.byte_offset as usize, b.insert_hyphen != 0);
    });
    chosen
}

/// # Safety
/// Owners are live, exclusive and disjoint. Callbacks remain valid throughout the
/// call, borrow text only for their invocation, and never access these owners.
/// inserted updates only C++ ruby metadata. result is disjoint writable storage.
pub unsafe fn split(
    store: &mut WordStore,
    tokens: &mut Tokens,
    widths: &mut Widths,
    request: &SplitRequest,
    result: &mut SplitResult,
) -> bool {
    let r = request;
    if r.available <= 0 || r.index >= tokens.len() {
        return false;
    }
    let original = tokens.get(r.index).unwrap();
    let Some(word) = store.view(original.word) else {
        return false;
    };
    let Some((offset, width, hyphen)) = choose(word, original.focus, r) else {
        return false;
    };
    if offset + 1 > 208 {
        return false;
    }
    let mut position = 0;
    let mut prefix_length = 0;
    while position < offset {
        if crate::utf8::next(word, &mut position).is_none() {
            return false;
        }
        prefix_length += 1;
    }
    let remainder_offset = tokens.source_after(tokens.offset(r.index), prefix_length);
    if !widths.reserve(widths.len + 1) {
        return false;
    }
    let mut prefix_buf = [0u8; 208];
    prefix_buf[..offset].copy_from_slice(&word[..offset]);
    let prefix_len = offset + usize::from(hyphen);
    if hyphen {
        prefix_buf[offset] = b'-';
    }
    let Some(prefix_word) = store.append(&prefix_buf[..prefix_len]) else {
        return false;
    };
    let remainder_word = Word {
        chunk: original.word.chunk,
        off: original.word.off + offset as u16,
        len: original.word.len - offset as u16,
    };
    let mut prefix = original;
    let mut remainder = original;
    let style = r.style & !128;
    remainder.word = remainder_word;
    remainder.style = style;
    remainder.focus = original.focus.saturating_sub(offset.min(255) as u8);
    remainder.flags = 0;
    if !tokens.insert(r.index + 1, remainder, remainder_offset) {
        store.release(prefix_word);
        return false;
    }
    prefix.word = prefix_word;
    prefix.style = style | if hyphen { 128 } else { 0 };
    prefix.focus = usize::from(original.focus).min(offset) as u8;
    if usize::from(prefix.focus) + usize::from(hyphen) >= usize::from(prefix.word.len) {
        prefix.style |= 1;
        prefix.focus = 0;
    }
    tokens.set(r.index, prefix);
    (r.inserted)(r.context, r.index + 1);
    *widths.data.add(r.index) = width as u16;
    let remainder = store.view(remainder_word).unwrap();
    let remainder_width = (r.measure)(
        r.context,
        remainder.as_ptr(),
        remainder.len(),
        r.style,
        tokens.get(r.index + 1).unwrap().focus,
        0,
    );
    // reserve succeeded before token mutation, so insertion cannot allocate or fail.
    widths.insert(r.index + 1, remainder_width);
    *result = SplitResult {
        offset,
        width,
        remainder_offset,
        remainder_width,
        hyphen: u8::from(hyphen),
    };
    true
}
