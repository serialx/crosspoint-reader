//! Value types at the chapter/assembly boundary; flags have an explicit byte width.
use crate::css;

#[derive(Clone, Copy, Default)]
#[repr(C)]
pub struct Border {
    pub width: u8,
    pub style: u8,
}
impl Border {
    pub fn visible(self) -> bool {
        self.width != 0 && self.style != 0
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
#[repr(C)]
pub struct Block {
    pub alignment: u8,
    pub character_spacing: i8,
    pub margin_top: i16,
    pub margin_bottom: i16,
    pub margin_left: i16,
    pub margin_right: i16,
    pub padding_top: i16,
    pub padding_bottom: i16,
    pub padding_left: i16,
    pub padding_right: i16,
    pub text_indent: i16,
    pub text_indent_defined: u8,
    pub text_align_defined: u8,
    pub rtl: u8,
    pub direction_defined: u8,
    pub font_scale: f32,
    pub font_scale_defined: u8,
    pub font_id: i32,
    pub break_before: u8,
    pub break_after: u8,
    pub keep_with_next: u8,
}
impl Default for Block {
    fn default() -> Self {
        Self {
            alignment: 0,
            character_spacing: 0,
            margin_top: 0,
            margin_bottom: 0,
            margin_left: 0,
            margin_right: 0,
            padding_top: 0,
            padding_bottom: 0,
            padding_left: 0,
            padding_right: 0,
            text_indent: 0,
            text_indent_defined: 0,
            text_align_defined: 0,
            rtl: 0,
            direction_defined: 0,
            font_scale: 1.0,
            font_scale_defined: 0,
            font_id: 0,
            break_before: 0,
            break_after: 0,
            keep_with_next: 0,
        }
    }
}
impl Block {
    pub fn without_top(mut self) -> Self {
        self.margin_top = 0;
        self.padding_top = 0;
        self
    }
    pub fn without_bottom(mut self) -> Self {
        self.margin_bottom = 0;
        self.padding_bottom = 0;
        self
    }
    pub fn bottom_inset(self) -> i16 {
        self.margin_bottom.wrapping_add(self.padding_bottom)
    }
    pub fn add_bottom(mut self, source: Self) -> Self {
        self.margin_bottom = self.margin_bottom.max(source.margin_bottom);
        self.padding_bottom = self.padding_bottom.wrapping_add(source.padding_bottom);
        self
    }
    pub fn combine(self, mut child: Self) -> Self {
        child.margin_left = child.margin_left.wrapping_add(self.margin_left);
        child.margin_right = child.margin_right.wrapping_add(self.margin_right);
        child.padding_left = child.padding_left.wrapping_add(self.padding_left);
        child.padding_right = child.padding_right.wrapping_add(self.padding_right);
        if child.text_indent_defined == 0 && self.text_indent_defined != 0 {
            child.text_indent = self.text_indent;
            child.text_indent_defined = 1;
        }
        if child.text_align_defined == 0 && self.text_align_defined != 0 {
            child.alignment = self.alignment;
            child.text_align_defined = 1;
        }
        if child.direction_defined == 0 && self.direction_defined != 0 {
            child.rtl = self.rtl;
            child.direction_defined = 1;
        }
        if child.font_scale_defined == 0 && self.font_scale_defined != 0 {
            child.font_scale = self.font_scale;
            child.font_scale_defined = 1;
        }
        child
    }
    pub fn from_css(css: &Css, em: f32, alignment: u8, width: u16) -> Self {
        let mut block = Self::default();
        let width = f32::from(width);
        let cap = (em * 2.0) as i16;
        block.margin_top = css.pixels(10, em, width);
        block.margin_bottom = css.pixels(15, em, width);
        block.margin_left = css.pixels(20, em, width).min(cap);
        block.margin_right = css.pixels(25, em, width).min(cap);
        block.padding_top = css.pixels(30, em, width);
        block.padding_bottom = css.pixels(35, em, width);
        block.padding_left = css.pixels(40, em, width).min(cap);
        block.padding_right = css.pixels(45, em, width).min(cap);
        if css.has(4) && (css.unit(5) != 4 || width > 0.0) {
            block.text_indent = css.pixels(5, em, width);
            block.text_indent_defined = 1;
        }
        block.text_align_defined = u8::from(css.has(0));
        block.alignment = if alignment == 4 {
            if css.has(0) {
                css.byte(0)
            } else {
                0
            }
        } else {
            alignment
        };
        block.break_before = u8::from(css.has(21) && css.byte(69) != 0);
        block.break_after = u8::from(css.has(22) && css.byte(70) != 0);
        if css.has(16) {
            block.rtl = u8::from(css.byte(4) == 1);
            block.direction_defined = 1;
        }
        block
    }
}

#[derive(Clone, Copy, Default)]
#[repr(transparent)]
pub struct Css(pub css::Style);
impl Css {
    pub fn has(&self, bit: u32) -> bool {
        self.0.flags() & (1 << bit) != 0
    }
    pub fn byte(&self, offset: usize) -> u8 {
        self.0 .0.get(offset).copied().unwrap_or(0)
    }
    pub fn set(&mut self, offset: usize, value: u8, flag: Option<u32>) {
        if let Some(byte) = self.0 .0.get_mut(offset) {
            *byte = value;
        }
        if let Some(bit) = flag {
            let flags = self.0.flags() | (1 << bit);
            self.0 .0[84..88].copy_from_slice(&flags.to_le_bytes());
        }
    }
    pub fn value(&self, offset: usize) -> f32 {
        let Some(value) = self.0 .0.get(offset..offset + 4) else {
            return 0.0;
        };
        f32::from_le_bytes([value[0], value[1], value[2], value[3]])
    }
    pub fn unit(&self, offset: usize) -> u8 {
        self.byte(offset + 4)
    }
    pub fn pixels(&self, offset: usize, em: f32, width: f32) -> i16 {
        let value = self.value(offset);
        let pixels = match self.unit(offset) {
            1 | 2 => value * em,
            3 => value * 1.33,
            4 => value * width / 100.0,
            _ => value,
        };
        // C++ converts through the target integer width, wrapping the low 16 bits.
        (pixels as i32) as i16
    }
    pub fn border(&self, side: usize) -> Border {
        Border {
            width: self.byte(71 + side * 2),
            style: self.byte(72 + side * 2),
        }
    }
    pub fn drop_lines(&self) -> u8 {
        if self.has(29) && self.byte(82) > 0 {
            return if self.byte(82) >= 2 { self.byte(82) } else { 0 };
        }
        let large = self.has(19) && self.value(60) >= 1.5;
        if !large && !(self.has(28) && self.byte(81) != 0) {
            return 0;
        }
        if large {
            ((self.value(60) + 0.5) as i32).clamp(2, 4) as u8
        } else {
            3
        }
    }
}

#[derive(Clone, Copy)]
#[repr(C)]
pub struct DropCap {
    pub text: [u8; 13],
    pub length: u8,
    pub codepoints: u8,
    pub lines: u8,
    pub bold: u8,
    pub first_pending: u8,
    pub span_depth: i32,
}
impl Default for DropCap {
    fn default() -> Self {
        Self {
            text: [0; 13],
            length: 0,
            codepoints: 0,
            lines: 0,
            bold: 0,
            first_pending: 0,
            span_depth: -1,
        }
    }
}

#[derive(Clone, Copy)]
#[repr(C)]
pub struct Footnote {
    pub number: [u8; 32],
    pub href: [u8; 256],
}
impl Default for Footnote {
    fn default() -> Self {
        Self {
            number: [0; 32],
            href: [0; 256],
        }
    }
}

#[derive(Clone, Copy)]
pub struct InlineSize {
    pub scale: f32,
    pub depth: i32,
    pub valid: bool,
    pub open: bool,
}
impl Default for InlineSize {
    fn default() -> Self {
        Self {
            scale: 1.0,
            depth: -1,
            valid: false,
            open: false,
        }
    }
}

#[derive(Clone, Copy, Default)]
pub struct Entry {
    pub depth: i32,
    pub has_bold: bool,
    pub bold: bool,
    pub has_italic: bool,
    pub italic: bool,
    pub has_decoration: bool,
    pub decoration: u8,
    pub has_direction: bool,
    pub direction: u8,
    pub paragraph_direction: bool,
    pub has_align: bool,
    pub align: u8,
    pub has_sup: bool,
    pub sup: bool,
    pub has_sub: bool,
    pub sub: bool,
    pub has_small: bool,
    pub small: bool,
    pub has_space: bool,
    pub space: bool,
    pub has_scale: bool,
    pub scale_rem: bool,
    pub scale: f32,
}
impl Entry {
    pub fn decoration(&mut self, css: &Css) {
        if css.has(3) {
            self.has_decoration = true;
            self.decoration = css.byte(3);
        }
    }
    pub fn direction(&mut self, css: &Css) {
        if css.has(16) {
            self.has_direction = true;
            self.direction = css.byte(4);
        }
    }
    pub fn presentation(&mut self, css: &Css) {
        if css.has(19) {
            self.has_scale = true;
            self.scale_rem = css.unit(60) == 2;
            self.scale = css.value(60);
        }
        if css.has(30) {
            self.has_space = true;
            self.space = css.byte(83) != 0;
        }
        if css.has(20) {
            self.has_small = true;
            self.small = css.byte(68) != 0;
        }
    }
    pub fn vertical(&mut self, css: &Css) {
        if !css.has(17) {
            return;
        }
        if css.byte(66) == 1 {
            self.has_sup = true;
            self.sup = true;
        } else if css.byte(66) == 2 {
            self.has_sub = true;
            self.sub = true;
        }
    }
    pub fn font(&mut self, css: &Css) {
        if css.has(2) {
            self.has_bold = true;
            self.bold = css.byte(2) == 1;
        }
        if css.has(1) {
            self.has_italic = true;
            self.italic = css.byte(1) == 1;
        }
    }
}
