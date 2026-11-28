//! Shared toolbar event values and input thresholds.
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Event {
    None = 0,
    Dismiss = 1,
    Tool = 2,
    PrevChapter = 3,
    NextChapter = 4,
    Scrub = 5,
    Row = 6,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Swipe {
    None = 0,
    Left = 1,
    Right = 2,
    Up = 3,
    Down = 4,
}

pub const TEXT_ROWS: i32 = 5;
pub const HOLD_MS: u32 = 1500;
pub const HOLD_ROWS: i32 = 10;
