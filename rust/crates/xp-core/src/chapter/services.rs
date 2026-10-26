//! Borrowed synchronous assembly services. Each callback receives a fresh state reborrow.
use super::{
    style::{Block, Footnote},
    State,
};
use core::ffi::c_void;

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

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub entity_lookup: unsafe extern "C" fn(*mut c_void, *mut State, Span) -> Span,
    pub entity_buffer: unsafe extern "C" fn(*mut c_void, *mut State, u32) -> *mut u8,
    pub list_marker: unsafe extern "C" fn(*mut c_void, *mut State, Span, u32),
    pub preserve_space: unsafe extern "C" fn(*mut c_void, *mut State),
    pub paragraph_empty: unsafe extern "C" fn(*mut c_void, *mut State) -> u8,
    pub paragraph_size: unsafe extern "C" fn(*mut c_void, *mut State) -> u32,
    pub paragraph_scale: unsafe extern "C" fn(*mut c_void, *mut State) -> f32,
    pub paragraph_style: unsafe extern "C" fn(*mut c_void, *mut State, *mut Block),
    pub suppress_indent: unsafe extern "C" fn(*mut c_void, *mut State),
    pub ruby_capacity: unsafe extern "C" fn(*mut c_void, *mut State),
    pub add_link: unsafe extern "C" fn(*mut c_void, *mut State, Span) -> u8,
    pub link_matches: unsafe extern "C" fn(*mut c_void, *mut State, u8, Span) -> u8,
    pub size_slot: unsafe extern "C" fn(*mut c_void, *mut State, f32) -> u8,
    pub set_paragraph_style: unsafe extern "C" fn(*mut c_void, *mut State, *const Block),
    pub word_style: unsafe extern "C" fn(*mut c_void, *mut State, u32) -> u8,
    pub ruby_group: unsafe extern "C" fn(*mut c_void, *mut State, u32, u32, Span),
    pub ruby_append: unsafe extern "C" fn(*mut c_void, *mut State, u32, Span),
    pub add_word: unsafe extern "C" fn(*mut c_void, *mut State, Span, u8, u8, u8, u32, u8, u8, u8),
    pub paragraph_exists: unsafe extern "C" fn(*mut c_void, *mut State) -> u8,
    pub create_paragraph: unsafe extern "C" fn(*mut c_void, *mut State, *const Block),
    pub reset_paragraph: unsafe extern "C" fn(*mut c_void, *mut State),
    pub body_sd: unsafe extern "C" fn(*mut c_void, *mut State) -> u8,
    pub body_space: unsafe extern "C" fn(*mut c_void, *mut State) -> i32,
    pub prepare_text: unsafe extern "C" fn(*mut c_void, *mut State, Span, u8),
    pub text_advance: unsafe extern "C" fn(*mut c_void, *mut State, Span, u8) -> i32,
    pub body_ascender: unsafe extern "C" fn(*mut c_void, *mut State) -> i32,
    pub page_width: unsafe extern "C" fn(*mut c_void, *mut State) -> u16,
    pub page_height: unsafe extern "C" fn(*mut c_void, *mut State) -> u16,
    pub clear_cells: unsafe extern "C" fn(*mut c_void, *mut State),
    pub reserve_cells: unsafe extern "C" fn(*mut c_void, *mut State),
    pub buffered_words: unsafe extern "C" fn(*mut c_void, *mut State) -> u32,
    pub table_stacked: unsafe extern "C" fn(*mut c_void, *mut State) -> u8,
    pub set_stacked: unsafe extern "C" fn(*mut c_void, *mut State, u8),
    pub set_rtl: unsafe extern "C" fn(*mut c_void, *mut State, u8),
    pub reset_table_bytes: unsafe extern "C" fn(*mut c_void, *mut State),
    pub table_bytes: unsafe extern "C" fn(*mut c_void, *mut State) -> u32,
    pub add_table_bytes: unsafe extern "C" fn(*mut c_void, *mut State, u32),
    pub reset_extracted: unsafe extern "C" fn(*mut c_void, *mut State),
    pub extracted: unsafe extern "C" fn(*mut c_void, *mut State) -> i32,
    pub footnote: unsafe extern "C" fn(*mut c_void, *mut State, i32, *const Footnote),
    pub box_at_depth: unsafe extern "C" fn(*mut c_void, *mut State, i32) -> u8,
    pub image: unsafe extern "C" fn(*mut c_void, *mut State, Span, *const u8) -> u8,
    pub measure_table: unsafe extern "C" fn(*mut c_void, *mut State),
    pub start_block: unsafe extern "C" fn(*mut c_void, *mut State, *const Block, u8),
    pub flush_anchor: unsafe extern "C" fn(*mut c_void, *mut State),
    pub open_box: unsafe extern "C" fn(*mut c_void, *mut State, *mut Block, *const u8),
    pub close_box: unsafe extern "C" fn(*mut c_void, *mut State),
    pub line_break: unsafe extern "C" fn(*mut c_void, *mut State, *const Block),
    pub table_fallback: unsafe extern "C" fn(*mut c_void, *mut State),
    pub close_cell: unsafe extern "C" fn(*mut c_void, *mut State),
    pub finish_row: unsafe extern "C" fn(*mut c_void, *mut State),
    pub make_pages: unsafe extern "C" fn(*mut c_void, *mut State, u8, u8),
    pub horizontal_rule: unsafe extern "C" fn(*mut c_void, *mut State, *const Block),
    pub direction: unsafe extern "C" fn(*mut c_void, *mut State, u8, u8),
    pub anchor_is_toc: unsafe extern "C" fn(*mut c_void, *mut State, Span) -> u8,
    pub anchor_count: unsafe extern "C" fn(*mut c_void, *mut State) -> u32,
    pub has_anchor: unsafe extern "C" fn(*mut c_void, *mut State) -> u8,
    pub defer_anchor: unsafe extern "C" fn(*mut c_void, *mut State, Span),
    pub trace: unsafe extern "C" fn(*mut c_void, *mut State, *const Trace),
}

