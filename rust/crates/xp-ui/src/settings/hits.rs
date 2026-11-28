use crate::scene::{Error, HitSink, Interaction, Rect};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub first_row: Rect,
    pub first_index: i32,
    pub disabled: u32,
    pub heights: [i16; 32],
    pub gap: i16,
    pub count: u8,
    pub uniform: u8,
}
impl Layout {
    pub fn hit(&self, x: i16, y: i16) -> Option<Interaction> {
        if self.count == 0 || self.count > 32 || self.gap < 0 || self.first_row.h <= 0 {
            return None;
        }
        let emit = |i: usize, top: i32, h: i16| {
            let rect = Rect {
                y: i16::try_from(top).ok()?,
                h,
                ..self.first_row
            };
            if self.disabled & (1 << i) != 0 || !rect.contains(x, y) {
                return None;
            }
            Some(Interaction {
                rect,
                action: 1,
                value: self.first_index.checked_add(i as i32)?,
                generation: 1,
                inputs: 1,
                flags: 0,
            })
        };
        let first = i32::from(self.first_row.y);
        if self.uniform == 1 {
            let stride = i32::from(self.first_row.h) + i32::from(self.gap);
            let offset = i32::from(y) - first;
            if offset < 0 {
                return None;
            }
            let i = usize::try_from(offset / stride).ok()?;
            if i >= usize::from(self.count) {
                return None;
            }
            return emit(i, first + i as i32 * stride, self.first_row.h);
        }
        let mut top = first;
        for i in 0..usize::from(self.count) {
            let h = self.heights[i];
            if h <= 0 {
                return None;
            }
            if let Some(hit) = emit(i, top, h) {
                return Some(hit);
            }
            top += i32::from(h) + i32::from(self.gap);
        }
        None
    }
}
impl HitSink for Layout {
    fn push(&mut self, hit: Interaction) -> Result<(), Error> {
        let i = usize::from(self.count);
        if i >= 32 {
            return Err(Error::Capacity);
        }
        if hit.action != 1 || hit.rect.h <= 0 {
            return Err(Error::Invalid);
        }
        if i == 0 {
            self.first_row = hit.rect;
            self.first_index = hit.value;
            self.uniform = 1;
        } else {
            if hit.rect.x != self.first_row.x
                || hit.rect.w != self.first_row.w
                || Some(hit.value) != self.first_index.checked_add(i as i32)
            {
                return Err(Error::Invalid);
            }
            if i == 1 {
                self.gap = i16::try_from(
                    i32::from(hit.rect.y)
                        - i32::from(self.first_row.y)
                        - i32::from(self.first_row.h),
                )
                .map_err(|_| Error::Invalid)?;
                if self.gap < 0 {
                    return Err(Error::Invalid);
                }
            }
            let expected = i32::from(self.first_row.y)
                + self.heights[..i]
                    .iter()
                    .map(|&h| i32::from(h) + i32::from(self.gap))
                    .sum::<i32>();
            if expected != i32::from(hit.rect.y) {
                return Err(Error::Invalid);
            }
            if hit.rect.h != self.first_row.h {
                self.uniform = 0;
            }
        }
        self.heights[i] = hit.rect.h;
        self.count += 1;
        Ok(())
    }
}
