//! A synchronous plane source. The caller freezes content through all replays.
use crate::constants::INVALID;

pub trait Source {
    /// Maximum native rows that fit the caller-owned scratch.
    fn rows(&self) -> u16;
    /// Called only with the display transaction closed. No returned loan is retained.
    fn paint(&mut self, y: u16, rows: u16) -> Result<&[u8], u32>;
}

pub struct Frame<'a> {
    bytes: &'a [u8],
    source: Option<&'a mut dyn Source>,
}
impl<'a> Frame<'a> {
    pub fn buffered(bytes: &'a [u8]) -> Self {
        Self {
            bytes,
            source: None,
        }
    }
    pub fn streamed(source: &'a mut dyn Source) -> Result<Self, u32> {
        if source.rows() == 0 {
            return Err(INVALID);
        }
        Ok(Self {
            bytes: &[],
            source: Some(source),
        })
    }
    pub fn available(&self, size: usize) -> bool {
        self.source.is_some() || self.bytes.len() >= size
    }
    pub fn is_streamed(&self) -> bool {
        self.source.is_some()
    }
    pub fn bytes(&self) -> &[u8] {
        self.bytes
    }
    pub fn source(&mut self) -> Option<&mut (dyn Source + 'a)> {
        self.source.as_deref_mut()
    }
}
