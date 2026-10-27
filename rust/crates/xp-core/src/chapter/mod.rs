//! EPUB semantic state and owned assembly; external sink calls are synchronous.
extern crate alloc;
use crate::css::{Ancestor, Store};
use alloc::vec::Vec;
use core::ffi::c_void;
#[macro_use]
pub mod assembly;
mod binding;
mod buffer;
mod end;
pub mod feedback;
pub mod services;
mod start;
pub mod style;
pub mod table;
mod tag;
mod text;
use assembly::runtime::{Callbacks, OwnedParagraph, PageSink, Payload};
use services::Span;
use style::{Block, Css, DropCap, Entry, Footnote, InlineSize};
use tag::Tag;

#[derive(Clone, Copy, Default)]
struct List {
    ordered: bool,
    none: bool,
    counter: i32,
    depth: i32,
}

#[derive(Clone, Copy, Default)]
#[repr(C)]
pub struct Measure {
    pub min: [u16; 4],
    pub preferred: [u16; 4],
    pub columns: u8,
}

pub struct State {
    paragraph: *mut crate::para::paragraph::Paragraph,
    callbacks: *const Callbacks,
    pub assembly: assembly::Assembly<OwnedParagraph, Payload>,
    page_storage: core::cell::UnsafeCell<assembly::runtime::Storage>,
    ctx: *mut c_void,
    css: *const Store,
    alignment: u8,
    embedded: bool,
    images: u8,
    css_enabled: bool,
    pub stopped: bool,
    pub oom: bool,
    pub ended: bool,
    pub byte: u64,
    depth: i32,
    skip: i32,
    bold_until: i32,
    italic_until: i32,
    continued: bool,
    in_ruby: bool,
    ruby_start: i32,
    collecting_ruby: bool,
    ruby: buffer::TextBuffer,
    marker_only: bool,
    inside_cell: bool,
    rows_spanned: u16,
    table_border: style::Border,
    column_count: u8,
    column_widths: [u16; 4],
    paragraph_index: u16,
    list_index: u16,
    visible_offset: u32,
    word_offset: u32,
    inside_body: bool,
    synthetic: bool,
    non_visible: u16,
    in_link: bool,
    link_depth: i32,
    link_id: u8,
    footnote: Footnote,
    footnote_len: usize,
    page_break: bool,
    inline_size: InlineSize,
    lists: Vec<List>,
    word: [u8; 201],
    word_len: usize,
    inline: Vec<Entry>,
    blocks: Vec<Block>,
    current_css: Css,
    ancestors: [Ancestor; 16],
    bold: bool,
    italic: bool,
    decoration: u8,
    direction_defined: bool,
    direction: u8,
    align_defined: bool,
    align: u8,
    sup: bool,
    sub: bool,
    small: bool,
    space: bool,
    table_depth: i32,
    drop_cap: DropCap,
}

impl State {
    /// # Safety
    /// paragraph is null or a live disjoint owner. Assembly rebinds on every
    /// ownership transition, using the current semantic callback reborrow.
    pub unsafe fn set_paragraph(&mut self, paragraph: *mut crate::para::paragraph::Paragraph) {
        self.paragraph = paragraph;
    }

