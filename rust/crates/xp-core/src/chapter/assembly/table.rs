use super::*;

struct RowLines<B> {
    lines: [Vec<Option<B>>; MAX_COLUMNS],
    offsets: Vec<u32>,
}

impl<P: Paragraph<B>, B> Assembly<P, B> {
    pub fn clear_cells(&mut self) {
        self.cells.clear();
    }
    pub fn reserve_cells(&mut self) -> Result {
        let result = self
            .cells
            .try_reserve_exact(MAX_COLUMNS)
            .map_err(|_| Error::Allocation);
        self.latch(result)
    }
    pub fn buffered_words(&self) -> u32 {
        self.cells
            .iter()
            .fold(0u32, |total, p| total.wrapping_add(p.size()))
    }
    pub fn fallback_row<S: Sink<Payload = B>>(&mut self, flow: &mut Flow, sink: &mut S) -> Result {
        observe!(sink, "TableFallback", self.table_stacked);
        if self.table_stacked {
            return Ok(());
        }
        let active = self.paragraph.take();
        self.table_stacked = true;
        let result = (|| {
            while !self.cells.is_empty() {
                self.paragraph = Some(self.cells.remove(0));
                self.extracted = 0;
                if self.paragraph.as_ref().is_some_and(|p| p.size() != 0) {
                    self.make_pages(true, true, flow, sink)?;
                }
            }
            Ok(())
        })();
        self.cells.clear();
        self.paragraph = active;
        self.extracted = 0;
        result
    }
    pub fn close_cell<S: Sink<Payload = B>>(&mut self, flow: &mut Flow, sink: &mut S) -> Result {
        observe!(sink, "TableCellEnd", flow.cell_open, self.table_stacked);
        if !flow.cell_open {
            return Ok(());
        }
        flow.cell_open = false;
        let bottom = flow.cell_bottom;
        let Some(paragraph) = self.paragraph.as_ref() else {
            return Ok(());
        };
        self.oom |= paragraph.dropped();
        if !self.table_stacked && self.cells.len() >= MAX_COLUMNS {
            self.fallback_row(flow, sink)?;
        }
        if self.table_stacked {
            self.extracted = 0;
            if self.paragraph.as_ref().is_some_and(|p| p.size() != 0) {
                self.make_pages(true, true, flow, sink)?;
            } else {
                self.next_y = self.next_y.wrapping_add(bottom);
            }
            self.paragraph = None;
            return Ok(());
        }
        let reserve = reserve_one(&mut self.cells);
        self.latch(reserve)?;
        if let Some(paragraph) = self.paragraph.take() {
            self.cells.push(paragraph);
        }
        Ok(())
    }
    pub fn row_separator(&mut self) -> Result {
        if !self.nonempty_page()
            || self.options.width == 0
            || i32::from(self.next_y) + 4 > i32::from(self.options.height)
        {
            return Ok(());
        }
        self.add_element(Element {
            x: 0,
            y: self.next_y.wrapping_add(1),
            content: Content::Rule {
                width: self.options.width,
                thickness: 1,
            },
        })?;
        self.next_y = self.next_y.wrapping_add(4);
        Ok(())
    }
    pub fn finish_row<S: Sink<Payload = B>>(&mut self, flow: &mut Flow, sink: &mut S) -> Result {
        observe!(
            sink,
            "TableRowEnd",
            self.table_rtl,
            self.table_stacked,
            flow.columns
        );
        self.running()?;
        self.close_cell(flow, sink)?;
        if self.cells.is_empty() {
            if self.table_stacked {
                self.row_separator()?;
            }
            self.table_stacked = false;
            return Ok(());
        }
        let mut row = RowLines {
            lines: core::array::from_fn(|_| Vec::new()),
            offsets: Vec::new(),
        };
        let result = self.layout_row(flow, sink, &mut row);
        self.latch(result)
    }
    fn frame_cells(&mut self, top: i32, bottom: i32, widths: &[u16], border: Border) -> Result {
        let height = (bottom + 1).min(i32::from(self.options.height)) - top;
        if height <= 0 {
            return Ok(());
        }
        for column in 0..widths.len() {
            let x = column_x(widths, column, self.table_rtl);
            let width = if x + i32::from(widths[column]) >= i32::from(self.options.width) {
                i32::from(self.options.width) - x
            } else {
                i32::from(widths[column]) + 1
            };
            self.add_element(Element {
                x: x as i16,
                y: top as i16,
                content: Content::Box {
                    width: width as u16,
                    height: height as u16,
                    sides: [border; 4],
                    shaded: false,
                },
            })?;
        }
        Ok(())
    }
    #[inline(never)]
    fn layout_row<S: Sink<Payload = B>>(
        &mut self,
        flow: &mut Flow,
        sink: &mut S,
        row: &mut RowLines<B>,
    ) -> Result {
        let line_height = ((sink.table_line_height(self.options.font) as f32
            * self.options.compression) as i16)
            .max(1);
        let columns = self.cells.len();
        let planned = usize::from(flow.columns) == columns;
        let mut widths = [0u16; MAX_COLUMNS];
        let mut top_padding = [0i16; MAX_COLUMNS];
        let mut max_top = 0i32;
        let mut max_bottom = 0i32;
        let mut fits = columns >= 2;
        for column in 0..columns {
            widths[column] = if planned {
                flow.column_widths[column]
            } else {
                (usize::from(self.options.width) / columns) as u16
            };
            let style = self.cells[column].style();
            top_padding[column] = style.padding_top;
            max_top = max_top.max(i32::from(style.padding_top));
            max_bottom = max_bottom.max(i32::from(style.padding_bottom));
            fits &= i32::from(widths[column])
                > i32::from(style.padding_left) + i32::from(style.padding_right)
                && (planned || i32::from(widths[column]) >= i32::from(line_height) * 3);
        }
        if !fits {
            self.fallback_row(flow, sink)?;
            self.row_separator()?;
            self.table_stacked = false;
            return Ok(());
        }
        let mut max_lines = 0;
        for (column, cell) in self.cells.iter_mut().enumerate() {
            let lines = &mut row.lines[column];
            let estimate = (cell.size() as usize)
                .checked_mul(2)
                .ok_or(Error::Allocation)?;
            lines
                .try_reserve_exact(estimate)
                .map_err(|_| Error::Allocation)?;
            row.offsets
                .try_reserve_exact(estimate.saturating_sub(row.offsets.len()))
                .map_err(|_| Error::Allocation)?;
            let style = cell.style();
            let font = cell.prepare_font(sink, self.options.font, style.font_scale);
            let width = (i32::from(widths[column])
                - i32::from(style.padding_left)
                - i32::from(style.padding_right)) as u16;
            cell.layout(
                sink,
                Layout {
                    font,
                    width,
                    last: true,
                    character_spacing: self.options.character_spacing,
                    word_spacing: self.options.word_spacing,
                    max_lines: u32::MAX,
                },
                |_, line| {
                    let index = lines.len();
                    let offset = line.visible_offset;
                    push(lines, Some(line.payload))?;
                    if row.offsets.len() <= index {
                        push(&mut row.offsets, u32::MAX)?;
                    }
                    row.offsets[index] = row.offsets[index].min(offset);
                    Ok(())
                },
            )?;
            max_lines = max_lines.max(lines.len());
        }
        self.oom |= self.cells.iter().any(|p| p.dropped());
        self.cells.clear();
        let border = Border {
            width: flow.table_border.width.min(3),
            ..flow.table_border
        };
        let bordered = border.visible();
        let gap = if bordered {
            i32::from(border.width) + 2
        } else {
            0
        };
        let mut slice_top = -1;
        for index in 0..max_lines {
            let offset = row
                .offsets
                .get(index)
                .copied()
                .unwrap_or(flow.position.visible);
            let mut row_height = line_height;
            for lines in row.lines.iter().take(columns) {
                if let Some(Some(line)) = lines.get(index) {
                    row_height =
                        row_height.max(sink.row_line_metrics(line, self.options).height() as i16);
                }
            }
            let top_gap = if slice_top < 0 { max_top + gap } else { 0 };
            let bottom_gap = max_bottom + gap;
            let page_full = self.nonempty_page()
                && i32::from(self.next_y) + top_gap + i32::from(row_height) + bottom_gap
                    > i32::from(self.options.height);
            if self.page.is_none() || page_full {
                if page_full {
                    if bordered && slice_top >= 0 {
                        self.frame_cells(
                            slice_top,
                            i32::from(self.next_y) + bottom_gap,
                            &widths[..columns],
                            border,
                        )?;
                    }
                    slice_top = -1;
                    self.set_visible(offset, sink);
                    self.complete_page(flow, sink)?;
                }
                self.ensure_page();
            }
            if slice_top < 0 {
                slice_top = i32::from(self.next_y);
                self.next_y = (i32::from(self.next_y) + gap + max_top) as i16;
            }
            let row_y = self.next_y;
            if let Some(page) = self.page.as_mut() {
                if page.elements.capacity() < page.elements.len() + columns {
                    let fit = ((i32::from(self.options.height) - i32::from(self.next_y))
                        / i32::from(row_height)) as usize;
                    let reserve = (max_lines - index).min(fit.max(1)) * columns + 1;
                    page.elements
                        .try_reserve_exact(reserve)
                        .map_err(|_| Error::Allocation)?;
                }
            }
            for (column, padding) in top_padding.iter().enumerate().take(columns) {
                let Some(line) = row.lines[column].get_mut(index).and_then(Option::take) else {
                    continue;
                };
                let mut payload = line;
                sink.set_line_margin(
                    &mut payload,
                    column_x(&widths[..columns], column, self.table_rtl) as i16,
                );
                let line = Line {
                    payload,
                    visible_offset: offset,
                    lines_after: 0,
                };
                self.next_y = (i32::from(row_y) - max_top + i32::from(*padding)) as i16;
                self.add_line(line, flow, sink)?;
            }
            self.next_y = row_y.wrapping_add(row_height);
        }
        self.next_y =
            (i32::from(self.next_y) + max_bottom).min(i32::from(self.options.height)) as i16;
        self.keep_lines = 0;
        if bordered && slice_top >= 0 && self.page.is_some() {
            self.next_y = (i32::from(self.next_y) + gap).min(i32::from(self.options.height)) as i16;
            self.frame_cells(
                slice_top,
                i32::from(self.next_y),
                &widths[..columns],
                border,
            )?;
        } else {
            self.row_separator()?;
        }
        self.table_stacked = false;
        Ok(())
    }
}
fn column_x(widths: &[u16], column: usize, rtl: bool) -> i32 {
    widths
        .iter()
        .enumerate()
        .filter(|(other, _)| {
            if rtl {
                *other > column
            } else {
                *other < column
            }
        })
        .map(|(_, width)| i32::from(*width))
        .sum()
}
