use super::*;
use model::{left, right};

impl<P: Paragraph<B>, B> Assembly<P, B> {
    pub fn box_at_depth(&self, depth: i32) -> bool {
        self.boxes.last().is_some_and(|scope| scope.depth == depth)
    }
    /// The semantic caller flushes its pending word before entering this operation.
    pub fn open_box<S: Sink<Payload = B>>(
        &mut self,
        own: &mut Block,
        css: &Css,
        flow: &mut Flow,
        sink: &mut S,
    ) -> Result {
        let shaded = css.has(27) && css.byte(80) != 0;
        if flow.table_depth > 0
            || self.boxes.len() >= MAX_BOXES
            || (!(0..4).any(|i| css.border(i).visible()) && !shaded)
        {
            return Ok(());
        }
        if self.paragraph.as_ref().is_some_and(|p| p.size() != 0) && !flow.marker_only {
            self.start_block(flow.parent.without_top().without_bottom(), true, flow, sink)?;
        }
        let sides = core::array::from_fn(|i| {
            let side = css.border(i);
            if side.visible() {
                side
            } else {
                Border::default()
            }
        });
        let mut scope = BoxScope {
            depth: flow.depth,
            left: left(flow.parent).wrapping_add(own.margin_left),
            right: (i32::from(self.options.width)
                - i32::from(right(flow.parent))
                - i32::from(own.margin_right)) as i16,
            pad_top: 0,
            pad_bottom: 0,
            sides,
            shaded,
            top: -1,
            bottom: -1,
            continued: false,
        };
        if i32::from(scope.right) - i32::from(scope.left) < 8 {
            return Ok(());
        }
        for (padding, side) in [
            &mut own.padding_top,
            &mut own.padding_right,
            &mut own.padding_bottom,
            &mut own.padding_left,
        ]
        .into_iter()
        .zip(sides)
        {
            if side.width != 0 || shaded {
                *padding = (*padding).max(4).wrapping_add(i16::from(side.width));
            }
        }
        scope.pad_top = own.padding_top;
        scope.pad_bottom = own.padding_bottom;
        observe!(
            sink,
            "BoxOpen",
            scope.depth,
            scope.left,
            scope.right,
            scope.pad_top,
            scope.pad_bottom,
            scope.sides[0],
            scope.sides[1],
            scope.sides[2],
            scope.sides[3],
            scope.shaded,
            &*own
        );
        let result = push(&mut self.boxes, scope);
        self.latch(result)
    }
    pub fn close_box<S: Sink<Payload = B>>(&mut self, flow: &Flow, sink: &mut S) -> Result {
        observe!(sink, "BoxClose", flow.depth);
        let Some(mut scope) = self.boxes.pop() else {
            return Ok(());
        };
        if scope.top < 0 && !scope.continued {
            let height = i32::from(scope.pad_top) + i32::from(scope.pad_bottom);
            if height <= 0 {
                return Ok(());
            }
            let mut above = 0;
            let mut below = 0;
            if let Some(p) = self.paragraph.as_mut().filter(|p| p.size() == 0) {
                let mut style = p.style();
                above = i32::from(style.margin_top);
                below = i32::from(style.margin_bottom);
                style.margin_top = 0;
                style.padding_top = 0;
                style.margin_bottom = 0;
                style.padding_bottom = 0;
                p.set_style(style);
            }
            self.ensure_page();
            if self.nonempty_page()
                && i32::from(self.next_y) + above + height > i32::from(self.options.height)
            {
                self.complete_page(flow, sink)?;
                above = 0;
            }
            let top = i32::from(self.next_y) + above;
            scope.top = (top + i32::from(scope.pad_top)) as i16;
            scope.bottom = scope.top;
            self.next_y = (top + height + below) as i16;
            self.note_content(top, top + height);
        }
        self.emit_box(scope, true)
    }
    pub(super) fn note_content(&mut self, top: i32, bottom: i32) {
        for scope in &mut self.boxes {
            if scope.top < 0 {
                scope.top = top as i16;
            }
            scope.bottom = i32::from(scope.bottom).max(bottom) as i16;
        }
    }
    pub(super) fn emit_box(&mut self, scope: BoxScope, closing: bool) -> Result {
        if scope.top < 0 || self.page.is_none() {
            return Ok(());
        }
        let top = 0.max(i32::from(scope.top) - i32::from(scope.pad_top));
        let bottom = i32::from(self.options.height).min(
            i32::from(scope.bottom)
                + if closing {
                    i32::from(scope.pad_bottom)
                } else {
                    0
                },
        );
        if bottom <= top {
            return Ok(());
        }
        let sides = [
            if scope.continued {
                Border::default()
            } else {
                scope.sides[0]
            },
            scope.sides[1],
            if closing {
                scope.sides[2]
            } else {
                Border::default()
            },
            scope.sides[3],
        ];
        self.add_element(Element {
            x: scope.left,
            y: top as i16,
            content: Content::Box {
                width: scope.right.wrapping_sub(scope.left) as u16,
                height: (bottom - top) as u16,
                sides,
                shaded: scope.shaded,
            },
        })
    }
    /// The semantic caller flushes its pending word before entering this operation.
    pub fn horizontal_rule<S: Sink<Payload = B>>(
        &mut self,
        style: Block,
        flow: &mut Flow,
        sink: &mut S,
    ) -> Result {
        observe!(sink, "HorizontalRule", &style, flow.position.visible);
        self.running()?;
        if let Some(paragraph) = &self.paragraph {
            self.start_block(paragraph.style(), true, flow, sink)?;
        }
        self.ensure_page();
        let line_height = sink.line_height(self.options.font, self.options.compression) as i16;
        let default_gap = line_height / 2;
        let top_gap = (if style.margin_top > 0 {
            style.margin_top
        } else {
            default_gap
        })
        .wrapping_add(style.padding_top.max(0));
        let bottom_gap = (if style.margin_bottom > 0 {
            style.margin_bottom
        } else {
            default_gap
        })
        .wrapping_add(style.padding_bottom.max(0));
        let available =
            ((i32::from(self.options.width) - i32::from(horizontal(style))) as i16).max(1);
        let width = (available / 4).max(1);
        let x = left(style).wrapping_add((available - width) / 2);
        let total = top_gap.wrapping_add(2).wrapping_add(bottom_gap);
        if self.nonempty_page()
            && i32::from(self.next_y) + i32::from(total) > i32::from(self.options.height)
        {
            self.set_visible(flow.position.visible, sink);
            self.complete_page(flow, sink)?;
        }
        self.next_y = self.next_y.wrapping_add(top_gap);
        self.add_element(Element {
            x,
            y: self.next_y,
            content: Content::Rule {
                width: width as u16,
                thickness: 2,
            },
        })?;
        self.set_visible(flow.position.visible, sink);
        self.note_content(i32::from(self.next_y), i32::from(self.next_y) + 2);
        self.keep_lines = 0;
        self.next_y = self.next_y.wrapping_add(2).wrapping_add(bottom_gap);
        self.record_anchor(sink)
    }
    pub(super) fn layout_drop_cap<S: Sink<Payload = B>>(
        &mut self,
        paragraph: &mut P,
        layout: Layout,
        flow: &mut Flow,
        sink: &mut S,
    ) -> Result {
        let cap = core::mem::take(&mut flow.drop_cap);
        let style = if cap.bold != 0 { 1 } else { 0 };
        let variant = sink.font_variant(self.options.font, 3.0);
        let font = if variant != 0 {
            variant
        } else {
            self.options.font
        };
        let line_height = sink.line_height(layout.font, self.options.compression);
        let ascender = sink.ascender(layout.font);
        let body_top = sink
            .codepoint(layout.font, u32::from(b'H'), 0)
            .map_or(ascender * 7 / 10, |(_, top)| top);
        let source_top = sink
            .codepoint(font, u32::from(b'H'), style)
            .map(|(_, top)| top)
            .filter(|top| *top > 0)
            .unwrap_or(body_top.max(1));
        let height = (i32::from(cap.lines) - 1) * line_height + body_top;
        let scale = (height * 256 / source_top).clamp(256, 4096);
        let mut width = sink.space_width(layout.font);
        let text = c_string(&cap.text);
        let mut cursor = 0;
        while cursor < text.len() {
            let Some(cp) = crate::utf8::next(text, &mut cursor) else {
                break;
            };
            if let Some((advance, _)) = sink.codepoint(font, cp, style) {
                width += (advance * scale / 256 + 8) >> 4;
            }
        }
        width = width.min(i32::from(layout.width / 2));
        self.ensure_page();
        if self.nonempty_page()
            && i32::from(self.next_y) + i32::from(cap.lines) * line_height
                > i32::from(self.options.height)
        {
            self.break_carrying(self.heading_carry(), 0, flow.position.visible, flow, sink)?;
        }
        let top = i32::from(self.next_y);
        let mut block = paragraph.style();
        self.add_element(Element {
            x: left(block),
            y: (top + (i32::from(cap.lines) - 1) * line_height + ascender) as i16,
            content: Content::DropCap {
                font,
                scale256: scale as u16,
                style,
                text: cap.text,
            },
        })?;
        self.note_content(top, top + i32::from(cap.lines) * line_height);
        paragraph.suppress_indent();
        block.text_indent = 0;
        block.text_indent_defined = 1;
        block.margin_left = block.margin_left.wrapping_add(width as i16);
        paragraph.set_style(block);
        let result = paragraph.layout(
            sink,
            Layout {
                width: (i32::from(layout.width) - width) as u16,
                max_lines: u32::from(cap.lines),
                ..layout
            },
            |sink, line| self.add_line(line, flow, sink),
        );
        block.margin_left = block.margin_left.wrapping_sub(width as i16);
        paragraph.set_style(block);
        self.next_y = i32::from(self.next_y).max(top + i32::from(cap.lines) * line_height) as i16;
        result
    }
}
