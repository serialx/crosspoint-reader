#![no_std]

pub mod constants;
pub mod frame;
#[cfg(feature = "ssd1677")]
pub mod ssd1677;
#[cfg(feature = "ssd1677")]
mod ssd1677_luts;
#[cfg(any(
    feature = "uc8179",
    feature = "uc8279-x3",
    feature = "uc8279-x4",
    feature = "uc8279c-a4"
))]
pub mod ultrachip;

#[derive(Clone, Copy)]
#[repr(u8)]
pub enum RowOrder {
    Forward = 0,
    Reverse = 1,
}

/// Physical I/O only; controller commands and state remain in the panel.
pub trait Bus {
    fn cmd(&mut self, command: u8);
    fn data8(&mut self, value: u8);
    fn data_span(&mut self, data: &[u8]);
    fn cmd_data(&mut self, command: u8, data: &[u8]);
    fn txn_begin(&mut self);
    fn txn_cmd(&mut self, command: u8);
    fn txn_data(&mut self, value: u8);
    fn txn_write(&mut self, data: &[u8]);
    fn txn_end(&mut self);
    /// One command and one transaction, retaining one write per row.
    fn plane(&mut self, command: u8, data: &[u8], stride: u16, order: RowOrder) -> Result<(), u32>;
    fn reset(&mut self, settle_ms: u16);
    fn reset_level(&mut self, level: u8);
    fn busy_level(&mut self) -> u8;
    fn busy_wait(&mut self, polarity: u32, tag: u32);
    fn refresh_wait(&mut self, tag: u32);
    fn delay_ms(&mut self, duration: u32);
    fn delay_us(&mut self, duration: u32);
    fn clock_ms(&mut self) -> u32;
    fn probe(&mut self, kind: u32, arg: u32) -> u32;
    fn diagnostic(&mut self, _event: u32, _a: u32, _b: u32, _c: u32, _d: u32) {}
}

pub trait Panel {
    fn begin(&mut self, bus: &mut impl Bus);
    fn capabilities(&self, mode: u32) -> u32;
}

#[cfg(any(feature = "uc8253", feature = "uc8179", feature = "uc8279-x4"))]
mod luts;
#[cfg(feature = "uc8253")]
mod uc8253;
#[cfg(feature = "uc8253")]
pub use uc8253::{default_config, direct_luts, Uc8253, X3Config};

#[derive(Default)]
pub struct Request<'a> {
    pub operation: u32,
    pub mode: u32,
    pub flags: u32,
    pub arg: u32,
    pub x: u16,
    pub y: u16,
    pub width: u16,
    pub height: u16,
    pub data: Option<&'a [u8]>,
}

#[cfg(all(test, feature = "uc8253"))]
mod tests;

#[cfg(feature = "paper-mono")]
pub mod papermono;
#[cfg(any(feature = "paper-mono", feature = "ssd2677"))]
pub mod retained;
#[cfg(feature = "ssd2677")]
pub mod ssd2677;
#[cfg(feature = "ssd2677")]
mod ssd2677_luts;
