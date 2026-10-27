//! Synchronous borrowed-line extraction. The sink may read paragraph scalars only.
extern crate alloc;
use super::{
    layout_extract, layout_line,
    paragraph::{Metrics, Paragraph, Span},
    paragraph_layout::{self, Widths},
};
use alloc::vec::Vec;
use core::{ffi::c_void, ptr, slice};

#[repr(C)]
pub struct Link {
    pub href: [u8; 256],
    pub x: i16,
    pub width: i16,
    pub top_lift: i16,
}
#[repr(C)]
pub struct Output {
    pub text: Span,
    pub offsets: *const u16,
    pub count: u32,
    pub positions: *const i16,
    pub styles: *const u8,
    pub boundaries: *const u8,
    pub suffixes: *const u16,
    pub sources: *const u32,
    pub ruby: *const Span,
    pub links: *const Link,
    pub link_count: u32,
    pub block: *const crate::chapter::style::Block,
    pub paragraph_start: u16,
    pub visible_offset: u32,
    pub font_slots: *const u8,
    pub fonts: *const i32,
}
#[repr(C)]
pub struct Options {
    pub font: i32,
    pub width: u16,
    pub include_last: u8,
    pub tracking: i8,
    pub spacing: u8,
    pub max_lines: u32,
    pub language: i32,
    pub stop: *const u8,
    pub context: *mut c_void,
    pub emit: unsafe extern "C" fn(*mut c_void, *const Output) -> u8,
}
struct Line(layout_extract::Line);
impl Drop for Line {
    fn drop(&mut self) {
        unsafe {
            self.0.destroy();
        }
    }
}
struct Positions<'a> {
    paragraph: &'a Paragraph,
    metrics: &'a Metrics,
    widths: &'a Widths,
    text: &'a [u8],
    offsets: &'a [u16],
    order: &'a [u16],
    start: usize,
    font: i32,
    reordered: bool,
}
impl Positions<'_> {
    fn logical(&self, i: usize) -> usize {
        self.start
            + if self.reordered {
                usize::from(self.order[i])
            } else {
                i
            }
    }
    fn word(&self, i: usize) -> &[u8] {
        let offset = usize::from(
            self.offsets[if self.reordered {
                usize::from(self.order[i])
            } else {
                i
            }],
        );
        let bytes = &self.text[offset..];
        &bytes[..bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len())]
    }
    fn flags(&self, i: usize) -> u8 {
        let t = self
            .paragraph
            .tokens
            .get(self.logical(i))
            .unwrap_or_default();
        if !self.reordered {
            return t.flags;
        }
        let mut continues = false;
        if i > 0 {
            let prev = self.logical(i - 1);
            let current = self.logical(i);
            continues = (current == prev + 1 && t.flags & 1 != 0)
                || (prev == current + 1
                    && self.paragraph.tokens.get(prev).unwrap_or_default().flags & 1 != 0);
        }
        u8::from(continues) | if continues { 0 } else { t.flags & 2 }
    }
}
unsafe extern "C" fn line_word(raw: *mut c_void, i: usize) -> layout_line::WordMetrics {
    let c = &*raw.cast::<Positions>();
    layout_line::WordMetrics {
        width: *c.widths.0.data.add(c.logical(i)),
        continues: c.flags(i) & 1,
        space: u8::from(c.word(i) == b" "),
    }
}
unsafe extern "C" fn line_gap(raw: *mut c_void, i: usize) -> i32 {
    let c = &*raw.cast::<Positions>();
    let p = c.paragraph;
    c.metrics.gap(
        c.font,
        c.word(i - 1),
        c.word(i),
        p.style(c.logical(i - 1)) & !128,
        c.flags(i),
        p.block.character_spacing,
        p.spacing,
    )
}

fn sanitized_len(word: &[u8]) -> usize {
    word.len() - 2 * word.windows(2).filter(|pair| *pair == b"\xc2\xad").count()
}

