//! Renderer-dependent paragraph measurements and break selection.
use super::{
    layout, layout_split,
    paragraph::{first, last, Metrics, Paragraph, Span},
};
use alloc::vec::Vec;
use core::{ffi::c_void, ptr};
extern crate alloc;

pub struct Widths(pub layout::Widths);
impl Drop for Widths {
    fn drop(&mut self) {
        unsafe {
            self.0.destroy();
        }
    }
}
pub struct Breaks(pub layout::Breaks);
impl Drop for Breaks {
    fn drop(&mut self) {
        unsafe {
            self.0.destroy();
        }
    }
}

pub fn scaled(advance: i32, percent: u8) -> i32 {
    if percent == 100 {
        advance
    } else {
        (advance * i32::from(percent) + 50) / 100
    }
}

impl Metrics {
    /// # Safety
    /// Nonempty text has a readable NUL at text.as_ptr().add(text.len()).
    unsafe fn advance(&self, font: i32, text: &[u8], style: u8, spacing: i8) -> i32 {
        unsafe { (self.advance)(self.context, font, Span::from_bytes(text), style, spacing) }
    }
    pub fn space(&self, font: i32, style: u8) -> i32 {
        unsafe { (self.space)(self.context, font, style) }
    }
    #[allow(clippy::too_many_arguments)] // Preserves the existing token/metric operation.
    pub fn gap(
        &self,
        font: i32,
        left: &[u8],
        right: &[u8],
        style: u8,
        flags: u8,
        tracking: i8,
        spacing: u8,
    ) -> i32 {
        unsafe {
            if flags & 1 != 0 {
                (self.kerning)(
                    self.context,
                    font,
                    last(left),
                    first(right),
                    style,
                    tracking,
                )
            } else if flags & 2 != 0 {
                i32::from(tracking)
            } else {
                scaled(
                    (self.space_advance)(self.context, font, last(left), first(right), style),
                    spacing,
                )
            }
        }
    }
    pub fn prefix(&self, font: i32, word: &[u8], style: u8, focus: u8, tracking: i8) -> u16 {
        let mut buffer = [0; 40];
        let len = usize::from(focus).min(39).min(word.len());
        buffer[..len].copy_from_slice(&word[..len]);
        let bold = style | 1;
        let mut pos = usize::from(focus).min(word.len());
        let next = crate::utf8::next(word, &mut pos).unwrap_or(0);
        let kern = unsafe {
            (self.kerning)(
                self.context,
                font,
                last(&buffer[..len]),
                next,
                bold,
                tracking,
            )
        };
        // The zero-filled buffer retains a NUL after the copied prefix.
        (unsafe { self.advance(font, &buffer[..len], bold, tracking) } + kern) as u16
    }
    #[allow(clippy::too_many_arguments)] // Preserves the existing token/metric operation.
    /// # Safety
    /// When terminated is true, nonempty word has a readable trailing NUL.
    /// Suffix slices retain that terminator; other slices use the copying path.
    unsafe fn width(
        &self,
        font: i32,
        word: &[u8],
        style: u8,
        focus: u8,
        tracking: i8,
        spacing: u8,
        hyphen: bool,
        terminated: bool,
    ) -> Option<u16> {
        if focus != 0 && usize::from(focus) < word.len() {
            let suffix = self.width(
                font,
                &word[usize::from(focus)..],
                style,
                0,
                tracking,
                spacing,
                hyphen,
                terminated,
            )?;
            return Some(
                self.prefix(font, word, style, focus, tracking)
                    .wrapping_add(suffix),
            );
        }
        let style = style | if focus != 0 { 1 } else { 0 };
        if word == b" " && !hyphen {
            return Some(scaled(self.space(font, style), spacing) as u16);
        }
        if terminated && !hyphen && !word.windows(2).any(|s| s == b"\xc2\xad") {
            return Some(self.advance(font, word, style, tracking) as u16);
        }
        let mut sanitized = Vec::new();
        sanitized
            .try_reserve_exact(word.len().checked_add(2)?)
            .ok()?;
        let mut pos = 0;
        while pos < word.len() {
            if word[pos..].starts_with(b"\xc2\xad") {
                pos += 2;
            } else {
                sanitized.push(word[pos]);
                pos += 1;
            }
        }
        if hyphen {
            sanitized.push(b'-');
        }
        let len = sanitized.len();
        sanitized.push(0);
        Some(self.advance(font, &sanitized[..len], style, tracking) as u16)
    }
}
impl Paragraph {
    pub fn first_indent(&self, first: bool, m: &Metrics, font: i32) -> i32 {
        if self.first_consumed || !first || !self.natural {
            return 0;
        }
        if self.block.text_indent_defined != 0 && self.block.text_indent < 0 {
            return i32::from(self.block.text_indent);
        }
        if self.indent == 0 {
            return 0;
        }
        scaled(m.space(font, 0), self.spacing) * i32::from(self.indent)
    }
    pub fn ruby_start(&self, start: usize, end: usize, m: &Metrics, font: i32) -> Option<i32> {
        if self.ruby_text(start).is_empty()
            || self.tokens.get(start).unwrap_or_default().style & 64 != 0
        {
            return Some(0);
        }
        let mut finish = start + 1;
        while finish < end && self.tokens.get(finish).unwrap_or_default().style & 64 != 0 {
            finish += 1;
        }
        self.ruby_overlap(start, finish, m, font)
    }
    pub fn ruby_end(&self, start: usize, end: usize, m: &Metrics, font: i32) -> Option<i32> {
        if self.ruby.is_empty() || end == 0 || start >= end {
            return Some(0);
        }
        let mut leader = end - 1;
        while leader > start && self.tokens.get(leader).unwrap_or_default().style & 64 != 0 {
            leader -= 1;
        }
        if self.ruby_text(leader).is_empty()
            || self.tokens.get(leader).unwrap_or_default().style & 64 != 0
        {
            return Some(0);
        }
        self.ruby_overlap(leader, end, m, font)
    }
    fn ruby_overlap(&self, start: usize, end: usize, m: &Metrics, font: i32) -> Option<i32> {
        let mut width = 0;
        for i in start..end {
            // WordStore appends a NUL after each word.
            width += i32::from(unsafe {
                m.width(
                    font,
                    self.word(i),
                    self.style(i),
                    0,
                    self.block.character_spacing,
                    self.spacing,
                    false,
                    true,
                )?
            });
        }
        Some(
            // Ruby Bytes stores a trailing NUL.
            (unsafe {
                m.advance(
                    font,
                    self.ruby_text(start),
                    16,
                    self.block.character_spacing,
                )
            } - width)
                .max(0)
                / 2,
        )
    }
    pub fn widths(&mut self, m: &Metrics, font: i32) -> Option<Widths> {
        let mut widths = Widths(layout::Widths {
            data: ptr::null_mut(),
            len: 0,
            capacity: 0,
            allocator: self.allocator,
            failed: 0,
        });
        unsafe {
            if !widths.0.reserve(self.tokens.len()) {
                return None;
            }
        }
        for i in 0..self.tokens.len() {
            let t = self.tokens.get(i)?;
            // WordStore appends a NUL after each word.
            let width = unsafe {
                m.width(
                    self.font(i, font),
                    self.word(i),
                    self.style(i),
                    t.focus,
                    self.block.character_spacing,
                    self.spacing,
                    false,
                    true,
                )?
            };
            unsafe {
                if !widths.0.insert(widths.0.len, width) {
                    return None;
                }
            }
        }
        if self.ruby.is_empty() {
            return Some(widths);
        }
        struct Group {
            start: usize,
            count: usize,
            overlap: i32,
        }
        let mut groups = Vec::new();
        let mut i = 0;
        let w = unsafe { core::slice::from_raw_parts_mut(widths.0.data, widths.0.len) };
        while i < self.tokens.len() {
            if !self.ruby_text(i).is_empty() && self.tokens.get(i)?.style & 64 == 0 {
                let mut count = 1;
                let mut base = i32::from(w[i]);
                while i + count < self.tokens.len() && self.tokens.get(i + count)?.style & 64 != 0 {
                    base += i32::from(w[i + count]);
                    count += 1;
                }
                // Ruby Bytes stores a trailing NUL.
                let ruby =
                    unsafe { m.advance(font, self.ruby_text(i), 16, self.block.character_spacing) };
                groups.try_reserve(1).ok()?;
                groups.push(Group {
                    start: i,
                    count,
                    overlap: ((ruby - base) / 2).max(0),
                });
                i += count;
            } else {
                i += 1;
            }
        }
        for (index, g) in groups.iter().enumerate() {
            if g.start > 0 {
                let extra = if ideograph(last(self.word(g.start - 1))) {
                    g.overlap
                } else {
                    (g.overlap - i32::from(w[g.start - 1]) / 2).max(0)
                };
                w[g.start - 1] = w[g.start - 1].wrapping_add(extra as u16);
            }
            let next = g.start + g.count;
            if next >= self.tokens.len() {
                continue;
            }
            if let Some(other) = groups.get(index + 1).filter(|other| other.start == next) {
                w[next - 1] = w[next - 1].wrapping_add((g.overlap + other.overlap) as u16);
                continue;
            }
            let extra = if ideograph(first(self.word(next))) {
                g.overlap
            } else {
                (g.overlap - i32::from(w[next]) / 2).max(0)
            };
            w[next - 1] = w[next - 1].wrapping_add(extra as u16);
            if let Some(other) = groups.get(index + 1) {
                let mut non_ideo = true;
                let mut gap = 0;
                for (k, width) in w.iter().enumerate().take(other.start).skip(next) {
                    if ideograph(first(self.word(k))) {
                        non_ideo = false;
                        break;
                    }
                    gap += i32::from(*width);
                }
                if non_ideo {
                    let allowed_right = g.overlap.min(i32::from(w[next - 1]) / 2);
                    let allowed_left = other.overlap.min(i32::from(w[other.start - 1]) / 2);
                    w[next - 1] = w[next - 1]
                        .wrapping_add((allowed_right + allowed_left - gap).max(0) as u16);
                }
            }
        }
        Some(widths)
    }
}
fn ideograph(cp: u32) -> bool {
    matches!(cp, 0x4e00..=0x9fff | 0x3400..=0x4dbf | 0xf900..=0xfaff | 0x20000..=0x3ffff)
}

