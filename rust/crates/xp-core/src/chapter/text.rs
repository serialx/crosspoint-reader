use super::*;
use crate::utf8;

fn punctuation(cp: u32) -> bool {
    (cp < 128 && (cp as u8).is_ascii_punctuation())
        || matches!(cp,0xa1|0xab|0xbb|0xbf|0x2010..=0x205e|0x3008..=0x3011)
}
impl State {
    pub(super) fn cancel_drop(&mut self) {
        #[cfg(feature = "chapter-trace")]
        self.trace(
            2,
            &[],
            Span::new(&self.drop_cap.text[..usize::from(self.drop_cap.length)]),
            Span::new(&[]),
            core::ptr::null(),
        );
        let length = usize::from(self.drop_cap.length);
        if self.word_len + length > self.word.len() - 1 {
            self.oom = true;
            return;
        }
        self.word[self.word_len..self.word_len + length]
            .copy_from_slice(&self.drop_cap.text[..length]);
        self.word_len += length;
        self.drop_cap = DropCap::default();
    }
    #[inline(never)]
    fn capture_drop(&mut self, bytes: &[u8]) -> bool {
        let length = usize::from(self.drop_cap.length);
        if length + bytes.len() > 12 || self.drop_cap.codepoints >= 3 {
            self.cancel_drop();
            return false;
        }
        #[cfg(feature = "chapter-trace")]
        self.trace(
            3,
            &[
                u32::from(self.drop_cap.lines),
                u32::from(self.drop_cap.bold),
            ],
            Span::new(bytes),
            Span::new(&[]),
            core::ptr::null(),
        );
        self.drop_cap.text[length..length + bytes.len()].copy_from_slice(bytes);
        self.drop_cap.length = (length + bytes.len()) as u8;
        self.drop_cap.text[length + bytes.len()] = 0;
        self.drop_cap.codepoints += 1;
        if self.drop_cap.span_depth < 0 && !punctuation(utf8::next(bytes, &mut 0).unwrap_or(0)) {
            self.drop_cap.first_pending = 0;
        }
        true
    }
    // Keep the event frame separate from the XML driver on the 8 KiB render stack.
    #[inline(never)]
    pub fn characters(&mut self, text: &[u8]) {
        if self.stopped || self.oom {
            return;
        }
        let count = self.inside_body && self.non_visible == 0 && !self.synthetic;
        let callback_offset = self.visible_offset;
        if count {
            let mut pos = 0;
            while pos < text.len() {
                if utf8::next(text, &mut pos).is_none() {
                    break;
                }
                self.visible_offset = self.visible_offset.wrapping_add(1);
            }
        }
        if self.table_depth > 1 && !self.inside_cell {
            return;
        }
        if self.skip < self.depth {
            return;
        }
        if self.collecting_ruby {
            if !self.ruby.append(text) {
                self.oom = true;
                return;
            }
            return;
        }
        if self.table_depth == 1 && !self.inside_cell && text.iter().all(|&c| whitespace(c)) {
            return;
        }
        if !self.paragraph_exists() {
            let style = if self.blocks.is_empty() {
                Block::default()
            } else {
                self.parent().without_bottom()
            };
            self.call_create_paragraph(&style);
            if !self.paragraph_exists() {
                self.oom = true;
                return;
            }
            self.call_reset_extracted();
        }
        if self.in_link {
            let mut start = 0;
            let mut end = text.len();
            while start < end && (whitespace(text[start]) || text[start] == b'[') {
                start += 1;
            }
            while end > start && (whitespace(text[end - 1]) || text[end - 1] == b']') {
                end -= 1;
            }
            for &c in &text[start..end] {
                if self.footnote_len >= 31 {
                    break;
                }
                self.footnote.number[self.footnote_len] = c;
                self.footnote_len += 1;
            }
            self.footnote.number[self.footnote_len] = 0;
        }
        if self.inline_size.valid && !self.inline_size.open && text.iter().any(|&c| !whitespace(c))
        {
            self.inline_size.valid = false;
        }
        let mut next_offset = callback_offset;
        let mut i = 0;
        while i < text.len() {
            let offset = next_offset;
            let c = text[i];
            if count && c & 0xc0 != 0x80 {
                next_offset = next_offset.wrapping_add(1);
            }
            let span = self.drop_cap.span_depth >= 0;
            if span
                || (self.drop_cap.first_pending != 0
                    && !whitespace(c)
                    && self.word_len == 0
                    && !self.in_link
                    && self.paragraph_empty())
            {
                if whitespace(c) {
                    if self.drop_cap.length > 0 {
                        self.cancel_drop();
                    }
                } else {
                    let length = (if c < 0x80 {
                        1
                    } else if c < 0xe0 {
                        2
                    } else if c < 0xf0 {
                        3
                    } else {
                        4
                    })
                    .min(text.len() - i);
                    if self.capture_drop(&text[i..i + length]) {
                        i += length;
                        continue;
                    }
                }
            }
            if self.space && whitespace(c) {
                self.flush_word();
                self.call_suppress_indent();
                if c == b'\n' {
                    let style = self.paragraph_style().without_top().without_bottom();
                    self.call_line_break(&style);
                    if !self.paragraph_exists() {
                        return;
                    }
                } else if c != b'\r' {
                    for _ in 0..if c == b'\t' { 4 } else { 1 } {
                        self.word[0] = b' ';
                        self.word_len = 1;
                        self.word_offset = offset;
                        self.flush_word();
                    }
                }
                i += 1;
                continue;
            }
            if whitespace(c) {
                if self.word_len > 0 {
                    self.flush_word();
                }
                self.continued = false;
                i += 1;
                continue;
            }
            let no_break = if c == 0xc2 && text[i..].starts_with(b"\xc2\xa0") {
                2
            } else if c == 0xe2 && text[i..].starts_with(b"\xe2\x80\xaf") {
                3
            } else {
                0
            };
            if no_break != 0 {
                if self.word_len > 0 {
                    self.flush_word();
                }
                self.word[0] = b' ';
                self.word[1] = 0;
                self.word_len = 1;
                self.word_offset = offset;
                self.continued = true;
                self.flush_word();
                self.continued = true;
                i += no_break;
                continue;
            }
            if c == 0xef && text[i..].starts_with(b"\xef\xbb\xbf") {
                i += 3;
                continue;
            }
            if self.word_len >= 200 {
                let safe = utf8::safe_truncate(&self.word[..self.word_len]);
                if safe < self.word_len && safe > 0 {
                    let overflow = self.word_len - safe;
                    let mut offset = self.word_offset;
                    let mut pos = 0;
                    while pos < safe {
                        if utf8::next(&self.word[..safe], &mut pos).is_none() {
                            break;
                        }
                        offset = offset.wrapping_add(1);
                    }
                    let mut saved = [0u8; 4];
                    if overflow > saved.len() {
                        self.oom = true;
                        return;
                    }
                    saved[..overflow].copy_from_slice(&self.word[safe..self.word_len]);
                    self.word_len = safe;
                    self.flush_word();
                    self.continued = true;
                    self.word[..overflow].copy_from_slice(&saved[..overflow]);
                    self.word_len = overflow;
                    self.word_offset = offset;
                } else {
                    self.flush_word();
                    self.continued = true;
                }
            }
            if self.word_len == 0 {
                self.word_offset = offset;
            }
            self.word[self.word_len] = c;
            self.word_len += 1;
            i += 1;
        }
        if !self.paragraph_exists() {
            return;
        }
        let words = self.call_paragraph_size();
        if words > if self.embedded { 320 } else { 750 } && !self.in_ruby {
            self.make_pages(false);
        }
    }
    pub fn flush_word(&mut self) {
        if !self.paragraph_exists() {
            self.word_len = 0;
            self.continued = false;
            return;
        }
        if self.space {
            self.call_preserve_space();
        }
        let mut style = u8::from(self.bold_until < self.depth || self.bold)
            | (u8::from(self.italic_until < self.depth || self.italic) << 1);
        if self.decoration & 1 != 0 {
            style |= 4;
        }
        if self.decoration & 2 != 0 {
            style |= 8;
        }
        if self.small {
            style |= 128;
        }
        if self.sup {
            style |= 16;
        } else if self.sub {
            style |= 32;
        }
        self.word[self.word_len] = 0;
        let bytes = self.word_len;
        if self.inside_cell
            && self.call_table_stacked() == 0
            && self.call_table_bytes().saturating_add(bytes as u32) > 2048
        {
            self.call_table_fallback();
        }
        let mut link = 0;
        if self.in_link {
            let length = self
                .footnote
                .href
                .iter()
                .position(|&c| c == 0)
                .unwrap_or(256);
            let href = Span::new(&self.footnote.href[..length]);
            if self.call_link_matches(self.link_id, href) == 0 {
                self.link_id = self.call_add_link(href);
            }
            link = self.link_id;
        }
        let mut scale = 0.0;
        // Copy one entry before any callback; callbacks can update semantic stacks.
        for i in 0..self.inline.len() {
            let Some(entry) = self.inline.get(i).copied() else {
                break;
            };
            if !entry.has_scale {
                continue;
            }
            let base = if scale > 0.0 {
                scale
            } else {
                self.call_paragraph_scale()
            };
            scale = if entry.scale_rem {
                entry.scale
            } else {
                base * entry.scale
            };
        }
        let slot =
            if scale > 0.0 && (self.inline_size.valid || scale != self.call_paragraph_scale()) {
                self.call_size_slot(scale.clamp(0.5, 3.0))
            } else {
                0
            };
        let word = Span::new(&self.word[..bytes]);
        #[cfg(feature = "chapter-trace")]
        {
            let block = self.paragraph_style();
            self.trace(
                1,
                &[
                    u32::from(style),
                    u32::from(self.continued),
                    self.word_offset,
                    u32::from(link),
                    u32::from(slot),
                    u32::from(self.space),
                    scale.to_bits(),
                ],
                word,
                Span::new(&[]),
                &block,
            );
        }
        self.call_add_word(
            word,
            style,
            0,
            u8::from(self.continued),
            self.word_offset,
            link,
            slot,
            u8::from(self.space),
        );
        if self.inside_cell && self.call_table_stacked() == 0 {
            self.call_add_table_bytes(bytes as u32);
            let mut words = self.call_paragraph_size();
            words = words.saturating_add(self.call_buffered_words());
            if words > 160 {
                self.call_table_fallback();
            }
        }
        self.word_len = 0;
        self.continued = false;
        self.marker_only = false;
    }
}
