//! Borrowed spans, diagnostic records, and direct paragraph operations.
use super::{style::Block, State};
unsafe fn span_bytes<'a>(span: Span) -> &'a [u8] {
    if span.len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(span.data, span.len as usize)
    }
}

#[derive(Clone, Copy)]
#[repr(C)]
pub struct Span {
    pub data: *const u8,
    pub len: u32,
}
impl Span {
    pub fn new(bytes: &[u8]) -> Self {
        Self {
            data: bytes.as_ptr(),
            len: bytes.len() as u32,
        }
    }
    fn reborrow(self, state: *mut State) -> Self {
        let base = state as usize;
        let address = self.data as usize;
        if address >= base && address - base < core::mem::size_of::<State>() {
            // Inline word/href storage uses the callback's fresh mutable reborrow.
            Self {
                data: unsafe { state.cast::<u8>().add(address - base) },
                len: self.len,
            }
        } else {
            self
        }
    }
}

#[repr(C)]
pub struct Trace {
    pub kind: u32,
    pub words: [u32; 10],
    pub first: Span,
    pub second: Span,
    pub block: *const Block,
}

impl State {
    pub fn entity_lookup(&mut self, spelling: Span) -> Span {
        unsafe { ((*self.callbacks).entity_lookup)(self.ctx, spelling) }
    }
    pub fn entity_buffer(&mut self, length: u32) -> *mut u8 {
        unsafe { ((*self.callbacks).entity_buffer)(self.ctx, length) }
    }

    pub(super) fn call_list_marker(&mut self, text: Span, offset: u32) {
        #[cfg(feature = "chapter-trace")]
        self.trace(9, &[offset], text, Span::new(&[]), core::ptr::null());
        self.call_add_word(text, 0, 0, 0, offset, 0, 0, 0);
    }
    pub(super) fn call_preserve_space(&mut self) {
        if let Some(p) = unsafe { self.paragraph.as_mut() } {
            p.first_consumed = true;
            if p.block.alignment == 0 {
                p.block.alignment = 1;
            }
        }
    }
    pub(super) fn call_paragraph_empty(&mut self) -> u8 {
        unsafe { self.paragraph.as_ref().is_none_or(|p| p.tokens.is_empty()) as u8 }
    }
    pub(super) fn call_paragraph_size(&mut self) -> u32 {
        unsafe { self.paragraph.as_ref().map_or(0, |p| p.tokens.len() as u32) }
    }
    pub(super) fn call_paragraph_scale(&mut self) -> f32 {
        unsafe { self.paragraph.as_ref().map_or(1.0, |p| p.block.font_scale) }
    }
    pub(super) fn call_paragraph_style(&mut self, out: *mut Block) {
        unsafe {
            out.write(
                self.paragraph
                    .as_ref()
                    .map_or_else(Block::default, |p| p.block),
            );
        }
    }
    pub(super) fn call_suppress_indent(&mut self) {
        if let Some(p) = unsafe { self.paragraph.as_mut() } {
            p.first_consumed = true;
        }
    }
    pub(super) fn call_ruby_capacity(&mut self) {}
    pub(super) fn call_add_link(&mut self, href: Span) -> u8 {
        let href = href.reborrow(self);
        unsafe {
            self.paragraph
                .as_mut()
                .map_or(0, |p| p.add_link(span_bytes(href)))
        }
    }
    pub(super) fn call_link_matches(&mut self, id: u8, href: Span) -> u8 {
        let href = href.reborrow(self);
        unsafe {
            self.paragraph
                .as_ref()
                .is_some_and(|p| p.link_matches(id, span_bytes(href))) as u8
        }
    }
    pub(super) fn call_size_slot(&mut self, scale: f32) -> u8 {
        unsafe { self.paragraph.as_mut().map_or(0, |p| p.size_slot(scale)) }
    }
    pub(super) fn call_set_paragraph_style(&mut self, style: *const Block) {
        if let Some(p) = unsafe { self.paragraph.as_mut() } {
            p.block = unsafe { *style };
        }
    }
    pub(super) fn call_word_style(&mut self, index: u32) -> u8 {
        unsafe {
            self.paragraph
                .as_ref()
                .map_or(0, |p| p.style(index as usize))
        }
    }
    pub(super) fn call_ruby_group(&mut self, index: u32, count: u32, text: Span) {
        #[cfg(feature = "chapter-trace")]
        self.trace(10, &[index, count], text, Span::new(&[]), core::ptr::null());
        let text = text.reborrow(self);
        if let Some(p) = unsafe { self.paragraph.as_mut() } {
            p.set_ruby_group(index as usize, count as usize, unsafe { span_bytes(text) });
        }
    }
    pub(super) fn call_ruby_append(&mut self, index: u32, text: Span) {
        let text = text.reborrow(self);
        let Some(p) = (unsafe { self.paragraph.as_mut() }) else {
            return;
        };
        let old = p.ruby_text(index as usize);
        let text = unsafe { span_bytes(text) };
        let mut combined = super::buffer::TextBuffer::new();
        if !combined.append(old) || !combined.append(text) {
            p.dropped = true;
            return;
        }
        #[cfg(feature = "chapter-trace")]
        self.trace(
            11,
            &[index],
            Span::new(&combined),
            Span::new(&[]),
            core::ptr::null(),
        );
        unsafe {
            (*self.paragraph).set_ruby(index as usize, &combined);
        }
    }
    #[allow(clippy::too_many_arguments)] // Mirrors the established paragraph operation.
    pub(super) fn call_add_word(
        &mut self,
        word: Span,
        style: u8,
        underline: u8,
        attach: u8,
        offset: u32,
        link: u8,
        slot: u8,
        no_space: u8,
    ) {
        let word = word.reborrow(self);
        if let Some(p) = unsafe { self.paragraph.as_mut() } {
            p.add_word(
                unsafe { span_bytes(word) },
                style,
                underline != 0,
                attach != 0,
                offset,
                link,
                slot,
                no_space != 0,
            );
        } else {
            self.oom = true;
        }
    }
    pub(super) fn call_paragraph_exists(&mut self) -> u8 {
        u8::from(!self.paragraph.is_null())
    }
    #[cfg(feature = "chapter-trace")]
    pub(super) fn trace(
        &mut self,
        kind: u32,
        values: &[u32],
        first: Span,
        second: Span,
        block: *const Block,
    ) {
        let mut record = Trace {
            kind,
            words: [0; 10],
            first,
            second,
            block,
        };
        record.words[..values.len()].copy_from_slice(values);
        self.call_trace(&record);
    }
    #[cfg(feature = "chapter-trace")]
    #[inline]
    pub(super) fn call_trace(&mut self, record: *const Trace) {
        unsafe { ((*self.callbacks).trace)(self.ctx, record) }
    }
}