    /// # Safety
    /// out is writable, aligned uninitialized storage for State. Callbacks/context
    /// and the optional CSS store outlive this state. Calls are serialized.
    /// Callbacks may reborrow the supplied State pointer only synchronously; they must
    /// never use an older pointer while a more recent callback owns the reborrow.
    #[allow(clippy::too_many_arguments)] // Construction mirrors the fixed C options and callbacks.
    pub unsafe fn initialize(
        out: *mut Self,
        callbacks: *const Callbacks,
        ctx: *mut c_void,
        css: *const Store,
        alignment: u8,
        embedded: bool,
        images: u8,
        css_enabled: bool,
        assembly_options: assembly::Options,
    ) {
        core::ptr::addr_of_mut!((*out).paragraph).write(core::ptr::null_mut());
        core::ptr::addr_of_mut!((*out).callbacks).write(callbacks);
        core::ptr::addr_of_mut!((*out).assembly).write(assembly::Assembly::new(assembly_options));
        core::ptr::addr_of_mut!((*out).page_storage).write(core::cell::UnsafeCell::new(
            assembly::runtime::Storage::default(),
        ));
        core::ptr::addr_of_mut!((*out).ctx).write(ctx);
        core::ptr::addr_of_mut!((*out).css).write(css);
        core::ptr::addr_of_mut!((*out).alignment).write(alignment);
        core::ptr::addr_of_mut!((*out).embedded).write(embedded);
        core::ptr::addr_of_mut!((*out).images).write(images);
        core::ptr::addr_of_mut!((*out).css_enabled).write(css_enabled);
        core::ptr::addr_of_mut!((*out).stopped).write(false);
        core::ptr::addr_of_mut!((*out).oom).write(false);
        core::ptr::addr_of_mut!((*out).ended).write(false);
        core::ptr::addr_of_mut!((*out).byte).write(0);
        core::ptr::addr_of_mut!((*out).depth).write(0);
        core::ptr::addr_of_mut!((*out).skip).write(i32::MAX);
        core::ptr::addr_of_mut!((*out).bold_until).write(i32::MAX);
        core::ptr::addr_of_mut!((*out).italic_until).write(i32::MAX);
        core::ptr::addr_of_mut!((*out).continued).write(false);
        core::ptr::addr_of_mut!((*out).in_ruby).write(false);
        core::ptr::addr_of_mut!((*out).ruby_start).write(-1);
        core::ptr::addr_of_mut!((*out).collecting_ruby).write(false);
        core::ptr::addr_of_mut!((*out).ruby).write(buffer::TextBuffer::new());
        core::ptr::addr_of_mut!((*out).marker_only).write(false);
        core::ptr::addr_of_mut!((*out).inside_cell).write(false);
        core::ptr::addr_of_mut!((*out).rows_spanned).write(0);
        core::ptr::addr_of_mut!((*out).table_border).write(style::Border::default());
        core::ptr::addr_of_mut!((*out).column_count).write(0);
        core::ptr::addr_of_mut!((*out).column_widths).write([0; 4]);
        core::ptr::addr_of_mut!((*out).paragraph_index).write(0);
        core::ptr::addr_of_mut!((*out).list_index).write(0);
        core::ptr::addr_of_mut!((*out).visible_offset).write(0);
        core::ptr::addr_of_mut!((*out).word_offset).write(0);
        core::ptr::addr_of_mut!((*out).inside_body).write(false);
        core::ptr::addr_of_mut!((*out).synthetic).write(false);
        core::ptr::addr_of_mut!((*out).non_visible).write(0);
        core::ptr::addr_of_mut!((*out).in_link).write(false);
        core::ptr::addr_of_mut!((*out).link_depth).write(-1);
        core::ptr::addr_of_mut!((*out).link_id).write(0);
        core::ptr::addr_of_mut!((*out).footnote).write(Footnote::default());
        core::ptr::addr_of_mut!((*out).footnote_len).write(0);
        core::ptr::addr_of_mut!((*out).page_break).write(false);
        core::ptr::addr_of_mut!((*out).inline_size).write(InlineSize::default());
        core::ptr::addr_of_mut!((*out).lists).write(Vec::new());
        core::ptr::addr_of_mut!((*out).word).write([0; 201]);
        core::ptr::addr_of_mut!((*out).word_len).write(0);
        core::ptr::addr_of_mut!((*out).inline).write(Vec::new());
        core::ptr::addr_of_mut!((*out).blocks).write(Vec::new());
        core::ptr::addr_of_mut!((*out).current_css).write(Css::default());
        core::ptr::addr_of_mut!((*out).ancestors).write([crate::css::ancestor(b"", b"", b""); 16]);
        core::ptr::addr_of_mut!((*out).bold).write(false);
        core::ptr::addr_of_mut!((*out).italic).write(false);
        core::ptr::addr_of_mut!((*out).decoration).write(0);
        core::ptr::addr_of_mut!((*out).direction_defined).write(false);
        core::ptr::addr_of_mut!((*out).direction).write(0);
        core::ptr::addr_of_mut!((*out).align_defined).write(false);
        core::ptr::addr_of_mut!((*out).align).write(1);
        core::ptr::addr_of_mut!((*out).sup).write(false);
        core::ptr::addr_of_mut!((*out).sub).write(false);
        core::ptr::addr_of_mut!((*out).small).write(false);
        core::ptr::addr_of_mut!((*out).space).write(false);
        core::ptr::addr_of_mut!((*out).table_depth).write(0);
        core::ptr::addr_of_mut!((*out).drop_cap).write(DropCap::default());
    }
    fn parent(&self) -> Block {
        self.blocks.last().copied().unwrap_or_default()
    }
    fn push_block(&mut self, mut block: Block) {
        block.break_before = 0;
        if self.blocks.try_reserve(1).is_err() {
            self.oom = true;
            return;
        }
        self.blocks.push(block);
    }
    fn push_inline(&mut self, entry: Entry) {
        if self.inline.try_reserve(1).is_err() {
            self.oom = true;
            return;
        }
        self.inline.push(entry);
    }
    fn paragraph_exists(&mut self) -> bool {
        self.call_paragraph_exists() != 0
    }
    fn paragraph_empty(&mut self) -> bool {
        self.call_paragraph_empty() != 0
    }
    fn paragraph_style(&mut self) -> Block {
        let mut out = Block::default();
        self.call_paragraph_style(&mut out);
        out
    }
    fn start_block(&mut self, style: Block, end: bool) {
        self.call_start_block(&style, u8::from(end));
    }
    fn make_pages(&mut self, last: bool) {
        self.call_make_pages(u8::from(last), 1);
    }
    fn font_scale(&self, block: &mut Block, css: &Css, tag: Tag) {
        let mut parent = self.parent().font_scale;
        for entry in &self.inline {
            if entry.has_scale {
                parent = if entry.scale_rem {
                    entry.scale
                } else {
                    parent * entry.scale
                };
            }
        }
        let mut scale = parent;
        if css.has(19) {
            scale = if css.unit(60) == 2 {
                css.value(60)
            } else {
                parent * css.value(60)
            };
        } else if tag.heading() != 0 {
            let level = tag.heading() + b'0';
            let scales = [2.0, 1.5, 1.17, 1.0, 0.83, 0.67];
            scale = parent * scales[usize::from(level - b'1')];
        }
        block.font_scale = scale.clamp(0.5, 3.0);
        block.font_scale_defined = 1;
    }
    fn update_style(&mut self) {
        let css = &self.current_css;
        self.bold = css.has(2) && css.byte(2) == 1;
        self.italic = css.has(1) && css.byte(1) == 1;
        self.decoration = if css.has(3) { css.byte(3) } else { 0 };
        self.small = css.has(20) && css.byte(68) != 0;
        self.space = css.has(30) && css.byte(83) != 0;
        let parent = self.parent();
        let mut defined = parent.direction_defined != 0;
        let mut rtl = parent.rtl != 0;
        self.direction_defined = defined;
        self.direction = u8::from(rtl);
        self.align_defined = css.has(0);
        self.align = css.byte(0);
        self.sup = false;
        self.sub = false;
        for entry in &self.inline {
            if entry.has_bold {
                self.bold = entry.bold;
            }
            if entry.has_italic {
                self.italic = entry.italic;
            }
            if entry.has_decoration {
                self.decoration |= entry.decoration;
            }
            if entry.has_space {
                self.space = entry.space;
            }
            if entry.has_small {
                self.small = entry.small;
            }
            if entry.has_direction {
                self.direction_defined = true;
                self.direction = entry.direction;
                if entry.paragraph_direction {
                    defined = true;
                    rtl = entry.direction == 1;
                }
            }
            if entry.has_align {
                self.align_defined = true;
                self.align = entry.align;
            }
            if entry.has_sup {
                self.sup = entry.sup;
                if entry.sup {
                    self.sub = false;
                }
            }
            if entry.has_sub {
                self.sub = entry.sub;
                if entry.sub {
                    self.sup = false;
                }
            }
        }
        self.call_direction(u8::from(defined), u8::from(rtl));
    }
    fn push_block_text(&mut self, css: &Css) {
        let sized = css.has(19) || self.inline.iter().any(|entry| entry.has_scale);
        if !css.has(2)
            && !css.has(1)
            && !css.has(3)
            && !css.has(20)
            && !css.has(30)
            && !css.has(16)
            && !css.has(0)
            && !sized
        {
            return;
        }
        let mut entry = Entry {
            depth: self.depth,
            ..Entry::default()
        };
        entry.font(css);
        entry.decoration(css);
        entry.presentation(css);
        if sized {
            entry.has_scale = true;
            entry.scale_rem = true;
            entry.scale = self.parent().font_scale;
        }
        entry.direction(css);
        entry.paragraph_direction = true;
        if css.has(0) {
            entry.has_align = true;
            entry.align = css.byte(0);
        }
        self.push_inline(entry);
        self.update_style();
    }
    fn push_decoration(&mut self, decoration: u8, css: &Css) {
        let mut entry = Entry {
            depth: self.depth,
            has_decoration: true,
            decoration: if css.has(3) { css.byte(3) } else { decoration },
            ..Entry::default()
        };
        entry.font(css);
        entry.presentation(css);
        entry.direction(css);
        self.push_inline(entry);
        self.update_style();
    }
}

fn whitespace(c: u8) -> bool {
    matches!(c, b' ' | b'\r' | b'\n' | b'\t')
}
fn non_visible(name: &[u8]) -> bool {
    [b"head".as_slice(), b"style", b"script", b"title", b"rp"]
        .iter()
        .any(|tag| name.eq_ignore_ascii_case(tag))
}
fn table_span(value: &[u8]) -> u16 {
    if value.is_empty() {
        return 1;
    }
    let mut span = 0u32;
    for c in value {
        if !c.is_ascii_digit() {
            return 1;
        }
        let digit = u32::from(c - b'0');
        if span > (u32::from(u16::MAX) - digit) / 10 {
            return u16::MAX;
        }
        span = span * 10 + digit;
    }
    if span == 0 {
        u16::MAX
    } else {
        span as u16
    }
}

#[cfg(feature = "chapter-test")]
mod test_access;
