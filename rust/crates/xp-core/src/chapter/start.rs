use super::*;
use crate::xml::Event;

fn fields<'a>(event: &'a Event<'a>) -> impl Iterator<Item = (&'a [u8], &'a [u8])> {
    event
        .fields
        .get(1..)
        .unwrap_or(&[])
        .as_chunks::<2>()
        .0
        .iter()
        .map(|pair| {
            (
                event.data.get(pair[0].range.clone()).unwrap_or(&[]),
                event.data.get(pair[1].range.clone()).unwrap_or(&[]),
            )
        })
}
fn attr<'a>(event: &'a Event<'a>, name: &[u8]) -> &'a [u8] {
    fields(event)
        .find_map(|(key, value)| (key == name).then_some(value))
        .unwrap_or(&[])
}
fn internal_link(href: &[u8]) -> bool {
    !href.is_empty()
        && ![
            b"http://".as_slice(),
            b"https://",
            b"mailto:",
            b"ftp://",
            b"tel:",
            b"javascript:",
        ]
        .iter()
        .any(|p| href.starts_with(p))
}
impl State {
    fn arm_drop(&mut self, name: &[u8], classes: &[u8], id: &[u8]) {
        if self.css.is_null() || self.table_depth > 0 || self.drop_cap.length > 0 {
            return;
        }
        let store = unsafe { &*self.css };
        if !store.has_first_letter() {
            return;
        }
        let count = usize::try_from(self.depth).unwrap_or(usize::MAX);
        let ancestors = self.ancestors.get(..count).unwrap_or(&[]);
        let css = Css(store.resolve(name, classes, id, ancestors, true));
        let lines = css.drop_lines();
        if lines == 0 {
            return;
        }
        #[cfg(feature = "chapter-trace")]
        self.trace(
            4,
            &[u32::from(lines), u32::from(css.byte(2))],
            Span::new(&[]),
            Span::new(&[]),
            core::ptr::null(),
        );
        self.drop_cap.first_pending = 1;
        self.drop_cap.lines = lines;
        self.drop_cap.bold = u8::from(css.has(2) && css.byte(2) == 1);
    }
    fn flush_before_inline(&mut self) {
        if self.word_len > 0 {
            self.flush_word();
            self.continued = true;
        }
    }
    // Keep the event frame separate from the XML driver on the 8 KiB render stack.
    #[inline(never)]
    pub fn start(&mut self, event: &Event<'_>) {
        if self.stopped || self.oom {
            return;
        }
        let name = event
            .fields
            .first()
            .and_then(|f| event.data.get(f.range.clone()))
            .unwrap_or(&[]);
        let tag = Tag::parse(name);
        if name.eq_ignore_ascii_case(b"body") {
            self.inside_body = true;
        }
        if self.inside_body && (self.non_visible > 0 || non_visible(name)) {
            self.non_visible = self.non_visible.wrapping_add(1);
        }
        if self.skip < self.depth {
            self.depth += 1;
            return;
        }
        if tag == Tag::Paragraph {
            self.paragraph_index = self.paragraph_index.wrapping_add(1);
        }
        if tag == Tag::ListItem {
            self.list_index = self.list_index.wrapping_add(1);
        }
        let mut classes = b"".as_slice();
        let mut inline_css = b"".as_slice();
        let mut direction = b"".as_slice();
        let mut id = b"".as_slice();
        let mut hidden = false;
        for (key, value) in fields(event) {
            match key {
                b"class" => classes = value,
                b"style" => inline_css = value,
                b"dir" => direction = value,
                b"hidden" => hidden = true,
                b"id" => {
                    id = value;
                    let toc = self.call_anchor_is_toc(Span::new(id)) != 0;
                    if toc || (tag != Tag::Span && self.call_anchor_count() < 1024) {
                        if self.call_has_anchor() != 0 {
                            self.call_flush_anchor();
                        }
                        self.call_defer_anchor(Span::new(id));
                    }
                }
                _ => {}
            }
        }
        let centered = Block {
            text_align_defined: 1,
            alignment: 2,
            ..Block::default()
        };
        let mut css = Css::default();
        if self.css_enabled {
            let count = usize::try_from(self.depth).unwrap_or(usize::MAX);
            let ancestors = self.ancestors.get(..count).unwrap_or(&[]);
            if !self.css.is_null() {
                css = Css(unsafe { &*self.css }.resolve(name, classes, id, ancestors, false));
            }
            if let Some(ancestor) = self.ancestors.get_mut(count) {
                *ancestor = crate::css::ancestor(name, classes, id);
            }
            if !inline_css.is_empty() {
                css.0.apply(&crate::css::inline(inline_css));
            }
        }
        if css.byte(2) == 2 {
            css.set(2, u8::from(self.bold || self.bold_until < self.depth), None);
        }
        if css.byte(1) == 2 {
            css.set(
                1,
                u8::from(self.italic || self.italic_until < self.depth),
                None,
            );
        }
        if tag == Tag::Pre && !css.has(30) {
            css.set(83, 1, Some(30));
        }
        if hidden {
            css.set(65, 1, Some(15));
        }
        if direction.eq_ignore_ascii_case(b"rtl") {
            css.set(4, 1, Some(16));
        } else if direction.eq_ignore_ascii_case(b"ltr") {
            css.set(4, 0, Some(16));
        }
        if !css.has(16) && self.direction_defined {
            css.set(4, self.direction, Some(16));
        }
        if css.has(15) && css.byte(65) == 1 {
            self.skip = self.depth;
            self.depth += 1;
            return;
        }
        if tag == Tag::Table {
            if self.table_depth > 0 {
                if self.table_depth == 1 && self.inside_cell && self.word_len > 0 {
                    self.flush_word();
                }
                self.continued = false;
                self.table_depth += 1;
                self.depth += 1;
                return;
            }
            if self.word_len > 0 {
                self.flush_word();
            }
            if self.paragraph_exists() && !self.paragraph_empty() {
                self.make_pages(true);
                self.call_reset_paragraph();
            }
            self.call_flush_anchor();
            self.inline_size = InlineSize::default();
            self.drop_cap.first_pending = 0;
            self.push_block_text(&css);
            self.table_depth = 1;
            self.inside_cell = false;
            self.call_set_stacked(0);
            self.call_set_rtl(u8::from(css.has(16) && css.byte(4) == 1));
            self.rows_spanned = 0;
            self.call_reset_table_bytes();
            self.call_clear_cells();
            self.call_reserve_cells();
            self.table_border = style::Border::default();
            self.column_count = 0;
            self.call_measure_table();
            for side in 0..4 {
                let border = css.border(side);
                if !self.table_border.visible() && border.visible() {
                    self.table_border = border;
                }
            }
            let border = attr(event, b"border");
            if !self.table_border.visible() && positive_atoi(border) {
                self.table_border = style::Border { width: 1, style: 1 };
            }
            self.depth += 1;
            return;
        }
        if self.table_depth == 1 && tag == Tag::Row {
            self.call_finish_row();
            if self.paragraph_exists() && !self.paragraph_empty() {
                self.make_pages(true);
            }
            self.call_reset_paragraph();
            self.call_reset_table_bytes();
            self.call_set_stacked(u8::from(self.rows_spanned > 0));
            self.call_set_rtl(u8::from(css.has(16) && css.byte(4) == 1));
            if self.rows_spanned != u16::MAX && self.rows_spanned > 0 {
                self.rows_spanned -= 1;
            }
            self.push_block_text(&css);
            self.depth += 1;
            return;
        }
        if self.table_depth == 1 && matches!(tag, Tag::Cell | Tag::HeaderCell) {
            if self.word_len > 0 {
                self.flush_word();
            }
            self.call_close_cell();
            if self.paragraph_exists() && !self.paragraph_empty() {
                self.make_pages(true);
            }
            self.call_reset_paragraph();
            let cols = table_span(attr(event, b"colspan"));
            let rows = table_span(attr(event, b"rowspan"));
            if cols > 1 || rows > 1 {
                self.call_table_fallback();
            }
            if rows > 1 {
                self.rows_spanned =
                    self.rows_spanned
                        .max(if rows == u16::MAX { rows } else { rows - 1 });
            }
            let mut cell = Block::default();
            self.font_scale(&mut cell, &css, tag);
            let em = self.call_body_ascender() as f32;
            // Keep viewport queries in the C++ source order, including repeated padding queries.
            cell.padding_left = if css.has(11) {
                self.cell_padding(&css, 40, em, true)
            } else {
                4
            };
            cell.padding_right = if css.has(12) {
                self.cell_padding(&css, 45, em, true)
            } else {
                4
            };
            cell.padding_top = self.cell_padding(&css, 30, em, false);
            cell.padding_bottom = self.cell_padding(&css, 35, em, false);
            cell.text_align_defined = 1;
            cell.alignment = if css.has(0) {
                css.byte(0)
            } else if self.align_defined {
                self.align
            } else if css.has(16) && css.byte(4) == 1 {
                3
            } else {
                1
            };
            if css.has(16) {
                cell.direction_defined = 1;
                cell.rtl = u8::from(css.byte(4) == 1);
            }
            self.call_create_paragraph(&cell);
            if !self.paragraph_exists() {
                self.diagnostic(assembly::Diagnostic::TableCell, 0, &[]);
                self.skip = self.depth;
                self.depth += 1;
                return;
            }
            for side in 0..4 {
                let border = css.border(side);
                if !self.table_border.visible() && border.visible() {
                    self.table_border = border;
                }
            }
            self.push_block(cell);
            self.inside_cell = true;
            self.call_reset_extracted();
            self.call_flush_anchor();
            self.push_block_text(&css);
            if tag == Tag::HeaderCell && (!css.has(2) || css.byte(2) == 1) {
                self.bold_until = self.bold_until.min(self.depth);
            }
            self.depth += 1;
            return;
        }
        if self.table_depth >= 1
            && self.inside_cell
            && (tag.block() || tag == Tag::Rule || (tag == Tag::Image))
        {
            if self.word_len > 0 {
                self.flush_word();
            }
            self.call_table_fallback();
        }
        if tag == Tag::Image {
            let mut src = b"".as_slice();
            let mut alt = b"".as_slice();
            for (key, value) in fields(event) {
                if key == b"src" || (src.is_empty() && matches!(key, b"href" | b"xlink:href")) {
                    src = value;
                } else if key == b"alt" {
                    alt = value;
                }
            }
            if let Some(fragment) = src.iter().position(|&b| b == b'#') {
                src = &src[..fragment];
            }
            if self.images == 2 {
                self.skip = self.depth;
                self.depth += 1;
                return;
            }
            #[cfg(feature = "chapter-trace")]
            self.trace(
                5,
                &[
                    css.value(55).to_bits(),
                    u32::from(css.byte(59)),
                    css.value(50).to_bits(),
                    u32::from(css.byte(54)),
                    u32::from(css.has(14)),
                    u32::from(css.has(13)),
                ],
                Span::new(src),
                Span::new(alt),
                core::ptr::null(),
            );
            if !src.is_empty() && self.images != 1 {
                match self.call_image(Span::new(src), css.0 .0.as_ptr()) {
                    1 => {
                        self.depth += 1;
                        return;
                    }
                    2 => return,
                    _ => {}
                }
            }
            if !alt.is_empty() {
                self.start_block(self.parent().combine(centered).without_bottom(), true);
                self.italic_until = self.italic_until.min(self.depth);
                self.depth += 1;
                self.synthetic = true;
                // One callback preserves footnote normalization and soft-flush placement.
                let mut text = buffer::TextBuffer::new();
                if !text.reserve(alt.len().saturating_add(9)) {
                    self.oom = true;
                    return;
                }
                text.append(b"[Image: ");
                text.append(alt);
                text.append(b"]");
                self.characters(&text);
                self.synthetic = false;
                self.skip = self.depth - 1;
                return;
            }
            self.skip = self.depth;
            self.depth += 1;
            return;
        }
        if tag == Tag::Ruby {
            self.flush_before_inline();
            self.in_ruby = true;
            self.ruby_start = if self.paragraph_exists() {
                self.call_paragraph_size() as i32
            } else {
                0
            };
            if self.paragraph_exists() {
                self.call_ruby_capacity();
            }
            self.ruby.clear();
            self.depth += 1;
            return;
        }
        if tag == Tag::RubyText {
            if self.word_len > 0 {
                self.flush_word();
            }
            self.collecting_ruby = true;
            self.depth += 1;
            return;
        }
        if non_visible(name) {
            self.skip = self.depth;
            self.depth += 1;
            return;
        }
        if fields(event).any(|(key, value)| {
            (key == b"role" && value == b"doc-pagebreak")
                || (key == b"epub:type" && value == b"pagebreak")
        }) {
            self.skip = self.depth;
            self.depth += 1;
            return;
        }
        let href = attr(event, b"href");
        if tag == Tag::Link && internal_link(href) {
            if self.table_depth >= 1 && self.inside_cell && self.call_table_stacked() == 0 {
                self.call_table_fallback();
            }
            self.flush_before_inline();
            self.in_link = true;
            self.link_depth = self.depth;
            self.link_id = if self.paragraph_exists() {
                self.call_add_link(Span::new(href))
            } else {
                0
            };
            self.footnote.href[0] = 0;
            if self.link_id != 0 {
                if href.len() >= self.footnote.href.len() {
                    self.oom = true;
                    return;
                }
                self.footnote.href[..href.len()].copy_from_slice(href);
                self.footnote.href[href.len()] = 0;
            }
            self.footnote.number[0] = 0;
            self.footnote_len = 0;
            let mut entry = Entry {
                depth: self.depth,
                has_decoration: true,
                decoration: 1,
                ..Entry::default()
            };
            entry.presentation(&css);
            entry.direction(&css);
            entry.vertical(&css);
            self.push_inline(entry);
            self.update_style();
            self.depth += 1;
            return;
        }
        let em = self.call_body_ascender() as f32;
        let width = self.call_page_width();
        let user = Block::from_css(&css, em, self.alignment, width);
        if tag == Tag::Rule {
            let width = self.call_page_width();
            let mut hr = Block::from_css(&css, em, 1, width);
            if !self.embedded {
                hr.margin_left = 0;
                hr.margin_right = 0;
                hr.margin_top = 0;
                hr.margin_bottom = 0;
                hr.padding_left = 0;
                hr.padding_right = 0;
                hr.padding_top = 0;
                hr.padding_bottom = 0;
                hr.text_indent_defined = 0;
                hr.text_indent = 0;
            }
            self.call_horizontal_rule(&hr);
            self.depth += 1;
            return;
        }
        if tag.heading() != 0 {
            if self.word_len > 0 {
                self.flush_word();
            }
            self.current_css = css;
            let width = self.call_page_width();
            let mut style = Block::from_css(&css, em, 2, width);
            style.text_align_defined = 1;
            if self.embedded && css.has(0) {
                style.alignment = css.byte(0);
            }
            self.font_scale(&mut style, &css, tag);
            style.keep_with_next = 1;
            self.call_open_box(&mut style, css.0 .0.as_ptr());
            let accumulated = self.parent().combine(style);
            self.push_block(accumulated);
            self.start_block(accumulated.without_bottom(), true);
            self.push_block_text(&css);
            self.arm_drop(name, classes, id);
            self.bold_until = self.bold_until.min(self.depth);
            self.update_style();
        } else if tag.block() {
            if tag == Tag::Break {
                if self.word_len > 0 {
                    self.flush_word();
                }
                self.call_line_break(&self.parent().without_top().without_bottom());
            } else {
                if self.word_len > 0 {
                    self.flush_word();
                }
                self.current_css = css;
                let mut style = user;
                self.font_scale(&mut style, &css, tag);
                self.call_open_box(&mut style, css.0 .0.as_ptr());
                let accumulated = self.parent().combine(style);
                self.push_block(accumulated);
                self.start_block(accumulated.without_bottom(), true);
                if !self.paragraph_exists() {
                    return;
                }
                self.push_block_text(&css);
                self.update_style();
                if tag != Tag::ListItem {
                    self.arm_drop(name, classes, id);
                }
                if tag == Tag::ListItem {
                    if self.lists.last().is_some_and(|l| l.none) {
                    } else if self.lists.last().is_some_and(|l| l.ordered) {
                        let Some(list) = self.lists.last_mut() else {
                            return;
                        };
                        list.counter = list.counter.wrapping_add(1);
                        let mut marker = [0u8; 16];
                        let length = number_marker(list.counter, &mut marker);
                        self.call_list_marker(Span::new(&marker[..length]), self.visible_offset);
                        self.marker_only = true;
                    } else {
                        self.call_list_marker(Span::new(b"\xe2\x80\xa2"), self.visible_offset);
                        self.marker_only = true;
                    }
                } else if matches!(tag, Tag::UnorderedList | Tag::OrderedList) {
                    if self.lists.try_reserve(1).is_err() {
                        self.oom = true;
                        return;
                    }
                    self.lists.push(List {
                        ordered: tag == Tag::OrderedList,
                        none: css.has(18) && css.byte(67) == 1,
                        depth: self.depth,
                        counter: 0,
                    });
                }
            }
        } else if matches!(tag, Tag::Underline | Tag::Strike) {
            self.flush_before_inline();
            self.push_decoration(if matches!(tag, Tag::Underline) { 1 } else { 2 }, &css);
        } else if matches!(tag, Tag::Bold | Tag::Italic) {
            self.flush_before_inline();
            let mut entry = Entry {
                depth: self.depth,
                ..Entry::default()
            };
            if matches!(tag, Tag::Bold) {
                self.bold_until = self.bold_until.min(self.depth);
                entry.has_bold = true;
                entry.bold = true;
                if css.has(1) {
                    entry.has_italic = true;
                    entry.italic = css.byte(1) == 1;
                }
            } else {
                self.italic_until = self.italic_until.min(self.depth);
                entry.has_italic = true;
                entry.italic = true;
                if css.has(2) {
                    entry.has_bold = true;
                    entry.bold = css.byte(2) == 1;
                }
            }
            entry.decoration(&css);
            entry.presentation(&css);
            entry.direction(&css);
            self.push_inline(entry);
            self.update_style();
        } else if matches!(tag, Tag::Sup | Tag::Sub) {
            self.flush_before_inline();
            let mut entry = Entry {
                depth: self.depth,
                ..Entry::default()
            };
            if tag == Tag::Sup {
                entry.has_sup = true;
                entry.sup = true;
            } else {
                entry.has_sub = true;
                entry.sub = true;
            }
            entry.presentation(&css);
            self.push_inline(entry);
            self.update_style();
        } else {
            self.inline_element(tag, &css);
        }
        self.depth += 1;
    }
    fn cell_padding(&mut self, css: &Css, offset: usize, em: f32, horizontal: bool) -> i16 {
        let limit = if horizontal {
            self.call_page_width()
        } else {
            self.call_page_height()
        } / 4;
        let width = self.call_page_width();
        css.pixels(offset, em, f32::from(width))
            .clamp(0, limit as i16)
    }
    fn inline_element(&mut self, tag: Tag, css: &Css) {
        if css.has(19)
            && self.table_depth == 0
            && self.word_len == 0
            && self.paragraph_exists()
            && self.paragraph_empty()
            && (!self.inline_size.valid || self.inline_size.open)
        {
            let base = if self.inline_size.valid {
                self.inline_size.scale
            } else {
                self.call_paragraph_scale()
            };
            self.inline_size.scale = (if css.unit(60) == 2 {
                css.value(60)
            } else {
                base * css.value(60)
            })
            .clamp(0.5, 3.0);
            if !self.inline_size.valid {
                self.inline_size.depth = self.depth;
            }
            self.inline_size.valid = true;
            self.inline_size.open = true;
        }
        if self.table_depth == 0
            && self.drop_cap.length == 0
            && self.drop_cap.span_depth < 0
            && self.word_len == 0
            && self.paragraph_exists()
            && self.paragraph_empty()
        {
            let lines = css.drop_lines();
            if lines > 0 {
                self.drop_cap.span_depth = self.depth;
                self.drop_cap.lines = lines;
                self.drop_cap.first_pending = 0;
                self.drop_cap.bold = u8::from(self.bold || (css.has(2) && css.byte(2) == 1));
            }
        }
        let table_align = self.table_depth >= 1 && css.has(0);
        let tag_scale = if tag == Tag::Small {
            0.83
        } else if tag == Tag::Big {
            1.2
        } else {
            0.0
        };
        let sized = css.has(19) || tag_scale > 0.0;
        if sized
            || css.has(2)
            || css.has(1)
            || css.has(3)
            || css.has(20)
            || css.has(30)
            || css.has(16)
            || css.has(17)
            || table_align
        {
            self.flush_before_inline();
            let mut entry = Entry {
                depth: self.depth,
                ..Entry::default()
            };
            entry.font(css);
            entry.decoration(css);
            entry.presentation(css);
            entry.direction(css);
            entry.paragraph_direction = matches!(tag, Tag::Html | Tag::Body);
            if table_align {
                entry.has_align = true;
                entry.align = css.byte(0);
            }
            entry.vertical(css);
            if sized {
                entry.has_scale = true;
                entry.scale_rem = css.has(19) && css.unit(60) == 2;
                entry.scale = if css.has(19) {
                    css.value(60)
                } else {
                    tag_scale
                };
            }
            self.push_inline(entry);
            self.update_style();
        }
    }
}
fn positive_atoi(value: &[u8]) -> bool {
    let mut bytes = value
        .iter()
        .copied()
        .skip_while(|c| c.is_ascii_whitespace())
        .peekable();
    let negative = bytes.peek() == Some(&b'-');
    if matches!(bytes.peek(), Some(b'-' | b'+')) {
        bytes.next();
    }
    let mut n = 0i32;
    for c in bytes {
        if !c.is_ascii_digit() {
            break;
        }
        n = n.wrapping_mul(10).wrapping_add(i32::from(c - b'0'));
    }
    (if negative { n.wrapping_neg() } else { n }) > 0
}
fn number_marker(value: i32, out: &mut [u8; 16]) -> usize {
    let mut reversed = [0u8; 10];
    let mut n = value.unsigned_abs();
    let mut count = 0;
    loop {
        reversed[count] = b'0' + (n % 10) as u8;
        count += 1;
        n /= 10;
        if n == 0 {
            break;
        }
    }
    let mut len = 0;
    if value < 0 {
        out[0] = b'-';
        len = 1;
    }
    for index in (0..count).rev() {
        out[len] = reversed[index];
        len += 1;
    }
    out[len] = b'.';
    len + 1
}
