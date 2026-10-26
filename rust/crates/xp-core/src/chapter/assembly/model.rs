//! Owned page records. Payload destructors return unconsumed blocks to their allocator.
use super::super::style::{Block, Border, DropCap, Footnote};
use super::alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Allocation,
    Stopped,
}
pub type Result<T = ()> = core::result::Result<T, Error>;

pub(super) fn push<T>(items: &mut Vec<T>, item: T) -> Result {
    reserve_one(items)?;
    items.push(item);
    Ok(())
}

pub(super) fn reserve_one<T>(items: &mut Vec<T>) -> Result {
    if items.len() == items.capacity() {
        items
            .try_reserve_exact(items.capacity().max(1))
            .map_err(|_| Error::Allocation)?;
    }
    Ok(())
}

#[repr(C)]
pub struct Link {
    pub href: [u8; 256],
    pub x: i16,
    pub y: i16,
    pub width: i16,
    pub height: i16,
}

#[repr(C, u8)]
pub enum Content<B> {
    Line(B),
    Image(B),
    Rule {
        width: u16,
        thickness: u8,
    },
    DropCap {
        font: i32,
        scale256: u16,
        style: u8,
        text: [u8; 13],
    },
    Box {
        width: u16,
        height: u16,
        sides: [Border; 4],
        shaded: bool,
    },
}
#[repr(C)]
pub struct Element<B> {
    pub x: i16,
    pub y: i16,
    pub content: Content<B>,
}

pub struct Page<B> {
    pub elements: Vec<Element<B>>,
    pub links: Vec<Link>,
    pub footnotes: Vec<Footnote>,
}
impl<B> Default for Page<B> {
    fn default() -> Self {
        Self {
            elements: Vec::new(),
            links: Vec::new(),
            footnotes: Vec::new(),
        }
    }
}
impl<B> Page<B> {
    pub fn add_line_link(
        &mut self,
        link: &LineLink,
        x: i16,
        y: i16,
        metrics: LineMetrics,
    ) -> Result {
        self.append_link(
            &link.href,
            x.wrapping_add(link.x),
            (i32::from(y) + metrics.ruby_shift - i32::from(link.top_lift)) as i16,
            link.width,
            (metrics.base_height + i32::from(link.top_lift)) as i16,
        )
    }
    pub fn add_link(&mut self, link: &Link) -> Result {
        self.append_link(&link.href, link.x, link.y, link.width, link.height)
    }
    #[inline(never)]
    fn append_link(&mut self, href: &[u8; 256], x: i16, y: i16, width: i16, height: i16) -> Result {
        if width <= 0 || height <= 0 || self.links.len() >= 32 || href[0] == 0 || !href.contains(&0)
        {
            return Ok(());
        }
        let mut link = Link {
            href: *href,
            x,
            y,
            width,
            height,
        };
        zero_tail(&mut link.href);
        push(&mut self.links, link)
    }
    #[inline(never)]
    pub fn add_footnote(&mut self, note: &Footnote) -> Result {
        let mut note = *note;
        if self.footnotes.len() == 16 {
            return Ok(());
        }
        note.number[31] = 0;
        note.href[255] = 0;
        zero_tail(&mut note.number);
        zero_tail(&mut note.href);
        push(&mut self.footnotes, note)
    }
}

pub struct Line<B> {
    pub payload: B,
    pub visible_offset: u32,
    pub lines_after: u32,
}
#[repr(C)]
pub struct LineLink {
    pub href: [u8; 256],
    pub x: i16,
    pub width: i16,
    pub top_lift: i16,
}
#[derive(Clone, Copy)]
#[repr(C)]
pub struct LineMetrics {
    pub x: i16,
    pub words: u32,
    pub base_height: i32,
    pub ruby_shift: i32,
}
impl LineMetrics {
    pub fn height(self) -> i32 {
        self.base_height + self.ruby_shift
    }
}

#[derive(Clone, Copy)]
#[repr(C)]
pub struct Options {
    pub font: i32,
    pub compression: f32,
    pub width: u16,
    pub height: u16,
    pub extra_spacing: bool,
    pub alignment: u8,
    pub indent_spaces: u8,
    pub character_spacing: i8,
    pub word_spacing: u8,
    pub hyphenation: bool,
    pub focus: bool,
}
#[derive(Clone, Copy)]
pub struct Layout {
    pub font: i32,
    pub width: u16,
    pub last: bool,
    pub character_spacing: i8,
    pub word_spacing: u8,
    pub max_lines: u32,
}

