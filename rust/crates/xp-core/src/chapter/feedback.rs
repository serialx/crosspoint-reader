use super::*;

#[derive(Clone, Copy)]
#[repr(u32)]
pub enum Query {
    Stopped,
    Oom,
    Ended,
    Depth,
    TableDepth,
    VisibleOffset,
    ParagraphIndex,
    ListIndex,
    Alignment,
    PreserveWhitespace,
    MarkerOnly,
    HasWord,
    BlockDepth,
    InlineValid,
    InlineScale,
    PageBreak,
    CellOpen,
    ColumnCount,
    Column0,
    Column1,
    Column2,
    Column3,
    TableBorder,
}
#[derive(Clone, Copy)]
#[repr(u32)]
pub enum Notify {
    Stop,
    FailLayout,
    ConsumeMarker,
    ClearContinuation,
    ResetInlineSize,
    FlushWord,
    ClearDropCap,
    DisarmDropCap,
    ConsumePageBreak,
    CloseCell,
    BeginBlocks,
    BeginLists,
}
impl State {
    pub fn query(&self, query: Query) -> u32 {
        match query {
            Query::Stopped => u32::from(self.stopped),
            Query::Oom => u32::from(self.oom),
            Query::Ended => u32::from(self.ended),
            Query::Depth => self.depth as u32,
            Query::TableDepth => self.table_depth as u32,
            Query::VisibleOffset => self.visible_offset,
            Query::ParagraphIndex => u32::from(self.paragraph_index),
            Query::ListIndex => u32::from(self.list_index),
            Query::Alignment => u32::from(self.alignment),
            Query::PreserveWhitespace => u32::from(self.space),
            Query::MarkerOnly => u32::from(self.marker_only),
            Query::HasWord => u32::from(self.word_len > 0),
            Query::BlockDepth => self.blocks.len() as u32,
            Query::InlineValid => u32::from(self.inline_size.valid),
            Query::InlineScale => self.inline_size.scale.to_bits(),
            Query::PageBreak => u32::from(self.page_break),
            Query::CellOpen => u32::from(self.inside_cell),
            Query::ColumnCount => u32::from(self.column_count),
            Query::Column0 => u32::from(self.column_widths[0]),
            Query::Column1 => u32::from(self.column_widths[1]),
            Query::Column2 => u32::from(self.column_widths[2]),
            Query::Column3 => u32::from(self.column_widths[3]),
            Query::TableBorder => {
                u32::from(self.table_border.width) | (u32::from(self.table_border.style) << 8)
            }
        }
    }
    pub fn notify(&mut self, notification: Notify) -> u32 {
        match notification {
            Notify::Stop => self.stopped = true,
            Notify::FailLayout => self.oom = true,
            Notify::ConsumeMarker => self.marker_only = false,
            Notify::ClearContinuation => self.continued = false,
            Notify::ResetInlineSize => self.inline_size = InlineSize::default(),
            Notify::FlushWord => self.flush_word(),
            Notify::ClearDropCap => self.drop_cap = DropCap::default(),
            Notify::DisarmDropCap => self.drop_cap.first_pending = 0,
            Notify::ConsumePageBreak => {
                let value = self.page_break;
                self.page_break = false;
                return u32::from(value);
            }
            Notify::CloseCell => {
                self.inside_cell = false;
                let padding = self.parent().padding_bottom;
                self.blocks.pop();
                return padding as u32;
            }
            Notify::BeginBlocks => {
                self.stopped = false;
                self.ended = false;
                let root = Block {
                    alignment: if self.alignment == 4 {
                        0
                    } else {
                        self.alignment
                    },
                    ..Block::default()
                };
                self.blocks.clear();
                if self.blocks.try_reserve(8).is_err() {
                    self.oom = true;
                    return 0;
                }
                self.blocks.push(root);
            }
            Notify::BeginLists => {
                self.lists.clear();
                if self.lists.try_reserve(4).is_err() {
                    self.oom = true;
                    return 0;
                }
                self.table_depth = 0;
                self.inside_cell = false;
                self.rows_spanned = 0;
            }
        }
        0
    }
    pub fn parent_block(&self) -> Block {
        self.parent()
    }
    pub fn drop_cap(&self) -> DropCap {
        self.drop_cap
    }
}
