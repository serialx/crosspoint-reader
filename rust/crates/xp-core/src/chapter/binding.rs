//! Direct semantic-to-assembly calls. Flow is a temporary scalar snapshot only.
use super::*;
use assembly::{Assembly, Flow, ImageStyle, Paragraph, Position, Result, Sink};
type Assembler = Assembly<OwnedParagraph, Payload>;

impl State {
    fn sink(&self) -> PageSink {
        PageSink {
            callbacks: self.callbacks,
            context: self.ctx,
            storage: self.page_storage.get(),
            link_index: None,
        }
    }
    #[inline(never)]
    fn flow(&self) -> Flow {
        Flow {
            depth: self.depth,
            table_depth: self.table_depth,
            parent: self.parent(),
            block_depth: self.blocks.len() as u32,
            marker_only: self.marker_only,
            continued: self.continued,
            inline_scale: self.inline_size.valid.then_some(self.inline_size.scale),
            drop_cap: self.drop_cap,
            page_break: self.page_break,
            cell_open: self.inside_cell,
            cell_bottom: self.parent().padding_bottom,
            columns: self.column_count,
            column_widths: self.column_widths,
            table_border: self.table_border,
            whitespace: self.space,
            position: Position {
                paragraph: self.paragraph_index,
                list: self.list_index,
                visible: self.visible_offset,
                next_y: self.assembly.next_y,
            },
        }
    }
    fn bind_assembly(&mut self) {
        self.paragraph = self
            .assembly
            .paragraph
            .as_ref()
            .map_or(core::ptr::null_mut(), OwnedParagraph::pointer);
        self.oom |=
            self.assembly.oom || unsafe { self.paragraph.as_ref().is_some_and(|p| p.dropped) };
        self.stopped |= self.assembly.stopped;
    }
    fn assemble<T>(
        &mut self,
        apply: impl FnOnce(&mut Assembler, &mut Flow, &mut PageSink) -> Result<T>,
    ) -> Option<T> {
        let mut flow = self.flow();
        self.assemble_flow(&mut flow, apply)
    }
    fn assemble_flow<T>(
        &mut self,
        flow: &mut Flow,
        apply: impl FnOnce(&mut Assembler, &mut Flow, &mut PageSink) -> Result<T>,
    ) -> Option<T> {
        let mut sink = self.sink();
        let result = apply(&mut self.assembly, flow, &mut sink);
        self.apply_flow(flow);
        match result {
            Ok(value) => Some(value),
            Err(assembly::Error::Allocation) => {
                self.oom = true;
                None
            }
            Err(assembly::Error::Stopped) => {
                self.stopped = true;
                None
            }
        }
    }
    #[inline(never)]
    fn apply_flow(&mut self, flow: &Flow) {
        self.marker_only = flow.marker_only;
        self.continued = flow.continued;
        if flow.inline_scale.is_none() {
            self.inline_size = InlineSize::default();
        }
        self.drop_cap = flow.drop_cap;
        self.page_break = flow.page_break;
        self.inside_cell = flow.cell_open;
        self.bind_assembly();
    }
    pub fn begin_assembly(&mut self) {
        self.notify(feedback::Notify::BeginBlocks);
        self.notify(feedback::Notify::BeginLists);
        self.assemble(|a, f, s| a.begin(f, s));
    }
    pub fn finish_assembly(&mut self) -> bool {
        self.assemble(|a, f, s| a.finish(f, s));
        !self.oom && !self.stopped
    }
    pub(super) fn call_create_paragraph(&mut self, style: *const Block) {
        let style = unsafe { *style };
        match OwnedParagraph::create(style, self.assembly.options, &mut self.sink()) {
            Ok(paragraph) => self.assembly.paragraph = Some(paragraph),
            Err(_) => self.oom = true,
        }
        self.bind_assembly();
    }
    pub(super) fn call_reset_paragraph(&mut self) {
        self.assembly.paragraph = None;
        self.bind_assembly();
    }
    pub(super) fn call_body_sd(&mut self) -> u8 {
        let metrics = unsafe { &*(*self.callbacks).metrics };
        let value = if let Some(e) = self.sink().native_engine() {
            u8::from(e.is_sd(self.assembly.options.font))
        } else {
            unsafe { (metrics.is_sd.unwrap())(self.ctx, self.assembly.options.font) }
        };
        observe!(
            self.sink(),
            "isSdCardFont",
            self.assembly.options.font,
            value
        );
        observe!(self.sink(), "Reply", value);
        value
    }
    pub(super) fn call_body_space(&mut self) -> i32 {
        let value = self.sink().space_width(self.assembly.options.font);
        observe!(self.sink(), "Reply", value);
        value
    }
    pub(super) fn call_prepare_text(&mut self, text: Span, style: u8) {
        let metrics = unsafe { &*(*self.callbacks).metrics };
        let span = crate::para::paragraph::Span {
            data: text.data,
            len: text.len,
        };
        unsafe {
            (metrics.prepare)(
                self.ctx,
                self.assembly.options.font,
                &span,
                1,
                0,
                0,
                style,
                0,
            );
        }
    }
    pub(super) fn call_text_advance(&mut self, text: Span, style: u8) -> i32 {
        // This path measures table tokens; the old callback bounded and terminated them.
        if text.len >= 40 {
            self.oom = true;
            return 0;
        }
        let mut bytes = [0; 40];
        unsafe {
            core::ptr::copy_nonoverlapping(text.data, bytes.as_mut_ptr(), text.len as usize);
        }
        let metrics = unsafe { &*(*self.callbacks).metrics };
        let span = crate::para::paragraph::Span {
            data: bytes.as_ptr(),
            len: text.len,
        };
        let value = if let Some(e) = self.sink().native_engine() {
            e.advance(
                self.assembly.options.font,
                &bytes[..text.len as usize],
                style,
                0,
                -1,
                crate::textm::MeasureMode::Layout,
            )
        } else {
            unsafe {
                (metrics.advance.unwrap())(self.ctx, self.assembly.options.font, span, style, 0)
            }
        };
        observe!(
            self.sink(),
            "getTextAdvanceX",
            self.assembly.options.font,
            &bytes[..text.len as usize],
            style,
            value
        );
        observe!(self.sink(), "Reply", value);
        value
    }
    pub(super) fn call_body_ascender(&mut self) -> i32 {
        let value = self.sink().ascender(self.assembly.options.font);
        observe!(self.sink(), "Reply", value);
        value
    }
    pub(super) fn call_page_width(&mut self) -> u16 {
        self.assembly.options.width
    }
    pub(super) fn call_page_height(&mut self) -> u16 {
        self.assembly.options.height
    }
    pub(super) fn call_clear_cells(&mut self) {
        self.assembly.clear_cells();
    }
    pub(super) fn call_reserve_cells(&mut self) {
        if self.assembly.reserve_cells().is_err() {
            self.oom = true;
        }
    }
    pub(super) fn call_buffered_words(&mut self) -> u32 {
        self.assembly.buffered_words()
    }
    pub(super) fn call_table_stacked(&mut self) -> u8 {
        self.assembly.table_stacked.into()
    }
    pub(super) fn call_set_stacked(&mut self, value: u8) {
        self.assembly.table_stacked = value != 0;
    }
    pub(super) fn call_set_rtl(&mut self, value: u8) {
        self.assembly.table_rtl = value != 0;
    }
    pub(super) fn call_reset_table_bytes(&mut self) {
        self.assembly.table_bytes = 0;
    }
    pub(super) fn call_table_bytes(&mut self) -> u32 {
        self.assembly.table_bytes
    }
    pub(super) fn call_add_table_bytes(&mut self, count: u32) {
        self.assembly.table_bytes = self.assembly.table_bytes.wrapping_add(count);
    }
    pub(super) fn call_reset_extracted(&mut self) {
        self.assembly.extracted = 0;
    }
    pub(super) fn call_extracted(&mut self) -> i32 {
        self.assembly.extracted
    }
    pub(super) fn call_footnote(&mut self, index: i32) {
        let note = &self.footnote;
        observe!(
            self.sink(),
            "Footnote",
            index,
            &note.number[..note.number.iter().position(|b| *b == 0).unwrap_or(32)],
            &note.href[..note.href.iter().position(|b| *b == 0).unwrap_or(256)]
        );
        if self.assembly.add_footnote(index, note).is_err() {
            self.oom = true;
        }
    }
    pub(super) fn call_box_at_depth(&mut self, depth: i32) -> u8 {
        self.assembly.box_at_depth(depth).into()
    }
    #[inline(never)]
    pub(super) fn call_image(&mut self, src: Span, css: *const u8) -> u8 {
        let css = unsafe { *css.cast::<Css>() };
        let src = if src.len == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(src.data, src.len as usize) }
        };
        let Some(prepared) =
            self.assemble(|a, _, s| a.prepare_image(src, ImageStyle::from_css(&css), s))
        else {
            return 2;
        };
        let Some(prepared) = prepared else {
            return 0;
        };
        if self.word_len > 0 {
            self.flush_word();
        }
        self.assemble(|a, f, s| a.place_image(prepared, f, s))
            .map_or(2, |_| 1)
    }
    pub(super) fn call_measure_table(&mut self) {
        unsafe {
            ((*self.callbacks).measure_table)(self.ctx, self);
        }
    }
    pub(super) fn call_start_block(&mut self, style: *const Block, end: u8) {
        let style = unsafe { *style };
        self.assemble(|a, f, s| a.start_block(style, end != 0, f, s));
    }
    pub(super) fn call_flush_anchor(&mut self) {
        self.assemble(|a, f, s| a.flush_anchor(f, s));
    }
    pub(super) fn call_open_box(&mut self, style: *mut Block, css: *const u8) {
        let css = unsafe { *css.cast::<Css>() };
        if !self.assembly.accepts_box(&css, self.table_depth) {
            return;
        }
        if self.word_len > 0 {
            self.flush_word();
        }
        self.assemble(|a, f, s| a.open_box(unsafe { &mut *style }, &css, f, s));
    }
    #[inline(never)]
    pub(super) fn call_close_box(&mut self) {
        self.assemble(|a, f, s| a.close_box(f, s));
    }
    pub(super) fn call_line_break(&mut self, style: *const Block) {
        let style = unsafe { *style };
        self.assemble(|a, f, s| a.break_text_line(style, f, s));
    }
    pub(super) fn call_table_fallback(&mut self) {
        self.assemble(|a, f, s| a.fallback_row(f, s));
    }
    fn closing_cell_flow(&mut self) -> Flow {
        let open = self.inside_cell;
        let bottom = self.parent().padding_bottom;
        if open {
            self.inside_cell = false;
            self.blocks.pop();
        }
        Flow {
            cell_open: open,
            cell_bottom: bottom,
            ..self.flow()
        }
    }
    pub(super) fn call_close_cell(&mut self) {
        let mut f = self.closing_cell_flow();
        self.assemble_flow(&mut f, |a, f, s| a.close_cell(f, s));
    }
    pub(super) fn call_finish_row(&mut self) {
        let mut f = self.closing_cell_flow();
        self.assemble_flow(&mut f, |a, f, s| a.finish_row(f, s));
    }
    pub(super) fn call_make_pages(&mut self, last: u8, end: u8) {
        self.assemble(|a, f, s| a.make_pages(last != 0, end != 0, f, s));
    }
    #[inline(never)]
    pub(super) fn call_horizontal_rule(&mut self, style: *const Block) {
        let style = unsafe { *style };
        if self.word_len > 0 {
            self.flush_word();
        }
        self.assemble(|a, f, s| a.horizontal_rule(style, f, s));
    }
    pub(super) fn call_direction(&mut self, defined: u8, rtl: u8) {
        self.assembly.direction(defined != 0, rtl != 0);
    }
    pub(super) fn call_anchor_is_toc(&mut self, id: Span) -> u8 {
        unsafe { ((*self.callbacks).anchor_is_toc)(self.ctx, id) }
    }
    pub(super) fn call_anchor_count(&mut self) -> u32 {
        unsafe { ((*self.callbacks).anchor_count)(self.ctx) }
    }
    pub(super) fn call_has_anchor(&mut self) -> u8 {
        (!self.sink().pending_anchor().is_empty()).into()
    }
    pub(super) fn call_defer_anchor(&mut self, id: Span) {
        unsafe {
            ((*self.callbacks).defer_anchor)(self.ctx, id);
        }
    }
}