/// Production paragraph operations; host tests supply a deterministic mock.
pub trait Paragraph<B>: Sized {
    fn create<S: Sink<Payload = B>>(style: Block, options: Options, sink: &mut S) -> Result<Self>;
    fn style(&self) -> Block;
    fn set_style(&mut self, style: Block);
    fn size(&self) -> u32;
    fn dropped(&self) -> bool;
    fn reset_indent(&mut self);
    fn suppress_indent(&mut self);
    fn prepare_font<S: Sink<Payload = B>>(&mut self, sink: &mut S, font: i32, scale: f32) -> i32;
    fn layout<S: Sink<Payload = B>>(
        &mut self,
        sink: &mut S,
        layout: Layout,
        emit: impl FnMut(&mut S, Line<B>) -> Result,
    ) -> Result;
}

/// Calls are synchronous; complete consumes all payloads on success, stop and error.
pub enum ImageProbe<R> {
    Unsupported,
    Failed {
        width: u16,
        height: u16,
    },
    Ready {
        resource: R,
        width: u16,
        height: u16,
    },
}

pub trait Sink {
    type Payload;
    type Image;
    fn paragraph_context(&self) -> Option<super::runtime::ParagraphContext> {
        None
    }
    fn create_line(
        &mut self,
        _line: &crate::para::paragraph_extract::Output,
    ) -> Result<Self::Payload> {
        Err(Error::Allocation)
    }
    #[cfg(feature = "chapter-trace")]
    fn trace(&self, _observation: super::trace::Observation<'_>) {}
    fn reclaim_paragraph_memory(&mut self) {}
    fn pending_anchor(&self) -> &[u8];
    fn defer_anchor(&mut self, text: &[u8]) -> Result;
    fn anchor_starts_chapter(&self) -> bool;
    fn record_anchor(&mut self, page: u16) -> Result;
    fn font_variant(&mut self, font: i32, scale: f32) -> i32;
    fn line_height(&mut self, font: i32, compression: f32) -> i32;
    fn table_line_height(&mut self, font: i32) -> i32 {
        self.line_height(font, 1.0)
    }
    fn ascender(&mut self, font: i32) -> i32;
    fn space_width(&mut self, font: i32) -> i32;
    fn codepoint(&mut self, font: i32, cp: u32, style: u8) -> Option<(i32, i32)>;
    fn line_metrics(&mut self, line: &Self::Payload, options: Options) -> LineMetrics;
    fn row_line_metrics(&mut self, line: &Self::Payload, options: Options) -> LineMetrics {
        self.line_metrics(line, options)
    }
    fn set_line_margin(&mut self, line: &mut Self::Payload, left: i16);
    fn line_links<'a>(&'a mut self, line: &'a mut Self::Payload) -> &'a [LineLink];
    fn complete(&mut self, page: Page<Self::Payload>, position: Position) -> Result;
    fn probe_image(
        &mut self,
        src: &[u8],
        counter: &mut i32,
        popup: &mut bool,
    ) -> Result<ImageProbe<Self::Image>>;
    fn create_image(
        &mut self,
        resource: Self::Image,
        width: i32,
        height: i32,
    ) -> Result<Self::Payload>;
}
#[derive(Clone, Copy, Default)]
#[repr(C)]
pub struct Position {
    pub paragraph: u16,
    pub list: u16,
    pub visible: u32,
    pub next_y: i16,
}

/// Scalar semantic snapshot; binding.rs applies changed feedback after each operation.
#[derive(Default)]
pub struct Flow {
    pub depth: i32,
    pub table_depth: i32,
    pub parent: Block,
    pub block_depth: u32,
    pub marker_only: bool,
    pub continued: bool,
    pub inline_scale: Option<f32>,
    pub drop_cap: DropCap,
    pub page_break: bool,
    pub cell_open: bool,
    pub cell_bottom: i16,
    pub columns: u8,
    pub column_widths: [u16; 4],
    pub table_border: Border,
    pub whitespace: bool,
    pub position: Position,
}

pub(super) fn left(style: Block) -> i16 {
    style.margin_left.wrapping_add(style.padding_left)
}
pub(super) fn right(style: Block) -> i16 {
    style.margin_right.wrapping_add(style.padding_right)
}
pub(super) fn horizontal(style: Block) -> i16 {
    left(style).wrapping_add(right(style))
}
pub(super) fn vertical_merge(parent: Block, mut child: Block) -> Block {
    child.margin_top = child.margin_top.max(parent.margin_top);
    child.margin_bottom = child.margin_bottom.max(parent.margin_bottom);
    child.padding_top = child.padding_top.wrapping_add(parent.padding_top);
    child.padding_bottom = child.padding_bottom.wrapping_add(parent.padding_bottom);
    child.break_before |= parent.break_before;
    if child.direction_defined == 0 && parent.direction_defined != 0 {
        child.rtl = parent.rtl;
        child.direction_defined = 1;
    }
    if child.font_scale_defined == 0 && parent.font_scale_defined != 0 {
        child.font_scale = parent.font_scale;
        child.font_scale_defined = 1;
    }
    child
}

fn zero_tail(bytes: &mut [u8]) {
    if let Some(end) = bytes.iter().position(|b| *b == 0) {
        bytes[end..].fill(0);
    }
}
