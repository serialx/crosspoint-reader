use super::{Error, Name};
use xp_ui::keyboard::owner::{
    storage::{Block, Memory},
    Result as KeyboardResult,
};

// The same allocation owns a decoded name or the keyboard's moved edit result.
pub struct KeyboardName<M: Memory> {
    bytes: Block<u8, M>,
    length: u32,
}
impl<M: Memory> Default for KeyboardName<M> {
    fn default() -> Self {
        Self {
            bytes: Block::default(),
            length: 0,
        }
    }
}
impl<M: Memory> KeyboardName<M> {
    pub fn from_result(result: KeyboardResult<M>) -> Option<Self> {
        match result {
            KeyboardResult::Cancelled => None,
            KeyboardResult::Text { bytes, length } => {
                if bytes.as_slice().get(length as usize) != Some(&0) {
                    return None;
                }
                Some(Self { bytes, length })
            }
        }
    }
    pub fn terminated(&self) -> &[u8] {
        if self.length == 0 {
            b"\0"
        } else {
            &self.bytes.as_slice()[..=self.length as usize]
        }
    }
}
impl<M: Memory> AsRef<[u8]> for KeyboardName<M> {
    fn as_ref(&self) -> &[u8] {
        &self.bytes.as_slice()[..self.length as usize]
    }
}
impl<M: Memory> Name for KeyboardName<M> {
    fn read(length: usize, fill: impl FnOnce(&mut [u8])) -> Result<Self, Error> {
        if length == 0 {
            fill(&mut []);
            return Ok(Self::default());
        }
        let required = length.checked_add(1).ok_or(Error::Size)?;
        let required = u32::try_from(required).map_err(|_| Error::Size)?;
        let mut bytes = Block::default();
        if !bytes.grow(required, 0) {
            return Err(Error::Allocation);
        }
        fill(&mut bytes.as_mut_slice()[..length]);
        Ok(Self {
            bytes,
            length: length as u32,
        })
    }
}