impl State {
    pub fn entity_lookup(&mut self, spelling: Span) -> Span {
        let callback = unsafe { (*self.services).entity_lookup };
        unsafe { callback(self.ctx, self, spelling) }
    }

    pub fn entity_buffer(&mut self, length: u32) -> *mut u8 {
        let callback = unsafe { (*self.services).entity_buffer };
        unsafe { callback(self.ctx, self, length) }
    }

    pub(super) fn call_list_marker(&mut self, text: Span, offset: u32) {
        let callback = unsafe { (*self.services).list_marker };
        let state = self as *mut State;
        unsafe {
            callback(self.ctx, state, text.reborrow(state), offset);
        }
    }
    pub(super) fn call_preserve_space(&mut self) {
        let callback = unsafe { (*self.services).preserve_space };
        unsafe {
            callback(self.ctx, self);
        }
    }

    #[inline]
    pub(super) fn call_paragraph_empty(&mut self) -> u8 {
        let callback = unsafe { (*self.services).paragraph_empty };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_paragraph_size(&mut self) -> u32 {
        let callback = unsafe { (*self.services).paragraph_size };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_paragraph_scale(&mut self) -> f32 {
        let callback = unsafe { (*self.services).paragraph_scale };
        unsafe { callback(self.ctx, self) }
    }
    #[inline]
    pub(super) fn call_paragraph_style(&mut self, out: *mut Block) {
        let callback = unsafe { (*self.services).paragraph_style };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, out) }
    }
    #[inline]
    pub(super) fn call_suppress_indent(&mut self) {
        let callback = unsafe { (*self.services).suppress_indent };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_ruby_capacity(&mut self) {
        let callback = unsafe { (*self.services).ruby_capacity };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_add_link(&mut self, href: Span) -> u8 {
        let callback = unsafe { (*self.services).add_link };
        let state = self as *mut State;
        let href = href.reborrow(state);
        unsafe { callback(self.ctx, state, href) }
    }
    #[inline]
    pub(super) fn call_link_matches(&mut self, id: u8, href: Span) -> u8 {
        let callback = unsafe { (*self.services).link_matches };
        let state = self as *mut State;
        let href = href.reborrow(state);
        unsafe { callback(self.ctx, state, id, href) }
    }
    #[inline]
    pub(super) fn call_size_slot(&mut self, scale: f32) -> u8 {
        let callback = unsafe { (*self.services).size_slot };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, scale) }
    }
    #[inline]
    pub(super) fn call_set_paragraph_style(&mut self, style: *const Block) {
        let callback = unsafe { (*self.services).set_paragraph_style };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, style) }
    }
    #[inline]
    pub(super) fn call_word_style(&mut self, index: u32) -> u8 {
        let callback = unsafe { (*self.services).word_style };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, index) }
    }
    #[inline]
    pub(super) fn call_ruby_group(&mut self, index: u32, count: u32, text: Span) {
        let callback = unsafe { (*self.services).ruby_group };
        let state = self as *mut State;
        let text = text.reborrow(state);
        unsafe { callback(self.ctx, state, index, count, text) }
    }
    #[inline]
    pub(super) fn call_ruby_append(&mut self, index: u32, text: Span) {
        let callback = unsafe { (*self.services).ruby_append };
        let state = self as *mut State;
        let text = text.reborrow(state);
        unsafe { callback(self.ctx, state, index, text) }
    }
    #[inline]
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
        let callback = unsafe { (*self.services).add_word };
        let state = self as *mut State;
        let word = word.reborrow(state);
        unsafe {
            callback(
                self.ctx, state, word, style, underline, attach, offset, link, slot, no_space,
            )
        }
    }
    #[inline]
    pub(super) fn call_paragraph_exists(&mut self) -> u8 {
        let callback = unsafe { (*self.services).paragraph_exists };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_create_paragraph(&mut self, style: *const Block) {
        let callback = unsafe { (*self.services).create_paragraph };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, style) }
    }
    #[inline]
    pub(super) fn call_reset_paragraph(&mut self) {
        let callback = unsafe { (*self.services).reset_paragraph };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_body_sd(&mut self) -> u8 {
        let callback = unsafe { (*self.services).body_sd };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_body_space(&mut self) -> i32 {
        let callback = unsafe { (*self.services).body_space };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_prepare_text(&mut self, text: Span, style: u8) {
        let callback = unsafe { (*self.services).prepare_text };
        let state = self as *mut State;
        let text = text.reborrow(state);
        unsafe { callback(self.ctx, state, text, style) }
    }
    #[inline]
    pub(super) fn call_text_advance(&mut self, text: Span, style: u8) -> i32 {
        let callback = unsafe { (*self.services).text_advance };
        let state = self as *mut State;
        let text = text.reborrow(state);
        unsafe { callback(self.ctx, state, text, style) }
    }
    #[inline]
    pub(super) fn call_body_ascender(&mut self) -> i32 {
        let callback = unsafe { (*self.services).body_ascender };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_page_width(&mut self) -> u16 {
        let callback = unsafe { (*self.services).page_width };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_page_height(&mut self) -> u16 {
        let callback = unsafe { (*self.services).page_height };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_clear_cells(&mut self) {
        let callback = unsafe { (*self.services).clear_cells };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_reserve_cells(&mut self) {
        let callback = unsafe { (*self.services).reserve_cells };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_buffered_words(&mut self) -> u32 {
        let callback = unsafe { (*self.services).buffered_words };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_table_stacked(&mut self) -> u8 {
        let callback = unsafe { (*self.services).table_stacked };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_set_stacked(&mut self, value: u8) {
        let callback = unsafe { (*self.services).set_stacked };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, value) }
    }
    #[inline]
    pub(super) fn call_set_rtl(&mut self, value: u8) {
        let callback = unsafe { (*self.services).set_rtl };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, value) }
    }
    #[inline]
    pub(super) fn call_reset_table_bytes(&mut self) {
        let callback = unsafe { (*self.services).reset_table_bytes };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_table_bytes(&mut self) -> u32 {
        let callback = unsafe { (*self.services).table_bytes };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_add_table_bytes(&mut self, count: u32) {
        let callback = unsafe { (*self.services).add_table_bytes };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, count) }
    }
    #[inline]
    pub(super) fn call_reset_extracted(&mut self) {
        let callback = unsafe { (*self.services).reset_extracted };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_extracted(&mut self) -> i32 {
        let callback = unsafe { (*self.services).extracted };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_footnote(&mut self, index: i32) {
        let callback = unsafe { (*self.services).footnote };
        let state = self as *mut State;
        unsafe {
            callback(
                self.ctx,
                state,
                index,
                core::ptr::addr_of!((*state).footnote),
            )
        }
    }
    #[inline]
    pub(super) fn call_box_at_depth(&mut self, depth: i32) -> u8 {
        let callback = unsafe { (*self.services).box_at_depth };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, depth) }
    }
    #[inline]
    pub(super) fn call_image(&mut self, src: Span, css: *const u8) -> u8 {
        let callback = unsafe { (*self.services).image };
        let state = self as *mut State;
        let src = src.reborrow(state);
        unsafe { callback(self.ctx, state, src, css) }
    }
    #[inline]
    pub(super) fn call_measure_table(&mut self) {
        let callback = unsafe { (*self.services).measure_table };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_start_block(&mut self, style: *const Block, paragraph_end: u8) {
        let callback = unsafe { (*self.services).start_block };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, style, paragraph_end) }
    }
    #[inline]
    pub(super) fn call_flush_anchor(&mut self) {
        let callback = unsafe { (*self.services).flush_anchor };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_open_box(&mut self, style: *mut Block, css: *const u8) {
        let callback = unsafe { (*self.services).open_box };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, style, css) }
    }
    #[inline]
    pub(super) fn call_close_box(&mut self) {
        let callback = unsafe { (*self.services).close_box };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_line_break(&mut self, style: *const Block) {
        let callback = unsafe { (*self.services).line_break };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, style) }
    }
    #[inline]
    pub(super) fn call_table_fallback(&mut self) {
        let callback = unsafe { (*self.services).table_fallback };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_close_cell(&mut self) {
        let callback = unsafe { (*self.services).close_cell };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_finish_row(&mut self) {
        let callback = unsafe { (*self.services).finish_row };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_make_pages(&mut self, last_line: u8, paragraph_end: u8) {
        let callback = unsafe { (*self.services).make_pages };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, last_line, paragraph_end) }
    }
    #[inline]
    pub(super) fn call_horizontal_rule(&mut self, style: *const Block) {
        let callback = unsafe { (*self.services).horizontal_rule };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, style) }
    }
    #[inline]
    pub(super) fn call_direction(&mut self, defined: u8, rtl: u8) {
        let callback = unsafe { (*self.services).direction };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, defined, rtl) }
    }
    #[inline]
    pub(super) fn call_anchor_is_toc(&mut self, id: Span) -> u8 {
        let callback = unsafe { (*self.services).anchor_is_toc };
        let state = self as *mut State;
        let id = id.reborrow(state);
        unsafe { callback(self.ctx, state, id) }
    }
    #[inline]
    pub(super) fn call_anchor_count(&mut self) -> u32 {
        let callback = unsafe { (*self.services).anchor_count };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_has_anchor(&mut self) -> u8 {
        let callback = unsafe { (*self.services).has_anchor };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state) }
    }
    #[inline]
    pub(super) fn call_defer_anchor(&mut self, id: Span) {
        let callback = unsafe { (*self.services).defer_anchor };
        let state = self as *mut State;
        let id = id.reborrow(state);
        unsafe { callback(self.ctx, state, id) }
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
        let callback = unsafe { (*self.services).trace };
        let state = self as *mut State;
        unsafe { callback(self.ctx, state, record) }
    }
}