#[cfg(feature = "chapter-test")]
impl State {
    pub fn test_paragraph(&mut self) -> *mut crate::para::paragraph::Paragraph {
        self.paragraph
    }
    pub fn test_assembly(&mut self, operation: u32, value: u32) -> u32 {
        match operation {
            0..=6 => return self.assembly.test_query(operation),
            7 => {
                return self
                    .sink()
                    .font_variant(self.assembly.options.font, f32::from_bits(value))
                    as u32
            }
            8 => return self.assembly.can_carry(value as usize).into(),
            100 => self.call_create_paragraph(&Block::default()),
            101 => self.call_reset_paragraph(),
            102 => self.call_make_pages(1, 1),
            103 => self.call_finish_row(),
            104 => {
                self.assemble(|a, f, s| {
                    if let Some(page) = a.page.take() {
                        s.complete(page, f.position)?;
                    }
                    Ok(())
                });
            }
            105 => self.assembly.page = None,
            106 => {
                self.assemble(|a, _, _| a.test_buffer_cell());
            }
            107 => self.assembly.next_y = value as i16,
            108 => self.assembly.test_recent(6, value),
            109 => self.assembly.test_recent(0, value),
            110 => self.assembly.test_recent(1, value),
            111 => self.call_start_block(&Block::default(), 1),
            _ => {}
        }
        0
    }
    pub fn test_carry(&mut self, carry: u32, lines: u32, offset: u32) {
        self.assemble(|a, f, s| a.break_carrying(carry as usize, lines as usize, offset, f, s));
    }
    pub fn test_note(&mut self, note: Footnote) {
        self.assemble(|a, _, _| {
            a.page
                .as_mut()
                .ok_or(assembly::Error::Allocation)?
                .add_footnote(&note)
        });
    }
    pub fn test_link(&mut self, link: assembly::Link) {
        self.assemble(|a, _, _| {
            a.page
                .as_mut()
                .ok_or(assembly::Error::Allocation)?
                .add_link(&link)
        });
    }
}
