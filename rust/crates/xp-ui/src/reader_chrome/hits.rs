use crate::scene::{Error, HitSink, Interaction, Rect};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Control {
    pub rect: Rect,
    pub value: i32,
    pub action: u8,
    pub reserved: [u8; 3],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub first_row: Rect,
    pub first_index: i32,
    pub gap: i16,
    pub row_count: u8,
    pub row_action: u8,
    pub heights: [i16; 24],
    pub controls: [Control; 7],
    pub generation: u32,
    pub control_count: u8,
    pub uniform: u8,
    pub reserved: [u8; 2],
}
impl Layout {
    pub fn new(row_action: u8, generation: u32) -> Self {
        Self {
            row_action,
            generation,
            uniform: 1,
            ..Self::default()
        }
    }
    pub fn valid(&self) -> bool {
        self.row_count <= 24
            && self.control_count <= 7
            && self.uniform <= 1
            && self.gap >= 0
            && matches!(self.row_action, 1 | 6)
            && (0..=65535).contains(&self.first_index)
            && self.first_index + i32::from(self.row_count) <= 65536
            && (self.row_count == 0 || self.first_row.h == self.heights[0])
            && (self.uniform == 0
                || self.heights[..usize::from(self.row_count)]
                    .iter()
                    .all(|h| *h == self.first_row.h))
            && self.heights[..usize::from(self.row_count)]
                .iter()
                .all(|h| *h > 0)
            && self.controls[..usize::from(self.control_count)]
                .iter()
                .all(|c| (1..=6).contains(&c.action))
    }
    fn row(&self, index: usize) -> Option<Interaction> {
        if index >= usize::from(self.row_count) {
            return None;
        }
        let offset = if self.uniform != 0 {
            (i32::from(self.first_row.h) + i32::from(self.gap)) * index as i32
        } else {
            self.heights[..index]
                .iter()
                .map(|h| i32::from(*h) + i32::from(self.gap))
                .sum()
        };
        Some(Interaction {
            rect: Rect {
                y: i16::try_from(i32::from(self.first_row.y) + offset).ok()?,
                h: self.heights[index],
                ..self.first_row
            },
            action: u32::from(self.row_action),
            value: self.first_index + index as i32,
            generation: self.generation,
            inputs: 1,
            flags: 0,
        })
    }
    pub fn hit(&self, x: i16, y: i16) -> Option<Interaction> {
        if !self.valid() {
            return None;
        }
        for i in (usize::from(self.row_count)..self.len()).rev() {
            let hit = self.get(i)?;
            if hit.rect.contains(x, y) {
                return Some(hit);
            }
        }
        if self.uniform != 0 && self.row_count > 0 {
            let stride = i32::from(self.first_row.h) + i32::from(self.gap);
            if stride <= 0 {
                return None;
            }
            let index = (i32::from(y) - i32::from(self.first_row.y)) / stride;
            let hit = self.row(usize::try_from(index).ok()?)?;
            return hit.rect.contains(x, y).then_some(hit);
        }
        (0..usize::from(self.row_count))
            .rev()
            .filter_map(|i| self.row(i))
            .find(|h| h.rect.contains(x, y))
    }
}
pub trait Hits {
    fn len(&self) -> usize;
    fn is_empty(&self) -> bool {
        self.len() == 0
    }
    fn get(&self, index: usize) -> Option<Interaction>;
}
impl Hits for Layout {
    fn len(&self) -> usize {
        usize::from(self.row_count) + usize::from(self.control_count)
    }
    fn get(&self, index: usize) -> Option<Interaction> {
        if index < usize::from(self.row_count) {
            return self.row(index);
        }
        let index = index - usize::from(self.row_count);
        if index >= usize::from(self.control_count) {
            return None;
        }
        let c = self.controls.get(index)?;
        Some(Interaction {
            rect: c.rect,
            value: c.value,
            action: u32::from(c.action),
            generation: self.generation,
            inputs: 1,
            flags: 0,
        })
    }
}
impl Hits for [Interaction] {
    fn len(&self) -> usize {
        <[Interaction]>::len(self)
    }
    fn get(&self, index: usize) -> Option<Interaction> {
        <[Interaction]>::get(self, index).copied()
    }
}
impl HitSink for Layout {
    fn push(&mut self, h: Interaction) -> Result<(), Error> {
        if h.generation != self.generation
            || h.inputs != 1
            || h.flags != 0
            || h.action == 0
            || h.action > 6
        {
            return Err(Error::Invalid);
        }
        if h.action != u32::from(self.row_action) {
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
        let index = usize::from(self.row_count);
        if index >= 24 {
            return Err(Error::Capacity);
        }
        if h.rect.h <= 0 || !(0..=65535).contains(&h.value) {
            return Err(Error::Invalid);
        }
        if index == 0 {
            self.first_row = h.rect;
            self.first_index = h.value;
        } else {
            if h.rect.x != self.first_row.x
                || h.rect.w != self.first_row.w
                || h.value != self.first_index + index as i32
            {
                return Err(Error::Invalid);
            }
            if index == 1 {
                self.gap = i16::try_from(
                    i32::from(h.rect.y) - i32::from(self.first_row.y) - i32::from(self.first_row.h),
                )
                .map_err(|_| Error::Invalid)?;
                if self.gap < 0 {
                    return Err(Error::Invalid);
                }
            } else {
                let previous = self.row(index - 1).ok_or(Error::Invalid)?.rect;
                if i32::from(h.rect.y)
                    != i32::from(previous.y) + i32::from(previous.h) + i32::from(self.gap)
                {
                    return Err(Error::Invalid);
                }
            }
            if h.rect.h != self.first_row.h {
                self.uniform = 0;
            }
        }
        self.heights[index] = h.rect.h;
        self.row_count += 1;
        Ok(())
    }
}
