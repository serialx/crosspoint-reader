//! Published list geometry: uniform rows are arithmetic, section rows store only vertical offsets.
use crate::reader_chrome::hits::Control;
use crate::scene::{Error, HitSink, Interaction, Rect};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub first: Rect,
    pub first_index: i32,
    pub heights: [i16; 24],
    pub offsets: [i16; 24],
    pub controls: [Control; 7],
    pub stride: i16,
    pub count: u8,
    pub control_count: u8,
    pub uniform: u8,
    pub reserved: [u8; 3],
}
impl Layout {
    pub fn valid(&self) -> bool {
        self.count <= 24
            && self.control_count <= 7
            && self.uniform <= 1
            && (0..=65535).contains(&self.first_index)
    }
    pub fn header(&mut self, controls: &[Control]) -> Result<(), Error> {
        if !self.valid()
            || controls.len() > 3
            || usize::from(self.control_count) + controls.len() > 7
            || controls
                .iter()
                .any(|c| !(4..=6).contains(&c.action) || c.rect.w < 0 || c.rect.h < 0)
        {
            return Err(Error::Invalid);
        }
        for c in controls {
            self.push(Interaction {
                rect: c.rect,
                action: u32::from(c.action),
                value: c.value,
                generation: 1,
                inputs: 1,
                flags: 0,
            })?;
        }
        Ok(())
    }
    pub fn row(&self, index: usize) -> Option<Interaction> {
        if !self.valid() || index >= usize::from(self.count) {
            return None;
        }
        let offset = if self.uniform != 0 {
            i32::from(self.stride) * index as i32
        } else {
            i32::from(self.offsets[index])
        };
        Some(Interaction {
            rect: Rect {
                y: i16::try_from(i32::from(self.first.y) + offset).ok()?,
                h: self.heights[index],
                ..self.first
            },
            action: 1,
            value: self.first_index + index as i32,
            generation: 1,
            inputs: 1,
            flags: 0,
        })
    }
    pub fn hit(&self, x: i16, y: i16) -> Option<Interaction> {
        if !self.valid() {
            return None;
        }
        for c in self.controls[..usize::from(self.control_count)]
            .iter()
            .rev()
        {
            if c.rect.contains(x, y) {
                return Some(Interaction {
                    rect: c.rect,
                    action: u32::from(c.action),
                    value: c.value,
                    generation: 1,
                    inputs: 1,
                    flags: 0,
                });
            }
        }
        if self.uniform != 0 && self.count > 0 && self.stride > 0 {
            let index = (i32::from(y) - i32::from(self.first.y)) / i32::from(self.stride);
            let hit = self.row(usize::try_from(index).ok()?)?;
            return hit.rect.contains(x, y).then_some(hit);
        }
        (0..usize::from(self.count))
            .rev()
            .filter_map(|i| self.row(i))
            .find(|r| r.rect.contains(x, y))
    }
}
impl HitSink for Layout {
    fn push(&mut self, h: Interaction) -> Result<(), Error> {
        if h.generation != 1 || h.inputs != 1 || h.flags != 0 {
            return Err(Error::Invalid);
        }
        if h.action != 1 {
            if !(2..=6).contains(&h.action) {
                return Err(Error::Invalid);
            }
            let c = self
                .controls
                .get_mut(usize::from(self.control_count))
                .ok_or(Error::Capacity)?;
            *c = Control {
                rect: h.rect,
                value: h.value,
                action: h.action as u8,
                reserved: [0; 3],
            };
            self.control_count += 1;
            return Ok(());
        }
        let index = usize::from(self.count);
        if index >= 24 || h.rect.h <= 0 || !(0..=65535).contains(&h.value) {
            return Err(Error::Capacity);
        }
        if index == 0 {
            self.first = h.rect;
            self.first_index = h.value;
            self.uniform = 1;
            self.stride = h.rect.h;
        } else if h.value != self.first_index + index as i32
            || h.rect.x != self.first.x
            || h.rect.w != self.first.w
        {
            return Err(Error::Invalid);
        }
        let offset = i16::try_from(i32::from(h.rect.y) - i32::from(self.first.y))
            .map_err(|_| Error::Invalid)?;
        if offset < 0 {
            return Err(Error::Invalid);
        }
        if index == 1 {
            self.stride = offset;
        }
        if index > 0
            && (i32::from(offset) != i32::from(self.stride) * index as i32
                || h.rect.h != self.first.h)
        {
            self.uniform = 0;
        }
        self.heights[index] = h.rect.h;
        self.offsets[index] = offset;
        self.count += 1;
        Ok(())
    }
}
