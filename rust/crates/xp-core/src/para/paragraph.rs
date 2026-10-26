//! Paragraph ownership and token insertion. Borrowed text never survives a call.
extern crate alloc;
use super::{Allocator, Token, Tokens, WordStore};
use crate::{chapter::style::Block, text, utf8};
use alloc::vec::Vec;
use core::{ffi::c_void, ptr};

#[derive(Default)]
pub struct Bytes {
    inline: [u8; 16],
    len: usize,
    heap: Vec<u8>,
}
impl Bytes {
    pub fn bytes(&self) -> &[u8] {
        if self.heap.is_empty() {
            &self.inline[..self.len]
        } else {
            &self.heap[..self.len]
        }
    }
    pub fn set(&mut self, bytes: &[u8]) -> bool {
        if bytes.len() < self.inline.len() && self.heap.capacity() == 0 {
            self.inline[..bytes.len()].copy_from_slice(bytes);
            self.inline[bytes.len()] = 0;
        } else {
            let Some(need) = bytes.len().checked_add(1) else {
                return false;
            };
            if self
                .heap
                .try_reserve(need.saturating_sub(self.heap.len()))
                .is_err()
            {
                return false;
            }
            self.heap.clear();
            self.heap.extend_from_slice(bytes);
            self.heap.push(0);
        }
        self.len = bytes.len();
        true
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Span {
    pub data: *const u8,
    pub len: u32,
}
impl Span {
    pub fn from_bytes(bytes: &[u8]) -> Self {
        Self {
            data: bytes.as_ptr(),
            len: bytes.len() as u32,
        }
    }
}
#[repr(C)]
pub struct Metrics {
    pub context: *mut c_void,
    pub trace: Option<unsafe extern "C" fn(*mut c_void, Span)>,
    pub advance: unsafe extern "C" fn(*mut c_void, i32, Span, u8, i8) -> i32,
    pub space: unsafe extern "C" fn(*mut c_void, i32, u8) -> i32,
    pub kerning: unsafe extern "C" fn(*mut c_void, i32, u32, u32, u8, i8) -> i32,
    pub space_advance: unsafe extern "C" fn(*mut c_void, i32, u32, u32, u8) -> i32,
    pub ascender: unsafe extern "C" fn(*mut c_void, i32) -> i32,
    pub is_sd: unsafe extern "C" fn(*mut c_void, i32) -> u8,
    pub prepare: unsafe extern "C" fn(*mut c_void, i32, *const Span, u32, u8, u8, u8, u8),
    pub starts_rtl: unsafe extern "C" fn(Span, i32) -> u8,
    pub visual_order: unsafe extern "C" fn(Span, *const u16, u32, u8, *mut u16) -> u8,
}

pub struct Paragraph {
    pub starts_rtl: unsafe extern "C" fn(Span, i32) -> u8,
    pub store: WordStore,
    pub tokens: Tokens,
    pub block: Block,
    pub links: Vec<Bytes>,
    pub ruby: Vec<Bytes>,
    pub scales: [f32; 4],
    pub fonts: [i32; 4],
    pub slots: u8,
    pub spacing: u8,
    pub indent: u8,
    pub hyphenation: bool,
    pub focus: bool,
    pub natural: bool,
    pub has_rtl: bool,
    pub dropped: bool,
    pub first_consumed: bool,
    pub paragraph_emitted: bool,
    pub lines_after: i32,
    pub allocator: *const Allocator,
}
impl Paragraph {
    /// # Safety
    /// out is aligned uninitialized storage. allocator and callbacks outlive it.
    pub unsafe fn initialize(
        out: *mut Self,
        allocator: *const Allocator,
        block: Block,
        hyphenation: bool,
        focus: bool,
        indent: u8,
        starts_rtl: unsafe extern "C" fn(Span, i32) -> u8,
    ) {
        macro_rules! field {
            ($name:ident, $value:expr) => {
                ptr::addr_of_mut!((*out).$name).write($value);
            };
        }
        field!(starts_rtl, starts_rtl);
        field!(store, WordStore::new(allocator));
        field!(tokens, Tokens::new(allocator));
        field!(block, block);
        field!(links, Vec::new());
        field!(ruby, Vec::new());
        field!(scales, [1.0, 0.0, 0.0, 0.0]);
        field!(fonts, [0; 4]);
        field!(slots, 1);
        field!(spacing, 100);
        field!(indent, indent);
        field!(hyphenation, hyphenation);
        field!(focus, focus);
        field!(natural, false);
        field!(has_rtl, false);
        field!(dropped, false);
        field!(first_consumed, false);
        field!(paragraph_emitted, false);
        field!(lines_after, -1);
        field!(allocator, allocator);
    }
    pub fn word(&self, index: usize) -> &[u8] {
        self.tokens
            .get(index)
            .and_then(|t| self.store.view(t.word))
            .unwrap_or(&[])
    }
    pub fn style(&self, index: usize) -> u8 {
        self.tokens
            .get(index)
            .map_or(0, |t| (t.style & !128) | (t.slot & 128))
    }
    pub fn font(&self, index: usize, base: i32) -> i32 {
        let slot = usize::from(self.tokens.get(index).unwrap_or_default().slot & 127);
        self.fonts
            .get(slot)
            .copied()
            .filter(|id| slot != 0 && *id != 0)
            .unwrap_or(base)
    }
    pub fn size_slot(&mut self, scale: f32) -> u8 {
        let mut nearest = 1;
        for slot in 1..self.slots as usize {
            if (self.scales[slot] - scale).abs() < 0.01 {
                return slot as u8;
            }
            if (self.scales[slot] - scale).abs() < (self.scales[nearest] - scale).abs() {
                nearest = slot;
            }
        }
        if self.slots < 4 {
            let slot = self.slots;
            self.scales[slot as usize] = scale;
            self.slots += 1;
            slot
        } else {
            nearest as u8
        }
    }
    pub fn add_link(&mut self, href: &[u8]) -> u8 {
        if href.is_empty() || href.len() >= 256 || self.links.len() >= 255 {
            return 0;
        }
        let mut value = Bytes::default();
        if !value.set(href) || self.links.try_reserve(1).is_err() {
            self.dropped = true;
            return 0;
        }
        self.links.push(value);
        self.links.len() as u8
    }
    pub fn link_matches(&self, id: u8, href: &[u8]) -> bool {
        id != 0
            && self
                .links
                .get(usize::from(id) - 1)
                .is_some_and(|s| s.bytes() == href)
    }
    pub fn ruby_text(&self, index: usize) -> &[u8] {
        self.ruby.get(index).map_or(&[], Bytes::bytes)
    }
    fn grow_ruby(&mut self) -> bool {
        let len = self.tokens.len();
        if self
            .ruby
            .try_reserve(len.saturating_sub(self.ruby.len()))
            .is_err()
        {
            self.dropped = true;
            return false;
        }
        self.ruby.resize_with(len, Bytes::default);
        true
    }
    pub fn set_ruby(&mut self, index: usize, ruby: &[u8]) {
        if index >= self.tokens.len() {
            return;
        }
        if self.ruby.len() <= index && !self.grow_ruby() {
            return;
        }
        if !self.ruby[index].set(ruby) {
            self.dropped = true;
        }
    }
    pub fn set_ruby_group(&mut self, start: usize, count: usize, ruby: &[u8]) {
        self.set_ruby(start, ruby);
        for index in start.saturating_add(1)..start.saturating_add(count).min(self.tokens.len()) {
            self.set_ruby(index, &[]);
            let mut token = self.tokens.get(index).unwrap();
            token.style |= 64;
            token.flags = 1;
            self.tokens.set(index, token);
        }
    }
    fn reserve_tokens(&mut self, count: usize) {
        if !self
            .tokens
            .len()
            .checked_add(count)
            .is_some_and(|n| self.tokens.reserve(n))
        {
            self.dropped = true;
        }
    }
    #[allow(clippy::too_many_arguments)] // Preserves the existing token/metric operation.
    fn push(
        &mut self,
        text: &[u8],
        style: u8,
        flags: u8,
        focus: u8,
        offset: u32,
        link: u8,
        slot: u8,
        pad_ruby: bool,
    ) {
        if self.dropped {
            return;
        }
        let Some(word) = self.store.append(text) else {
            self.dropped = true;
            return;
        };
        let token = Token {
            word,
            style: style & !128,
            focus,
            link,
            slot: slot | (style & 128),
            flags,
        };
        if !self.tokens.insert(self.tokens.len(), token, offset) {
            self.store.release(word);
            self.dropped = true;
            return;
        }
        if pad_ruby && !self.ruby.is_empty() {
            if self.ruby.try_reserve(1).is_err() {
                self.dropped = true;
                return;
            }
            self.ruby.push(Bytes::default());
        }
    }
    #[allow(clippy::too_many_arguments)] // Preserves the existing token/metric operation.
    pub fn add_word(
        &mut self,
        word: &[u8],
        style: u8,
        underline: bool,
        attach: bool,
        offset: u32,
        link: u8,
        slot: u8,
        no_space: bool,
    ) {
        if word.is_empty() {
            return;
        }
        let mut normalized = Vec::new();
        let word = if utf8::may_compose(word) {
            if normalized.try_reserve_exact(word.len() + 1).is_err() {
                self.dropped = true;
                return;
            }
            let ok = utf8::compose(word, &mut |s| normalized.extend_from_slice(s), &mut |n| {
                self.tokens.record_absorbed(offset.wrapping_add(n))
            });
            if !ok {
                self.dropped = true;
            }
            let len = normalized.len();
            normalized.push(0);
            &normalized[..len]
        } else {
            word
        };
        if word.is_empty() {
            return;
        }
        let style = style | if underline { 4 } else { 0 };
        let rtl = !self.has_rtl
            && word.iter().any(|b| (0xd6..=0xdb).contains(b))
            && unsafe { (self.starts_rtl)(Span::from_bytes(word), 64) != 0 };
        let mut flags = u8::from(attach) | (u8::from(no_space) << 1);
        if attach && !self.tokens.is_empty() {
            let previous = last(self.word(self.tokens.len() - 1));
            if text::break_after_hyphen(previous) {
                flags = 3;
            } else if text::opportunity(previous, first(word)) {
                flags = 2;
            }
        }
        let count = text::character_offsets(word, 0, &mut |_| {});
        if count != 0 {
            self.reserve_tokens(count + 1);
            let mut begin = 0;
            let mut source = offset;
            let mut first_token = true;
            text::character_offsets(word, count, &mut |end| {
                let part = &word[begin..end];
                let flags = if first_token {
                    flags
                } else {
                    2 | u8::from(text::break_after_hyphen(last(
                        self.word(self.tokens.len().saturating_sub(1)),
                    )))
                };
                self.push(part, style, flags, 0, source, link, slot, true);
                source = self.tokens.source_after(source, codepoints(part));
                begin = end;
                first_token = false;
            });
            if begin < word.len() {
                let flags = if first_token {
                    flags
                } else {
                    2 | u8::from(text::break_after_hyphen(last(
                        self.word(self.tokens.len().saturating_sub(1)),
                    )))
                };
                self.push(&word[begin..], style, flags, 0, source, link, slot, true);
            }
        } else if contains_cjk(word) {
            self.push(word, style, flags, 0, offset, link, slot, true);
        } else if !self.focus || style & 1 != 0 {
            let mut count = 0;
            explicit_offsets(word, flags & 1 != 0, &mut |_| count += 1);
            if count == 0 {
                self.push(word, style, flags, 0, offset, link, slot, true);
            } else {
                self.reserve_tokens(count + 1);
                let mut begin = 0;
                let mut source = offset;
                explicit_offsets(word, flags & 1 != 0, &mut |end| {
                    let part = &word[begin..end];
                    self.push(
                        part,
                        style,
                        if begin == 0 { flags } else { 3 },
                        0,
                        source,
                        link,
                        slot,
                        true,
                    );
                    source = self.tokens.source_after(source, codepoints(part));
                    begin = end;
                });
                if begin < word.len() {
                    self.push(
                        &word[begin..],
                        style,
                        if begin == 0 { flags } else { 3 },
                        0,
                        source,
                        link,
                        slot,
                        true,
                    );
                }
            }
        } else {
            self.reserve_tokens(word.len());
            let mut pos = 0;
            let mut begin = 0;
            let mut kind = word_character(utf8::next(word, &mut pos).unwrap_or(0));
            while pos < word.len() {
                let end = pos;
                let next = word_character(utf8::next(word, &mut pos).unwrap_or(0));
                if next != kind {
                    self.focus_segment(word, begin, end, kind, style, flags, offset, link, slot);
                    begin = end;
                    kind = next;
                }
            }
            self.focus_segment(
                word,
                begin,
                word.len(),
                kind,
                style,
                flags,
                offset,
                link,
                slot,
            );
        }
        self.has_rtl |= rtl;
    }
    #[allow(clippy::too_many_arguments)] // Preserves the existing token/metric operation.
    fn focus_segment(
        &mut self,
        word: &[u8],
        begin: usize,
        end: usize,
        kind: bool,
        mut style: u8,
        flags: u8,
        offset: u32,
        link: u8,
        slot: u8,
    ) {
        let part = &word[begin..end];
        let source = self.tokens.source_after(offset, codepoints(&word[..begin]));
        let flags = if begin == 0 {
            flags
        } else {
            1 | (u8::from(text::break_after_hyphen(last(
                self.word(self.tokens.len().saturating_sub(1)),
            ))) << 1)
        };
        let mut boundary = 0;
        if kind {
            let count = codepoints(part);
            let bold = (count * 45 / 100).clamp(1, 9);
            if bold >= count {
                style |= 1;
            } else {
                let mut pos = 0;
                for _ in 0..bold {
                    utf8::next(part, &mut pos);
                }
                boundary = pos.min(255) as u8;
            }
        }
        self.push(part, style, flags, boundary, source, link, slot, false);
    }
}
pub fn first(bytes: &[u8]) -> u32 {
    let mut pos = 0;
    while let Some(cp) = utf8::next(bytes, &mut pos) {
        if cp != 0xad {
            return cp;
        }
    }
    0
}
pub fn last(bytes: &[u8]) -> u32 {
    if bytes.is_empty() {
        return 0;
    }
    let mut pos = bytes.len() - 1;
    while pos > 0 && bytes[pos] & 0xc0 == 0x80 {
        pos -= 1;
    }
    utf8::next(bytes, &mut pos).unwrap_or(0)
}
pub fn codepoints(bytes: &[u8]) -> u32 {
    let mut pos = 0;
    let mut count = 0;
    while utf8::next(bytes, &mut pos).is_some() {
        count += 1;
    }
    count
}
fn contains_cjk(bytes: &[u8]) -> bool {
    let mut pos = 0;
    while let Some(cp) = utf8::next(bytes, &mut pos) {
        if text::cjk(cp) {
            return true;
        }
    }
    false
}
fn explicit_offsets(bytes: &[u8], attach: bool, emit: &mut impl FnMut(usize)) {
    let mut pos = 0;
    let mut leading = attach;
    while let Some(cp) = utf8::next(bytes, &mut pos) {
        if cp == 0 {
            break;
        }
        if text::break_after_hyphen(cp) {
            loop {
                let begin = pos;
                let Some(next) = utf8::next(bytes, &mut pos) else {
                    break;
                };
                if !text::break_after_hyphen(next) {
                    pos = begin;
                    break;
                }
            }
            if leading && pos < bytes.len() {
                emit(pos);
            }
        }
        leading = true;
    }
}
fn word_character(cp: u32) -> bool {
    match cp {
        0..=127 => (cp | 0x20) >= 97 && (cp | 0x20) <= 122 || cp == 39,
        0x2000..=0x2bff => cp == 0x2018 || cp == 0x2019,
        0xa1..=0xbf => matches!(cp, 0xaa | 0xb5 | 0xba),
        0x2e00..=0x2e7f | 0x2d7 | 0xfe63 | 0xff0d => false,
        _ => true,
    }
}

#[cfg(feature = "chapter-trace")]
pub fn trace(m: &Metrics, args: core::fmt::Arguments<'_>) {
    use core::fmt::Write;
    if let Some(emit) = m.trace {
        let mut text = alloc::string::String::new();
        let _ = text.write_fmt(args);
        text.push('\n');
        unsafe {
            emit(m.context, Span::from_bytes(text.as_bytes()));
        }
    }
}
#[cfg(feature = "chapter-trace")]
pub struct Hex<'a>(pub &'a [u8]);
#[cfg(feature = "chapter-trace")]
impl core::fmt::Display for Hex<'_> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        for b in self.0 {
            write!(f, "{b:02x}")?;
        }
        Ok(())
    }
}
