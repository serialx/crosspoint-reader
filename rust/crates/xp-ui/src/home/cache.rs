use crate::scene::Rect;
use core::ops::Range;
pub const SLOTS: usize = 7;

#[derive(Clone, Default)]
pub struct Slot {
    pub rect: Rect,
    pub range: Range<u32>,
    pub valid: bool,
}
#[derive(Clone)]
pub struct State {
    pub slots: [Slot; SLOTS],
    pub capacity: u32,
    pub used: u32,
    pub orientation: i32,
}
impl Default for State {
    fn default() -> Self {
        Self {
            slots: core::array::from_fn(|_| Slot::default()),
            capacity: 0,
            used: 0,
            orientation: -1,
        }
    }
}
impl State {
    pub fn capacity(region_bytes: u32, width: i32, height: i32) -> u32 {
        u32::try_from(width.max(height))
            .ok()
            .and_then(|edge| edge.checked_mul(SLOTS as u32))
            .and_then(|slack| region_bytes.checked_add(slack))
            .unwrap_or(0)
    }
    pub fn invalidate(&mut self) {
        self.used = 0;
        self.slots = core::array::from_fn(|_| Slot::default());
    }
    pub fn invalidate_slot(&mut self, index: usize) {
        if let Some(slot) = self.slots.get_mut(index) {
            slot.valid = false;
        }
    }
    pub fn prepare(&mut self, orientation: i32) {
        if self.orientation != orientation {
            self.invalidate();
            self.orientation = orientation;
        }
    }
    pub fn lookup(&self, index: usize, rect: Rect) -> Option<Range<u32>> {
        let slot = self.slots.get(index)?;
        (self.capacity != 0 && slot.valid && slot.rect == rect).then(|| slot.range.clone())
    }
    pub fn reserve(&mut self, index: usize, rect: Rect, needed: u32) -> Option<Range<u32>> {
        let slot = self.slots.get_mut(index)?;
        if self.capacity == 0 {
            return None;
        }
        if needed > slot.range.end - slot.range.start
            && needed <= self.capacity.saturating_sub(self.used)
        {
            slot.range = self.used..self.used + needed;
            self.used += needed;
        }
        if needed == 0 || needed > slot.range.end - slot.range.start {
            return None;
        }
        slot.rect = rect;
        Some(slot.range.clone())
    }
    pub fn copied(&mut self, index: usize, success: bool) {
        if let Some(s) = self.slots.get_mut(index) {
            s.valid = success;
        }
    }
}
