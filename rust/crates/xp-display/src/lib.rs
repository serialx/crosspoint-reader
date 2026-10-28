#![no_std]

pub mod constants;

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
    fn reset(&mut self, settle_ms: u16);
    fn reset_level(&mut self, level: u8);
    fn busy_level(&mut self) -> u8;
    fn busy_wait(&mut self, polarity: u32, tag: u32);
    fn refresh_wait(&mut self, tag: u32);
    fn delay_ms(&mut self, duration: u32);
    fn delay_us(&mut self, duration: u32);
    fn clock_ms(&mut self) -> u32;
    fn probe(&mut self, kind: u32, arg: u32) -> u32;
}

pub trait Panel {
    fn begin(&mut self, bus: &mut impl Bus);
    fn capabilities(&self, mode: u32) -> u32;
}

// ABI milestone stub; UC8253 behavior is added after consumers can branch.
pub struct Uc8253 {
    pub width: u16,
    pub height: u16,
}
impl Panel for Uc8253 {
    fn begin(&mut self, bus: &mut impl Bus) {
        bus.reset(50);
    }
    fn capabilities(&self, _mode: u32) -> u32 {
        0
    }
}
