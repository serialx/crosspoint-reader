//! Host-only operation observations; formatting matches ChapterTrace.h.
use super::super::style::{Block, Border};
use core::fmt;

#[derive(Clone, Copy)]
pub enum Field<'a> {
    Integer(i64),
    Float(f32),
    Bytes(&'a [u8]),
    Block(&'a Block),
    Border(Border),
}
macro_rules! integer {
    ($($ty:ty),*) => { $(impl From<$ty> for Field<'_> {
        fn from(value: $ty) -> Self { Self::Integer(value as i64) }
    })* };
}
integer!(u8, i8, u16, i16, u32, i32, usize, bool);
impl From<f32> for Field<'_> {
    fn from(value: f32) -> Self {
        Self::Float(value)
    }
}
impl<'a> From<&'a [u8]> for Field<'a> {
    fn from(value: &'a [u8]) -> Self {
        Self::Bytes(value)
    }
}
impl<'a> From<&'a Block> for Field<'a> {
    fn from(value: &'a Block) -> Self {
        Self::Block(value)
    }
}
impl From<Border> for Field<'_> {
    fn from(value: Border) -> Self {
        Self::Border(value)
    }
}

pub struct Observation<'a> {
    pub name: &'static str,
    pub fields: &'a [Field<'a>],
}
impl fmt::Display for Observation<'_> {
    fn fmt(&self, out: &mut fmt::Formatter<'_>) -> fmt::Result {
        out.write_str(self.name)?;
        for field in self.fields {
            write!(out, "\t{field}")?;
        }
        writeln!(out)
    }
}
impl fmt::Display for Field<'_> {
    fn fmt(&self, out: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Integer(value) => write!(out, "{value}"),
            Self::Float(value) => write!(out, "{}", value.to_bits()),
            Self::Bytes(bytes) => {
                for byte in *bytes {
                    write!(out, "{byte:02x}")?;
                }
                Ok(())
            }
            Self::Border(border) => write!(out, "{},{}", border.width, border.style),
            Self::Block(s) => write!(
                out,
                "{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                s.alignment,
                s.character_spacing,
                s.margin_top,
                s.margin_bottom,
                s.margin_left,
                s.margin_right,
                s.padding_top,
                s.padding_bottom,
                s.padding_left,
                s.padding_right,
                s.text_indent,
                s.text_indent_defined,
                s.text_align_defined,
                s.rtl,
                s.direction_defined,
                s.font_scale.to_bits(),
                s.font_scale_defined,
                s.font_id,
                s.break_before,
                s.break_after,
                s.keep_with_next
            ),
        }
    }
}