unsafe fn extract(
    p: *mut Paragraph,
    m: &Metrics,
    o: &Options,
    widths: &Widths,
    breaks: &[usize],
    break_index: usize,
) -> Option<()> {
    let end = breaks[break_index];
    let mut start = if break_index == 0 {
        0
    } else {
        breaks[break_index - 1]
    };
    if break_index > 0 || (*p).paragraph_emitted {
        while start + 1 < end && (*p).tokens.get(start)?.flags & 1 != 0 && (*p).word(start) == b" "
        {
            start += 1;
        }
    }
    let count = end - start;
    let has_focus = (start..end).any(|i| (*p).tokens.get(i).is_some_and(|t| t.focus != 0));
    if count == 0 || count > 10000 {
        return None;
    }
    let bytes = (start..end).try_fold(0usize, |sum, i| {
        sum.checked_add(sanitized_len((*p).word(i)) + 1)
    })?;
    if bytes > u16::MAX as usize {
        return None;
    }
    // One line-scoped allocation holds disjoint metadata and packed text spans.
    let alignment = core::mem::align_of::<Span>();
    let ruby_offset = (count * if has_focus { 14 } else { 11 }).next_multiple_of(alignment);
    let offsets_offset = ruby_offset + count * core::mem::size_of::<Span>();
    let order_offset = offsets_offset + count * 2;
    let text_offset = order_offset + count * 2;
    let allocation = ((*(*p).allocator).allocate)(text_offset + bytes);
    if allocation.is_null() {
        return None;
    }
    ptr::write_bytes(allocation, 0, text_offset + bytes);
    let mut line = Line(layout_extract::Line {
        data: allocation,
        count,
        allocator: (*p).allocator,
        focus: u8::from(has_focus),
    });
    let positions = allocation.add(count * 8).cast::<i16>();
    let styles = allocation.add(count * 10);
    let boundaries = allocation.add(count * 11);
    let suffixes = if has_focus {
        allocation.add(count * 12).cast::<u16>()
    } else {
        ptr::null_mut()
    };
    let ruby = slice::from_raw_parts_mut(allocation.add(ruby_offset).cast::<Span>(), count);
    let offsets = slice::from_raw_parts_mut(allocation.add(offsets_offset).cast::<u16>(), count);
    let order = slice::from_raw_parts_mut(allocation.add(order_offset).cast::<u16>(), count);
    let text = slice::from_raw_parts_mut(allocation.add(text_offset), bytes);
    let mut text_len = 0;
    for i in start..end {
        offsets[i - start] = text_len as u16;
        let word = (*p).word(i);
        let mut read = 0;
        while read < word.len() {
            if word[read..].starts_with(b"\xc2\xad") {
                read += 2;
            } else {
                text[text_len] = word[read];
                text_len += 1;
                read += 1;
            }
        }
        text[text_len] = 0;
        text_len += 1;
        ruby[i - start] = Span::from_bytes((*p).ruby_text(i));
    }
    let text = &text[..text_len];
    let indent = (*p).first_indent(break_index == 0, m, o.font);
    let extra_start = (*p).ruby_start(start, end, m, o.font)?;
    let extra_end = (*p).ruby_end(start, end, m, o.font)?;
    let alignment =
        if (*p).block.rtl != 0 && (*p).block.text_align_defined == 0 && (*p).block.alignment == 1 {
            3
        } else {
            (*p).block.alignment
        };
    let mut spec = layout_line::Spec {
        count,
        page: i32::from(o.width),
        indent,
        extra_start,
        extra_end,
        alignment,
        rtl: (*p).block.rtl,
        reordered: 0,
        last: u8::from(end == (*p).tokens.len()),
    };
    let mut context = Positions {
        paragraph: &*p,
        metrics: m,
        widths,
        text,
        offsets,
        order: &[],
        start,
        font: o.font,
        reordered: false,
    };
    let callbacks = layout_line::Callbacks {
        context: (&mut context as *mut Positions).cast(),
        word: line_word,
        gap: line_gap,
    };
    let logical = layout_line::metrics(&spec, &callbacks);
    let reordered = ((*p).block.rtl != 0 || (*p).has_rtl)
        && if let Some(engine) = m.engine.as_ref() {
            engine.word_order(text, offsets, (*p).block.rtl != 0, order)
        } else {
            (m.visual_order.unwrap())(
                Span::from_bytes(text),
                offsets.as_ptr(),
                count as u32,
                (*p).block.rtl,
                order.as_mut_ptr(),
            ) != 0
        };
    if reordered {
        context.reordered = true;
        context.order = order;
        spec.reordered = 1;
        let callbacks = layout_line::Callbacks {
            context: (&mut context as *mut Positions).cast(),
            word: line_word,
            gap: line_gap,
        };
        let visual = layout_line::metrics(&spec, &callbacks);
        para_trace!(
            m,
            "reordered-gaps\t{}\t{}\t{}\t{}\t{}",
            visual.width,
            visual.natural_gaps,
            visual.gap_count,
            visual.spare,
            visual.justify
        );
        layout_line::position(&spec, &callbacks, &visual, positions);
    } else {
        layout_line::position(&spec, &callbacks, &logical, positions);
    }
    para_trace!(
        m,
        "line\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
        break_index,
        start,
        end,
        (*p).tokens.offset(start),
        indent,
        extra_start,
        extra_end,
        logical.width,
        logical.gap_count,
        logical.natural_gaps,
        logical.spare,
        logical.justify,
        u8::from(reordered)
    );
    #[cfg(feature = "chapter-trace")]
    for (i, entry) in order.iter().enumerate() {
        para_trace!(
            m,
            "visual\t{}\t{}\t{}",
            i,
            if reordered { usize::from(*entry) } else { i },
            *positions.add(i)
        );
    }
    let mut paragraph_start = u16::MAX;
    if !layout_extract::sources(
        &mut line.0,
        &(*p).store,
        &mut (*p).tokens,
        start,
        if reordered {
            order.as_ptr()
        } else {
            ptr::null()
        },
        (*p).paragraph_emitted,
        &mut paragraph_start,
    ) {
        return None;
    }
    let mut links: Vec<Link> = Vec::new();
    let mut link_ids = Vec::new();
    let mut font_slots = [0u8; 128];
    let mut fonts = [0i32; 3];
    let mut mixed = false;
    for i in 0..count {
        let logical = start + if reordered { usize::from(order[i]) } else { i };
        let token = (*p).tokens.get(logical)?;
        styles.add(i).write(token.style);
        if has_focus {
            boundaries.add(i).write(token.focus);
            let word_offset = usize::from(offsets[logical - start]);
            let remaining = &text[word_offset..];
            let len = remaining
                .iter()
                .position(|b| *b == 0)
                .unwrap_or(remaining.len());
            suffixes.add(i).write(if token.focus == 0 {
                0
            } else {
                m.prefix(
                    (*p).font(logical, o.font),
                    &remaining[..len],
                    (*p).style(logical),
                    token.focus,
                    (*p).block.character_spacing,
                )
            });
        }
        if count <= font_slots.len() {
            let font = (*p).font(logical, o.font);
            let mut slot = 0;
            if font != o.font {
                if let Some(index) = fonts.iter().position(|f| *f == font || *f == 0) {
                    fonts[index] = font;
                    slot = index as u8 + 1;
                }
            }
            font_slots[i] = slot | (token.slot & 128);
            mixed |= font_slots[i] != 0;
        }
        if token.link == 0 || usize::from(token.link) > (*p).links.len() {
            continue;
        }
        let width = i32::from(*widths.0.data.add(logical));
        let x = *positions.add(i);
        let top = if *styles.add(i) & 16 != 0 {
            m.ascender_value(o.font) * 2 / 5
        } else {
            0
        };
        if let Some(index) = link_ids.iter().position(|id| *id == token.link) {
            let span = &mut links[index];
            let left = i32::from(span.x).min(i32::from(x));
            let right = (i32::from(span.x) + i32::from(span.width)).max(i32::from(x) + width);
            span.x = left as i16;
            span.width = (right - left) as i16;
            span.top_lift = span.top_lift.max(top as i16);
        } else {
            links.try_reserve(1).ok()?;
            link_ids.try_reserve(1).ok()?;
            let length = links.len();
            let slot = links.as_mut_ptr().add(length);
            ptr::write_bytes(slot, 0, 1);
            (*slot).x = x;
            (*slot).width = width as i16;
            (*slot).top_lift = top as i16;
            let href = (&(*p).links)[usize::from(token.link) - 1].bytes();
            ptr::copy_nonoverlapping(
                href.as_ptr(),
                ptr::addr_of_mut!((*slot).href).cast(),
                href.len().min(255),
            );
            links.set_len(length + 1);
            link_ids.push(token.link);
        }
    }
    #[cfg(feature = "chapter-trace")]
    {
        for (i, entry) in order.iter().enumerate() {
            let source = start + if reordered { usize::from(*entry) } else { i };
            let off = usize::from(offsets[source - start]);
            let raw = &text[off..];
            let len = raw.iter().position(|b| *b == 0).unwrap_or(raw.len());
            para_trace!(
                m,
                "output\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
                i,
                super::paragraph::Hex(&raw[..len]),
                *positions.add(i),
                *styles.add(i),
                (*p).tokens.get(source)?.focus,
                *line.0.data.cast::<u32>().add(i * 2),
                *line.0.data.cast::<u32>().add(i * 2 + 1),
                super::paragraph::Hex((*p).ruby_text(start + i))
            );
        }
        for link in &links {
            let len = link.href.iter().position(|b| *b == 0).unwrap_or(256);
            para_trace!(
                m,
                "link\t{}\t{}\t{}\t{}",
                super::paragraph::Hex(&link.href[..len]),
                link.x,
                link.width,
                link.top_lift
            );
        }
        para_trace!(m, "paragraph-start-word\t{}", paragraph_start);
    }
    if reordered {
        for entry in order.iter_mut() {
            *entry = offsets[usize::from(*entry)];
        }
    }
    let block = (*p).block;
    let output = Output {
        text: Span::from_bytes(text),
        offsets: if reordered {
            order.as_ptr()
        } else {
            offsets.as_ptr()
        },
        count: count as u32,
        positions,
        styles,
        boundaries: if has_focus { boundaries } else { ptr::null() },
        suffixes: if has_focus { suffixes } else { ptr::null() },
        sources: line.0.data.cast(),
        ruby: ruby.as_ptr(),
        links: links.as_ptr(),
        link_count: links.len() as u32,
        block: &block,
        paragraph_start,
        visible_offset: (*p).tokens.offset(start),
        font_slots: if mixed {
            font_slots.as_ptr()
        } else {
            ptr::null()
        },
        fonts: fonts.as_ptr(),
    };
    if (o.emit)(o.context, &output) == 0 {
        return None;
    }
    Some(())
}

