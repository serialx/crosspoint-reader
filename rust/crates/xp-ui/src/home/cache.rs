use crate::scene::Rect;
pub const SLOTS: usize = 7;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Slot {
    pub rect: Rect,
    pub offset: u32,
    pub bytes: u32,
    pub valid: u8,
    pub reserved: [u8; 3],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct State {
    pub slots: [Slot; SLOTS],
    pub capacity: u32,
    pub used: u32,
    pub orientation: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Span {
    pub offset: u32,
    pub bytes: u32,
}
impl Default for State {
    fn default() -> Self {
        Self {
            slots: [Slot::default(); SLOTS],
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
        self.slots = [Slot::default(); SLOTS];
    }
    pub fn invalidate_slot(&mut self, index: usize) {
        if let Some(slot) = self.slots.get_mut(index) {
            slot.valid = 0;
        }
    }
    pub fn prepare(&mut self, orientation: i32) {
        if self.orientation != orientation {
            self.invalidate();
            self.orientation = orientation;
        }
    }
    pub fn lookup(&self, index: usize, rect: Rect) -> Span {
        let Some(s) = self.slots.get(index) else {
            return Span::default();
        };
        if self.capacity != 0 && s.valid != 0 && s.rect == rect {
            Span {
                offset: s.offset,
                bytes: s.bytes,
            }
        } else {
            Span::default()
        }
    }
    pub fn reserve(&mut self, index: usize, rect: Rect, needed: u32) -> Span {
        let Some(s) = self.slots.get_mut(index) else {
            return Span::default();
        };
        if self.capacity == 0 {
            return Span::default();
        }
        if needed > s.bytes && needed <= self.capacity.saturating_sub(self.used) {
            s.offset = self.used;
            s.bytes = needed;
            self.used += needed;
        }
        if needed > 0 && needed <= s.bytes {
            s.rect = rect;
            Span {
                offset: s.offset,
                bytes: s.bytes,
            }
        } else {
            Span::default()
        }
    }
    pub fn copied(&mut self, index: usize, success: bool) {
        if let Some(s) = self.slots.get_mut(index) {
            s.valid = u8::from(success);
        }
    }
}
const _: () = {
    assert!(core::mem::size_of::<Slot>() == 20);
    assert!(core::mem::size_of::<State>() == 152);
    assert!(core::mem::size_of::<Span>() == 8);
};