struct Breaking<'a> {
    paragraph: *mut Paragraph,
    metrics: &'a Metrics,
    widths: *mut layout::Widths,
    breaks: *mut layout::Breaks,
    font: i32,
    language: i32,
}
unsafe extern "C" fn allows(raw: *mut c_void, i: usize) -> u8 {
    let c = &*raw.cast::<Breaking>();
    let t = (*c.paragraph).tokens.get(i).unwrap_or_default();
    u8::from(t.flags & 1 == 0 || t.flags & 2 != 0)
}
unsafe extern "C" fn gap(raw: *mut c_void, i: usize) -> i32 {
    let c = &*raw.cast::<Breaking>();
    let p = &*c.paragraph;
    c.metrics.gap(
        c.font,
        p.word(i - 1),
        p.word(i),
        p.style(i - 1),
        p.tokens.get(i).unwrap_or_default().flags,
        p.block.character_spacing,
        p.spacing,
    )
}
unsafe extern "C" fn ruby_start(raw: *mut c_void, start: usize, end: usize) -> i32 {
    let c = &*raw.cast::<Breaking>();
    (*c.paragraph)
        .ruby_start(start, end, c.metrics, c.font)
        .unwrap_or_else(|| {
            (*c.paragraph).dropped = true;
            0
        })
}
unsafe extern "C" fn ruby_end(raw: *mut c_void, start: usize, end: usize) -> i32 {
    let c = &*raw.cast::<Breaking>();
    (*c.paragraph)
        .ruby_end(start, end, c.metrics, c.font)
        .unwrap_or_else(|| {
            (*c.paragraph).dropped = true;
            0
        })
}
unsafe extern "C" fn emit(raw: *mut c_void, end: usize) -> u8 {
    let c = &*raw.cast::<Breaking>();
    (*c.breaks).insert((*c.breaks).len, end) as u8
}
struct Splitting<'a> {
    metrics: &'a Metrics,
    font: i32,
    tracking: i8,
    spacing: u8,
    failed: bool,
}
unsafe extern "C" fn measure_split(
    raw: *mut c_void,
    data: *const u8,
    len: usize,
    style: u8,
    focus: u8,
    hyphen: u8,
) -> u16 {
    let c = &mut *raw.cast::<Splitting>();
    match c.metrics.width(
        c.font,
        core::slice::from_raw_parts(data, len),
        style,
        focus,
        c.tracking,
        c.spacing,
        hyphen != 0,
        *data.add(len) == 0,
    ) {
        Some(width) => width,
        None => {
            c.failed = true;
            u16::MAX
        }
    }
}
unsafe extern "C" fn boundary(left: u32, right: u32) -> u8 {
    crate::text::boundary(left, right) as u8
}
unsafe extern "C" fn inserted(_: *mut c_void, _: usize) {}
unsafe extern "C" fn split(raw: *mut c_void, i: usize, available: i32, fallback: u8) -> u8 {
    let c = &mut *raw.cast::<Breaking>();
    let p = &mut *c.paragraph;
    if available <= 0 || i >= p.tokens.len() {
        return 0;
    }
    // Reserve before the token transaction so ruby insertion cannot fail afterwards.
    if i < p.ruby.len() && !p.ruby.reserve(p.ruby.len() + 1) {
        p.dropped = true;
        return 0;
    }
    let mut context = Splitting {
        metrics: c.metrics,
        font: p.font(i, c.font),
        tracking: p.block.character_spacing,
        spacing: p.spacing,
        failed: false,
    };
    let request = layout_split::SplitRequest {
        context: (&mut context as *mut Splitting).cast(),
        measure: measure_split,
        boundary,
        inserted,
        index: i,
        available,
        language: c.language,
        style: p.style(i),
        fallback,
    };
    let mut result = layout_split::SplitResult::default();
    #[cfg(feature = "chapter-trace")]
    let trace_word = if c.metrics.trace.is_some() {
        p.word(i).to_vec()
    } else {
        Vec::new()
    };
    let success = layout_split::split(
        &mut p.store,
        &mut p.tokens,
        &mut *c.widths,
        &request,
        &mut result,
    );
    if success {
        para_trace!(
            c.metrics,
            "split\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
            i,
            available,
            fallback,
            result.offset,
            result.hyphen,
            result.width,
            result.remainder_width,
            super::paragraph::Hex(&trace_word),
            result.remainder_offset
        );
    }
    p.dropped |= context.failed;
    if success && i < p.ruby.len() {
        p.ruby.insert_reserved(i + 1);
    }
    success as u8
}
/// # Safety
/// Paragraph is exclusively borrowed for breaking. Renderer callbacks cannot reenter it.
pub unsafe fn breaks(
    p: *mut Paragraph,
    m: &Metrics,
    widths: &mut Widths,
    font: i32,
    page: i32,
    max_lines: usize,
    language: i32,
) -> Option<Breaks> {
    let mut breaks = Breaks(layout::Breaks {
        data: ptr::null_mut(),
        len: 0,
        capacity: 0,
        allocator: (*p).allocator,
        failed: 0,
    });
    let indent = (*p).first_indent(true, m, font);
    let mut context = Breaking {
        paragraph: p,
        metrics: m,
        widths: &mut widths.0,
        breaks: &mut breaks.0,
        font,
        language,
    };
    let callbacks = layout::Callbacks {
        context: (&mut context as *mut Breaking).cast(),
        widths: &widths.0,
        has_ruby: u8::from(!(*p).ruby.is_empty()),
        allows_break: allows,
        gap,
        ruby_start,
        ruby_end,
        split,
        emit,
    };
    let ok = if (*p).hyphenation || max_lines != usize::MAX {
        layout::greedy(
            &callbacks,
            page,
            indent,
            max_lines,
            (*p).first_consumed,
            (*p).hyphenation,
        )
    } else {
        layout::optimal(&callbacks, (*p).allocator, page, indent)
    };
    if ok && breaks.0.len != 0 && widths.0.failed == 0 {
        Some(breaks)
    } else {
        None
    }
}