/// # Safety
/// All owners/callbacks remain live and serialized. Sink may query paragraph scalars,
/// but cannot change or destroy the paragraph; metrics callbacks cannot reenter it.
pub unsafe fn layout(p: *mut Paragraph, m: &Metrics, o: &Options) {
    if let Some(fill) = m.native {
        let mut provider = core::mem::MaybeUninit::uninit();
        if fill(m.context, provider.as_mut_ptr()) != 0 {
            let engine = crate::textm::native::Engine::new(provider.assume_init());
            let metrics = Metrics {
                engine: &engine,
                ..*m
            };
            return layout_inner(p, &metrics, o);
        }
    }
    layout_inner(p, m, o);
}
unsafe fn layout_inner(p: *mut Paragraph, m: &Metrics, o: &Options) {
    let stopped = || !o.stop.is_null() && *o.stop != 0;
    if (*p).tokens.is_empty() || o.max_lines == 0 || stopped() {
        return;
    }
    (*p).block.character_spacing = o.tracking;
    (*p).spacing = o.spacing;
    if (*p).block.direction_defined == 0 && (*p).has_rtl {
        for i in 0..(*p).tokens.len().min(3) {
            if crate::bidi::starts_with_rtl((*p).word(i), 5) {
                (*p).block.rtl = 1;
                break;
            }
        }
    }
    (*p).natural = (*p).block.alignment == 0
        || (*p).block.alignment == if (*p).block.rtl != 0 { 3 } else { 1 };
    if prepare(&*p, m, o.font).is_none() {
        (*p).dropped = true;
        return;
    }
    para_trace!(
        m,
        "paragraph\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
        o.width,
        o.font,
        o.include_last,
        o.tracking,
        o.spacing,
        if o.max_lines == u32::MAX {
            usize::MAX
        } else {
            o.max_lines as usize
        },
        u8::from((*p).first_consumed),
        u8::from((*p).paragraph_emitted),
        (*p).block.rtl
    );
    #[cfg(feature = "chapter-trace")]
    for i in 0..(*p).tokens.len() {
        let t = (*p).tokens.get(i).unwrap();
        para_trace!(
            m,
            "token\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
            i,
            super::paragraph::Hex((*p).word(i)),
            (*p).tokens.offset(i),
            t.style,
            t.flags,
            t.focus,
            t.slot,
            t.link,
            (*p).font(i, o.font)
        );
    }
    let Some(mut widths) = (*p).widths(m, o.font) else {
        (*p).dropped = true;
        return;
    };
    let max_lines = if o.max_lines == u32::MAX {
        usize::MAX
    } else {
        o.max_lines as usize
    };
    let Some(breaks) = paragraph_layout::breaks(
        p,
        m,
        &mut widths,
        o.font,
        i32::from(o.width),
        max_lines,
        o.language,
    ) else {
        (*p).dropped = true;
        return;
    };
    let indices = slice::from_raw_parts(breaks.0.data, breaks.0.len);
    let all = indices.last().copied() == Some((*p).tokens.len());
    let count = indices.len() - usize::from(o.include_last == 0 && all);
    para_trace!(m, "breaks\t{}\t{}", count, u8::from(all));
    #[cfg(feature = "chapter-trace")]
    for (i, end) in indices.iter().enumerate() {
        para_trace!(m, "break\t{}\t{}", i, end);
    }
    #[cfg(feature = "chapter-trace")]
    for i in 0..widths.0.len {
        para_trace!(m, "width\t{}\t{}", i, *widths.0.data.add(i));
    }
    for i in 0..count {
        if stopped() {
            return;
        }
        (*p).lines_after = if o.include_last != 0 && all {
            (count - 1 - i) as i32
        } else {
            -1
        };
        if extract(p, m, o, &widths, indices, i).is_none() {
            (*p).dropped = true;
        }
    }
    (*p).lines_after = -1;
    if count != 0 {
        (*p).first_consumed = true;
        (*p).paragraph_emitted = true;
        let consumed = indices[count - 1];
        for i in 0..consumed {
            if let Some(token) = (*p).tokens.get(i) {
                (*p).store.release(token.word);
            }
        }
        (*p).tokens.erase_prefix(consumed);
        let consumed = consumed.min((*p).ruby.len());
        (*p).ruby.erase_prefix(consumed);
    }
}
unsafe fn prepare(p: &Paragraph, m: &Metrics, font: i32) -> Option<()> {
    let mut fonts = [font, 0, 0, 0];
    let mut count = 1;
    for slot in 1..usize::from(p.slots) {
        let font = p.fonts[slot];
        if font != 0 && !fonts[..count].contains(&font) {
            fonts[count] = font;
            count += 1;
        }
    }
    if !(0..count).any(|i| m.sd(fonts[i])) {
        return Some(());
    }
    let mut styles = 0;
    let mut small = false;
    for i in 0..p.tokens.len() {
        let t = p.tokens.get(i)?;
        styles |= 1 << (t.style & 3);
        small |= t.slot & 128 != 0;
    }
    if styles == 0 {
        styles = 1;
    }
    let mut segments = Vec::new();
    segments.try_reserve_exact(p.store.chunk_count()).ok()?;
    for i in 0..p.store.chunk_count() {
        let (data, len) = p.store.chunk(i);
        if !data.is_null() {
            segments.push(Span {
                data,
                len: len as u32,
            });
        }
    }
    for font in fonts.iter().take(count) {
        if m.sd(*font) {
            (m.prepare)(
                m.context,
                *font,
                segments.as_ptr(),
                segments.len() as u32,
                u8::from(p.tokens.len() > 1),
                u8::from(p.hyphenation),
                styles,
                u8::from(small),
            );
        }
    }
    Some(())
}
