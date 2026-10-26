//! Page placement and build-scoped ownership, called directly by EPUB semantics.
extern crate alloc;
use super::style::{Block, Border, Css, Footnote};
use alloc::vec::Vec;
macro_rules! observe {
    ($sink:expr, $name:literal $(, $field:expr)* $(,)?) => {
        #[cfg(feature = "chapter-trace")]
        $sink.trace(crate::chapter::assembly::trace::Observation {
            name: $name,
            fields: &[$(crate::chapter::assembly::trace::Field::from($field)),*],
        });
    };
}
mod boxes;
mod image;
mod model;
pub mod runtime;
mod table;
#[cfg(feature = "chapter-trace")]
pub mod trace;
pub use image::{image_size, ImageResult, ImageStyle, Length};
pub use model::*;
use model::{horizontal, push, reserve_one, vertical_merge};

const MAX_CARRIED: usize = 6;
const MAX_BOXES: usize = 8;
const MAX_COLUMNS: usize = 4;

#[derive(Clone, Copy)]
struct BoxScope {
    depth: i32,
    left: i16,
    right: i16,
    pad_top: i16,
    pad_bottom: i16,
    sides: [Border; 4],
    shaded: bool,
    top: i16,
    bottom: i16,
    continued: bool,
}

pub struct Assembly<P, B> {
    pub options: Options,
    pub paragraph: Option<P>,
    pub page: Option<Page<B>>,
    pub next_y: i16,
    pub stopped: bool,
    pub oom: bool,
    pub completed: i32,
    pub extracted: i32,
    pub table_stacked: bool,
    pub table_rtl: bool,
    pub table_bytes: u32,
    pub image_counter: i32,
    pub image_popup: bool,
    visible: u32,
    visible_set: bool,
    layout_active: bool,
    layout_drop_cap: bool,
    lines_after: u32,
    paragraph_lines: u8,
    keep_lines: u8,
    recent: [u32; MAX_CARRIED],
    recent_count: usize,
    boxes: Vec<BoxScope>,
    cells: Vec<P>,
    pending_footnotes: Vec<(i32, Footnote)>,
}

