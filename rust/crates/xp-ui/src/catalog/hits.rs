use crate::scene::{Error, HitSink, Interaction, Rect};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Hits {
    pub row_area: Rect,
    pub first_index: i32,
    pub offsets: [i16; 32],
    pub heights: [i16; 32],
    pub controls: [Interaction; 6],
    pub rows: u8,
    pub control_count: u8,
    pub reserved: u16,
}
impl Hits {
    pub fn hit(&self, x: i16, y: i16) -> Option<Interaction> {
        if self.control_count > 6 || self.rows > 32 {
            return None;
        }
        for h in self
            .controls
            .get(..usize::from(self.control_count))?
            .iter()
            .rev()
        {
            if h.rect.contains(x, y) {
                return Some(*h);
            }
        }
        for i in 0..usize::from(self.rows) {
            let r = Rect {
                y: self.row_area.y.checked_add(self.offsets[i])?,
                h: self.heights[i],
                ..self.row_area
            };
            if r.contains(x, y) {
                return Some(Interaction {
                    rect: r,
                    action: 1,
                    value: self.first_index.checked_add(i as i32)?,
                    generation: 1,
                    inputs: 1,
                    flags: 0,
                });
            }
        }
        None
    }
}
impl HitSink for Hits {
    fn push(&mut self, h: Interaction) -> Result<(), Error> {
        if h.action != 1 {
            let p = self
                .controls
                .get_mut(usize::from(self.control_count))
                .ok_or(Error::Capacity)?;
            *p = h;
            self.control_count += 1;
            return Ok(());
        }
        let index = usize::from(self.rows);
        if index >= 32 {
            return Err(Error::Capacity);
        }
        if index == 0 {
            self.row_area = h.rect;
            self.first_index = h.value;
        }
        if h.value != self.first_index + index as i32
            || h.rect.x != self.row_area.x
            || h.rect.w != self.row_area.w
        {
            return Err(Error::Invalid);
        }
        self.offsets[index] = h
            .rect
            .y
            .checked_sub(self.row_area.y)
            .ok_or(Error::Invalid)?;
        self.heights[index] = h.rect.h;
        self.rows += 1;
        Ok(())
    }
}
