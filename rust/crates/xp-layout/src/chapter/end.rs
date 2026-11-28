use super::*;
impl State {
    // Keep the event frame separate from the XML driver on the 8 KiB render stack.
    #[inline(never)]
    pub fn end(&mut self, name: &[u8]) {
        let tag = Tag::parse(name);
        if self.stopped || self.oom {
            return;
        }
        if self.non_visible > 0 {
            self.non_visible -= 1;
        }
        if tag == Tag::RubyText {
            self.collecting_ruby = false;
            if self.in_ruby && self.paragraph_exists() {
                let current = self.call_paragraph_size() as i32;
                let base = current - self.ruby_start;
                // Normalize in place; no second temporary allocation is needed.
                normalize(&mut self.ruby);
                if !self.ruby.is_empty() {
                    let ruby = Span::new(&self.ruby);
                    if base > 0 {
                        self.call_ruby_group(self.ruby_start as u32, base as u32, ruby);
                        self.ruby_start = current;
                    } else if self.ruby_start > 0 {
                        let mut leader = self.ruby_start - 1;
                        while leader >= 0 && self.call_word_style(leader as u32) & 64 != 0 {
                            leader -= 1;
                        }
                        if leader >= 0 {
                            self.call_ruby_append(leader as u32, ruby);
                        }
                    }
                }
            }
            self.ruby.clear();
            if self.paragraph_exists() && !self.paragraph_empty() {
                self.continued = true;
            }
            self.depth -= 1;
            return;
        }
        if tag == Tag::Ruby && self.in_ruby {
            self.in_ruby = false;
            self.ruby_start = -1;
            self.ruby.clear();
            if self.paragraph_exists() && !self.paragraph_empty() {
                self.continued = true;
            }
            self.depth -= 1;
            return;
        }
        let pop = self
            .inline
            .last()
            .is_some_and(|entry| entry.depth == self.depth - 1);
        let change =
            pop || self.bold_until == self.depth - 1 || self.italic_until == self.depth - 1;
        let is_block = tag.block();
        let structural = tag.structural();
        let skipped = self.depth > self.skip;
        if !skipped && self.table_depth > 1 && tag == Tag::Table {
            if self.word_len > 0 {
                self.flush_word();
            }
            self.continued = false;
            self.table_depth -= 1;
            self.depth -= 1;
            self.diagnostic(assembly::Diagnostic::NestedTable, 0, &[]);
            return;
        }
        if self.word_len > 0 {
            let inline = !is_block && !structural && !(tag == Tag::Image) && self.depth != 1;
            let flush = change
                || is_block
                || matches!(tag, Tag::Bold | Tag::Italic | Tag::Underline | Tag::Strike)
                || structural
                || (tag == Tag::Image)
                || self.depth == 1;
            if flush {
                self.flush_word();
                if inline {
                    self.continued = true;
                }
            }
        }
        if is_block
            && tag != Tag::Break
            && !skipped
            && self.paragraph_exists()
            && self.paragraph_empty()
            && self.drop_cap.length > 0
        {
            self.cancel_drop();
            self.flush_word();
        }
        self.depth -= 1;
        if self.drop_cap.span_depth == self.depth {
            self.drop_cap.span_depth = -1;
        }
        if self.inline_size.valid && self.inline_size.depth == self.depth {
            self.inline_size.open = false;
        }
        if self.in_link && self.depth == self.link_depth {
            if self.footnote.number[0] != 0 && self.footnote.href[0] != 0 {
                let index = self.call_extracted()
                    + if self.paragraph_exists() {
                        self.call_paragraph_size() as i32
                    } else {
                        0
                    };
                self.call_footnote(index);
            }
            self.in_link = false;
            self.link_id = 0;
        }
        if self.skip == self.depth {
            self.skip = i32::MAX;
        }
        if !skipped && self.table_depth == 1 && matches!(tag, Tag::Cell | Tag::HeaderCell) {
            self.call_close_cell();
            self.continued = false;
        }
        if !skipped && self.table_depth == 1 && tag == Tag::Row {
            self.call_finish_row();
            self.continued = false;
        }
        if !skipped && self.table_depth == 1 && tag == Tag::Table {
            self.call_finish_row();
            if self.paragraph_exists() && !self.paragraph_empty() {
                self.make_pages(true);
            }
            self.call_reset_paragraph();
            self.table_depth = 0;
            self.inside_cell = false;
            self.call_set_stacked(0);
            self.rows_spanned = 0;
            self.call_reset_table_bytes();
            self.call_clear_cells();
            self.continued = false;
            let style = if self.blocks.is_empty() {
                Block::default()
            } else {
                self.parent().without_bottom()
            };
            self.call_create_paragraph(&style);
            if !self.paragraph_exists() {
                self.diagnostic(assembly::Diagnostic::AfterTableParagraph, 0, &[]);
                self.oom = true;
            }
            self.call_reset_extracted();
        }
        if self.bold_until == self.depth {
            self.bold_until = i32::MAX;
        }
        if self.italic_until == self.depth {
            self.italic_until = i32::MAX;
        }
        if self
            .inline
            .last()
            .is_some_and(|entry| entry.depth == self.depth)
        {
            self.inline.pop();
            self.update_style();
        }
        if is_block && !skipped {
            self.current_css = Css::default();
            self.update_style();
            if tag != Tag::Break && self.blocks.len() > 1 {
                if self.paragraph_exists() {
                    let style = self.paragraph_style().add_bottom(self.parent());
                    self.call_set_paragraph_style(&style);
                }
                let break_after = self.parent().break_after != 0;
                self.blocks.pop();
                self.start_block(self.parent().without_top().without_bottom(), true);
                self.update_style();
                if self.call_box_at_depth(self.depth) != 0 {
                    self.call_close_box();
                }
                if break_after {
                    self.page_break = true;
                }
            }
            if tag == Tag::ListItem {
                self.marker_only = false;
            }
        }
        if matches!(tag, Tag::UnorderedList | Tag::OrderedList)
            && self
                .lists
                .last()
                .is_some_and(|list| list.depth == self.depth)
        {
            self.lists.pop();
        }
        if tag == Tag::Body {
            self.inside_body = false;
        }
        if tag == Tag::Html {
            self.ended = true;
        }
    }
}
fn normalize(bytes: &mut buffer::TextBuffer) {
    let mut first = 0;
    while first < bytes.len() && whitespace(bytes[first]) {
        first += 1;
    }
    let mut end = bytes.len();
    while end > first && whitespace(bytes[end - 1]) {
        end -= 1;
    }
    let mut output = 0;
    let mut space = false;
    for input in first..end {
        let c = bytes[input];
        if whitespace(c) {
            if !space {
                bytes[output] = b' ';
                output += 1;
                space = true;
            }
        } else {
            bytes[output] = c;
            output += 1;
            space = false;
        }
    }
    bytes.truncate(output);
}