impl<P: Paragraph<B>, B> Assembly<P, B> {
    pub fn new(options: Options) -> Self {
        Self {
            options,
            paragraph: None,
            page: None,
            next_y: 0,
            stopped: false,
            oom: false,
            completed: 0,
            extracted: 0,
            table_stacked: false,
            table_rtl: false,
            table_bytes: 0,
            image_counter: 0,
            image_popup: false,
            visible: 0,
            visible_set: false,
            layout_active: false,
            layout_drop_cap: false,
            lines_after: 0,
            paragraph_lines: 0,
            keep_lines: 0,
            recent: [0; MAX_CARRIED],
            recent_count: 0,
            boxes: Vec::new(),
            cells: Vec::new(),
            pending_footnotes: Vec::new(),
        }
    }
    fn latch<T>(&mut self, result: Result<T>) -> Result<T> {
        match result {
            Err(Error::Allocation) => self.oom = true,
            Err(Error::Stopped) => self.stopped = true,
            Ok(_) => {}
        }
        result
    }
    fn running(&self) -> Result {
        if self.stopped {
            Err(Error::Stopped)
        } else if self.oom {
            Err(Error::Allocation)
        } else {
            Ok(())
        }
    }
    fn ensure_page(&mut self) {
        if self.page.is_none() {
            self.page = Some(Page::default());
            self.next_y = 0;
            self.visible_set = false;
        }
    }
    fn nonempty_page(&self) -> bool {
        self.page
            .as_ref()
            .is_some_and(|page| !page.elements.is_empty())
    }
    fn add_element(&mut self, element: Element<B>) -> Result {
        self.ensure_page();
        let result = match self.page.as_mut() {
            Some(page) => push(&mut page.elements, element),
            None => Err(Error::Allocation),
        };
        self.latch(result)
    }
    pub fn set_visible<S: Sink<Payload = B>>(&mut self, offset: u32, _sink: &mut S) {
        observe!(_sink, "SetVisibleOffset", offset);
        if !self.visible_set {
            self.visible = if self.completed == 0 { 0 } else { offset };
            self.visible_set = true;
        }
    }
    pub fn begin<S: Sink<Payload = B>>(&mut self, flow: &mut Flow, sink: &mut S) -> Result {
        self.stopped = false;
        self.boxes.clear();
        flow.drop_cap = Default::default();
        self.table_stacked = false;
        self.table_bytes = 0;
        self.cells.clear();
        let style = Block {
            alignment: flow.parent.alignment,
            text_align_defined: 1,
            ..Block::default()
        };
        self.start_block(style, true, flow, sink)
    }
    pub fn defer_anchor<S: Sink<Payload = B>>(&mut self, text: &[u8], sink: &mut S) -> Result {
        let result = sink.defer_anchor(text);
        self.latch(result)
    }
    fn record_anchor<S: Sink<Payload = B>>(&mut self, sink: &mut S) -> Result {
        if sink.pending_anchor().is_empty() {
            return Ok(());
        }
        let result = sink.record_anchor(self.completed as u16);
        self.latch(result)
    }
    pub fn flush_anchor<S: Sink<Payload = B>>(&mut self, flow: &Flow, sink: &mut S) -> Result {
        observe!(
            sink,
            "Anchor",
            sink.pending_anchor(),
            self.completed,
            self.stopped
        );
        self.running()?;
        if sink.pending_anchor().is_empty() {
            return Ok(());
        }
        if sink.anchor_starts_chapter() && self.nonempty_page() {
            self.complete_page(flow, sink)?;
        }
        self.record_anchor(sink)
    }
    fn emit_page<S: Sink<Payload = B>>(&mut self, flow: &Flow, sink: &mut S) -> Result {
        for index in 0..self.boxes.len() {
            let box_scope = self.boxes[index];
            self.emit_box(box_scope, false)?;
            let scope = &mut self.boxes[index];
            if scope.top >= 0 {
                scope.continued = true;
            }
            scope.top = -1;
            scope.bottom = -1;
        }
        let position = Position {
            visible: self.visible,
            next_y: self.next_y,
            ..flow.position
        };
        observe!(
            sink,
            "Page",
            position.paragraph,
            position.list,
            position.visible,
            position.next_y
        );
        let result = sink.complete(self.page.take().unwrap_or_default(), position);
        self.recent_count = 0;
        self.keep_lines = 0;
        self.paragraph_lines = 0;
        self.latch(result)
    }
    fn complete_page<S: Sink<Payload = B>>(&mut self, flow: &Flow, sink: &mut S) -> Result {
        let result = self.emit_page(flow, sink);
        self.completed = self.completed.wrapping_add(1);
        result?;
        self.ensure_page();
        Ok(())
    }
    pub fn apply_page_break<S: Sink<Payload = B>>(
        &mut self,
        flow: &mut Flow,
        sink: &mut S,
    ) -> Result {
        observe!(
            sink,
            "PageBreak",
            flow.page_break,
            self.paragraph
                .as_ref()
                .is_some_and(|p| p.style().break_before != 0)
        );
        let mut requested = core::mem::take(&mut flow.page_break);
        if let Some(paragraph) = self.paragraph.as_mut() {
            let mut style = paragraph.style();
            requested |= style.break_before != 0;
            style.break_before = 0;
            paragraph.set_style(style);
        }
        if requested && self.nonempty_page() {
            self.complete_page(flow, sink)?;
        }
        Ok(())
    }
    pub fn start_block<S: Sink<Payload = B>>(
        &mut self,
        style: Block,
        end: bool,
        flow: &mut Flow,
        sink: &mut S,
    ) -> Result {
        observe!(
            sink,
            "BeginBlock",
            &style,
            end,
            flow.position.paragraph,
            flow.position.list
        );
        self.running()?;
        flow.continued = false;
        if let Some(paragraph) = self.paragraph.as_mut() {
            if paragraph.size() == 0 {
                if end {
                    paragraph.reset_indent();
                }
                paragraph.set_style(vertical_merge(paragraph.style(), style));
                flow.inline_scale = None;
                return self.flush_anchor(flow, sink);
            }
            if flow.marker_only {
                paragraph.set_style(vertical_merge(paragraph.style(), style));
                flow.marker_only = false;
                return self.flush_anchor(flow, sink);
            }
            self.make_pages(true, end, flow, sink)?;
        }
        self.flush_anchor(flow, sink)?;
        flow.drop_cap.first_pending = 0;
        flow.inline_scale = None;
        let mut paragraph = P::create(style, self.options, sink);
        if matches!(paragraph, Err(Error::Allocation)) {
            sink.reclaim_paragraph_memory();
            paragraph = P::create(style, self.options, sink);
        }
        self.paragraph = Some(self.latch(paragraph)?);
        self.extracted = 0;
        flow.marker_only = false;
        Ok(())
    }
    pub fn break_text_line<S: Sink<Payload = B>>(
        &mut self,
        style: Block,
        flow: &mut Flow,
        sink: &mut S,
    ) -> Result {
        observe!(sink, "LineBreak", &style, flow.whitespace);
        let has_text = self.paragraph.as_ref().is_some_and(|p| p.size() != 0);
        if !has_text && self.nonempty_page() {
            self.next_y = (i32::from(self.options.height).min(
                i32::from(self.next_y)
                    + sink.line_height(self.options.font, self.options.compression),
            )) as i16;
        }
        self.start_block(style, false, flow, sink)?;
        if has_text || flow.whitespace {
            if let Some(paragraph) = self.paragraph.as_mut() {
                paragraph.suppress_indent();
            }
        }
        Ok(())
    }
    pub fn direction(&mut self, defined: bool, rtl: bool) {
        if let Some(paragraph) = self.paragraph.as_mut().filter(|p| p.size() == 0) {
            let mut style = paragraph.style();
            style.direction_defined = u8::from(defined);
            style.rtl = u8::from(rtl);
            paragraph.set_style(style);
        }
    }
    #[inline(never)]
    pub fn add_footnote(&mut self, index: i32, note: &Footnote) -> Result {
        let result = push(&mut self.pending_footnotes, (index, *note));
        self.latch(result)
    }
    pub fn make_pages<S: Sink<Payload = B>>(
        &mut self,
        last: bool,
        end: bool,
        flow: &mut Flow,
        sink: &mut S,
    ) -> Result {
        observe!(sink, "EndBlock", last, end, flow.position.visible);
        self.running()?;
        let Some(paragraph) = self.paragraph.as_ref() else {
            return Ok(());
        };
        if paragraph.dropped() {
            self.oom = true;
        }
        let nonempty = paragraph.size() != 0;
        self.ensure_page();
        if nonempty {
            self.apply_page_break(flow, sink)?;
        }
        let line_height = sink.line_height(self.options.font, self.options.compression);
        let Some(mut paragraph) = self.paragraph.take() else {
            return Ok(());
        };
        let result = self.layout_block(&mut paragraph, last, flow, sink);
        let style = paragraph.style();
        self.oom |= paragraph.dropped();
        self.paragraph = Some(paragraph);
        self.latch(result)?;
        if !last {
            return Ok(());
        }
        self.keep_lines = if style.keep_with_next != 0 {
            (usize::from(self.keep_lines) + usize::from(self.paragraph_lines)).min(MAX_CARRIED)
                as u8
        } else {
            0
        };
        self.paragraph_lines = 0;
        self.layout_drop_cap = false;
        if let Some(page) = self.page.as_mut() {
            let result = self
                .pending_footnotes
                .iter()
                .try_for_each(|(_, note)| page.add_footnote(note));
            self.pending_footnotes.clear();
            self.latch(result)?;
        }
        if !end {
            return Ok(());
        }
        if style.margin_bottom > 0 {
            self.next_y = self.next_y.wrapping_add(style.margin_bottom);
        }
        if style.padding_bottom > 0 {
            self.next_y = self.next_y.wrapping_add(style.padding_bottom);
        }
        if self.options.extra_spacing {
            self.next_y = self.next_y.wrapping_add((line_height / 2) as i16);
        }
        Ok(())
    }
    #[inline(never)]
    fn layout_block<S: Sink<Payload = B>>(
        &mut self,
        paragraph: &mut P,
        last: bool,
        flow: &mut Flow,
        sink: &mut S,
    ) -> Result {
        let style = paragraph.style();
        let font = paragraph.prepare_font(
            sink,
            self.options.font,
            flow.inline_scale.unwrap_or(style.font_scale),
        );
        let inset = i32::from(horizontal(style));
        let width = if inset < i32::from(self.options.width) {
            (i32::from(self.options.width) - inset) as u16
        } else {
            self.options.width
        };
        let mut top_applied = self.extracted != 0;
        let layout = Layout {
            font,
            width,
            last,
            character_spacing: self.options.character_spacing,
            word_spacing: self.options.word_spacing,
            max_lines: u32::MAX,
        };
        self.layout_active = true;
        let result = (|| {
            if flow.drop_cap.length > 0 && flow.drop_cap.span_depth < 0 && paragraph.size() != 0 {
                self.layout_drop_cap = true;
                self.apply_top(style, &mut top_applied);
                self.layout_drop_cap(paragraph, layout, flow, sink)?;
            }
            paragraph.layout(sink, layout, |sink, line| {
                self.apply_top(style, &mut top_applied);
                self.add_line(line, flow, sink)
            })
        })();
        self.layout_active = false;
        result
    }
    fn apply_top(&mut self, style: Block, applied: &mut bool) {
        if *applied {
            return;
        }
        if style.margin_top > 0 {
            self.next_y = self.next_y.wrapping_add(style.margin_top);
        }
        if style.padding_top > 0 {
            self.next_y = self.next_y.wrapping_add(style.padding_top);
        }
        *applied = true;
    }
    pub fn finish<S: Sink<Payload = B>>(&mut self, flow: &mut Flow, sink: &mut S) -> Result {
        self.running()?;
        if self.paragraph.is_some() {
            self.make_pages(true, true, flow, sink)?;
            self.running()?;
            self.record_anchor(sink)?;
            self.set_visible(flow.position.visible, sink);
            let result = self.emit_page(flow, sink);
            self.completed = self.completed.wrapping_add(1);
            self.paragraph = None;
            result?;
        }
        self.running()
    }
    pub fn abort(&mut self) {
        self.paragraph = None;
        self.page = None;
        self.cells.clear();
        self.pending_footnotes.clear();
        self.boxes.clear();
    }
    pub fn can_carry(&self, carry: usize) -> bool {
        if carry == 0 {
            return true;
        }
        let Some(page) = &self.page else {
            return false;
        };
        carry <= self.recent_count
            && carry <= MAX_CARRIED
            && page.elements.len() > carry
            && page
                .elements
                .iter()
                .rev()
                .take(carry)
                .all(|e| matches!(e.content, Content::Line(_)))
    }
    pub fn lines_to_carry(&self) -> (usize, usize) {
        let mut paragraph_lines = 0;
        if self.layout_active && !self.layout_drop_cap {
            if self.paragraph_lines == 1 {
                paragraph_lines = 1;
            } else if self.paragraph_lines >= 2 && self.lines_after == 0 {
                paragraph_lines = if self.paragraph_lines >= 3 { 1 } else { 2 };
            }
        }
        let mut carry = paragraph_lines;
        if self.keep_lines > 0 && paragraph_lines == usize::from(self.paragraph_lines) {
            carry += usize::from(self.keep_lines);
        }
        if self.can_carry(carry) {
            (carry, paragraph_lines)
        } else if self.can_carry(paragraph_lines) {
            (paragraph_lines, paragraph_lines)
        } else {
            (0, 0)
        }
    }
    fn heading_carry(&self) -> usize {
        if self.can_carry(usize::from(self.keep_lines)) {
            usize::from(self.keep_lines)
        } else {
            0
        }
    }
    #[inline(never)]
    fn move_carried(
        &mut self,
        next: &mut Page<B>,
        carry: usize,
        offsets: &mut [u32; MAX_CARRIED],
    ) -> Result<i16> {
        let mut top = 0;
        let reserve = next
            .elements
            .try_reserve_exact(carry)
            .map_err(|_| Error::Allocation);
        self.latch(reserve)?;
        if let Some(page) = self.page.as_mut() {
            let start = page.elements.len() - carry;
            top = page.elements[start].y;
            next.elements.extend(page.elements.drain(start..));
            offsets[..carry]
                .copy_from_slice(&self.recent[self.recent_count - carry..self.recent_count]);
            let reserve = next
                .links
                .try_reserve_exact(page.links.len())
                .map_err(|_| Error::Allocation);
            if let Err(error) = reserve {
                return self.latch(Err(error));
            }
            let mut i = 0;
            while i < page.links.len() {
                if i32::from(page.links[i].y) + i32::from(page.links[i].height) > i32::from(top) {
                    next.links.extend(page.links.drain(i..i + 1));
                } else {
                    i += 1;
                }
            }
            let mut i = 0;
            while i < page.footnotes.len() {
                let href = c_string(&page.footnotes[i].href);
                if next.links.iter().any(|link| c_string(&link.href) == href) {
                    if next.footnotes.is_empty()
                        && next
                            .footnotes
                            .try_reserve_exact(page.footnotes.len())
                            .is_err()
                    {
                        return self.latch(Err(Error::Allocation));
                    }
                    if let Err(error) = next.add_footnote(&page.footnotes[i]) {
                        return self.latch(Err(error));
                    }
                    if !page.links.iter().any(|link| c_string(&link.href) == href) {
                        page.footnotes.drain(i..i + 1);
                        continue;
                    }
                }
                i += 1;
            }
        }
        Ok(top)
    }
    pub fn break_carrying<S: Sink<Payload = B>>(
        &mut self,
        carry: usize,
        paragraph_lines: usize,
        offset: u32,
        flow: &Flow,
        sink: &mut S,
    ) -> Result {
        if !self.can_carry(carry) {
            return Ok(());
        }
        let mut next = Page::default();
        let bottom = self.next_y;
        let mut top = 0;
        let mut offsets = [0; MAX_CARRIED];
        if carry > 0 {
            top = self.move_carried(&mut next, carry, &mut offsets)?;
        }
        self.set_visible(offset, sink);
        let result = self.emit_page(flow, sink);
        self.completed = self.completed.wrapping_add(1);
        result?;
        self.next_y = 0;
        self.visible_set = false;
        for (element, offset) in next.elements.iter_mut().zip(offsets) {
            element.y = element.y.wrapping_sub(top);
            self.set_visible(offset, sink);
            self.recent[self.recent_count] = offset;
            self.recent_count += 1;
        }
        if carry > 0 {
            self.next_y = bottom.wrapping_sub(top);
            self.note_content(0, i32::from(self.next_y));
        }
        for link in &mut next.links {
            link.y = link.y.wrapping_sub(top);
        }
        self.page = Some(next);
        self.paragraph_lines = paragraph_lines as u8;
        Ok(())
    }
    pub fn add_line<S: Sink<Payload = B>>(
        &mut self,
        mut line: Line<B>,
        flow: &Flow,
        sink: &mut S,
    ) -> Result {
        self.running()?;
        self.lines_after = line.lines_after;
        let metrics = sink.line_metrics(&line.payload, self.options);
        self.ensure_page();
        if i32::from(self.next_y) + metrics.height() > i32::from(self.options.height) {
            let (carry, paragraph_lines) = self.lines_to_carry();
            self.break_carrying(carry, paragraph_lines, line.visible_offset, flow, sink)?;
        }
        self.set_visible(line.visible_offset, sink);
        self.extracted = self.extracted.wrapping_add(metrics.words as i32);
        if let Some(page) = self.page.as_mut() {
            let count = self
                .pending_footnotes
                .iter()
                .take_while(|(index, _)| *index <= self.extracted)
                .count();
            let result = self
                .pending_footnotes
                .iter()
                .take(count)
                .try_for_each(|(_, note)| page.add_footnote(note));
            self.pending_footnotes.drain(..count);
            self.latch(result)?;
        }
        let x = metrics.x;
        if let Some(page) = self.page.as_mut() {
            for link in sink.line_links(&mut line.payload) {
                let result = page.add_line_link(link, x, self.next_y, metrics);
                if let Err(error) = result {
                    return self.latch(Err(error));
                }
            }
        }
        self.add_element(Element {
            x,
            y: self.next_y,
            content: Content::Line(line.payload),
        })?;
        self.note_content(
            i32::from(self.next_y),
            i32::from(self.next_y) + metrics.height(),
        );
        self.next_y = (i32::from(self.next_y) + metrics.height()) as i16;
        if self.layout_active {
            self.paragraph_lines = self.paragraph_lines.saturating_add(1);
        }
        if self.recent_count == MAX_CARRIED {
            self.recent.copy_within(1..MAX_CARRIED, 0);
            self.recent_count -= 1;
        }
        self.recent[self.recent_count] = line.visible_offset;
        self.recent_count += 1;
        Ok(())
    }
}
fn c_string(text: &[u8]) -> &[u8] {
    &text[..text.iter().position(|b| *b == 0).unwrap_or(text.len())]
}

#[cfg(test)]
mod tests;

#[cfg(feature = "chapter-test")]
impl<P: Paragraph<B>, B> Assembly<P, B> {
    pub fn test_query(&self, field: u32) -> u32 {
        match field {
            0 => self.page.is_some().into(),
            1 => self.next_y as u32,
            2 => self.table_stacked.into(),
            3 => self.table_bytes,
            4 => self.pending_footnotes.len() as u32,
            5 => 1, // Row scratch is local and cannot survive a synchronous row operation.
            6 => self.cells.len() as u32,
            _ => 0,
        }
    }
    pub fn test_pending_note(&self, index: usize) -> Footnote {
        self.pending_footnotes
            .get(index)
            .map_or_else(Footnote::default, |(_, note)| *note)
    }
    pub fn test_recent(&mut self, index: u32, value: u32) {
        if index < MAX_CARRIED as u32 {
            self.recent[index as usize] = value;
        } else {
            self.recent_count = (value as usize).min(MAX_CARRIED);
        }
    }
    pub fn test_buffer_cell(&mut self) -> Result {
        reserve_one(&mut self.cells)?;
        if let Some(p) = self.paragraph.take() {
            self.cells.push(p);
        }
        Ok(())
    }
}
