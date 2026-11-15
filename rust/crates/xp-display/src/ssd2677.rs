//! SSD2677 Picco/Sticky command policy, ported from SDK 08effe64.
use crate::{
    retained::{Plane, Psram},
    ssd2677_luts as lut, Bus,
};

pub const TAG_RESET: u32 = 90;
pub const TAG_INIT: u32 = 91;
pub const TAG_LATCH: u32 = 92;
pub const TAG_ON: u32 = 93;
pub const TAG_OFF: u32 = 94;
pub const TAG_REFRESH: u32 = 95;

// Picco build 2912 DROM 0x3c8246b4; Sticky v1.2.0 DROM 0x3c292494.
const PICCO_INIT: &[(u8, &[u8])] = &[
    (0x00, &[0x27, 0x09]),
    (0x83, &[0, 0, 3, 0x1f, 0, 0xc8, 2, 0xa8, 1]),
    (0x01, &[7, 0xf0, 0x22, 0x20, 0x78, 0x22]),
    (0x06, &[0x0f, 0x8b, 0x93, 0xc1]),
    (0xe7, &[0xc1]),
    (0x50, &[0x77]),
    (0x62, &[0x98, 0x98, 0x98, 0x75, 0xca, 0xb2, 0x98, 0x7e]),
    (0xe0, &[0x10]),
    (0x65, &[0, 0, 0, 0]),
    (0x30, &[4]),
    (0x82, &[0x28]),
    (0xe9, &[1]),
];
const STICKY_INIT: &[(u8, &[u8])] = &[
    (0, &[0x2f, 0x0e]),
    (6, &[0x0f, 0x8b, 0x93, 0xc1]),
    (0xe7, &[0xc1]),
    (0x30, &[8]),
    (0x50, &[0x77]),
    (0x62, &[0x76, 0x76, 0x76, 0x5a, 0x9d, 0x8a, 0x76, 0x62]),
    (0x61, &[3, 0x20, 2, 0xa8]),
    (0xe0, &[0x10]),
    (0x65, &[0, 0, 0, 0]),
    (0xe9, &[1]),
];

pub struct Ssd2677 {
    sticky: bool,
    width_bytes: usize,
    height: usize,
    needs_power_on: bool,
    needs_initial_full: bool,
    latched_waveform: u8,
    absolute_gray: bool,
    old: Plane,
    lsb: Plane,
}
impl Ssd2677 {
    pub fn new(sticky: bool, width: u16, height: u16) -> Self {
        let size = usize::from(width / 8) * usize::from(height);
        Self {
            sticky,
            width_bytes: usize::from(width / 8),
            height: usize::from(height),
            needs_power_on: true,
            needs_initial_full: true,
            latched_waveform: 0,
            absolute_gray: false,
            old: Plane::new(size),
            lsb: Plane::new(size),
        }
    }
    /// # Safety
    /// Use the same allocator that supplied every retained plane.
    pub unsafe fn release(&mut self, memory: &mut impl Psram) {
        self.old.release(memory);
        self.lsb.release(memory);
    }
    pub fn capabilities(&self, mode: u32) -> u32 {
        use crate::constants::*;
        match mode {
            0 => CAP_OVERLAY,
            1 if self.sticky => CAP_ABSOLUTE | CAP_COMBINED,
            _ => 0,
        }
    }
    fn init(&mut self, bus: &mut impl Bus) {
        if self.sticky {
            bus.reset_level(0);
            bus.delay_ms(20);
            bus.reset_level(1);
            bus.delay_ms(10);
        } else {
            bus.reset(0);
        }
        bus.busy_wait(1, TAG_RESET);
        for &(command, data) in if self.sticky { STICKY_INIT } else { PICCO_INIT } {
            bus.cmd(command);
            bus.data_span(data);
            if command == 0 && !self.sticky {
                bus.delay_ms(10);
            }
            if self.sticky && (command == 0 || command == 0xe9) {
                bus.busy_wait(1, TAG_INIT);
            }
        }
        bus.busy_wait(1, TAG_INIT);
        self.needs_power_on = true;
        self.latched_waveform = 0;
    }
    fn latch(&mut self, bus: &mut impl Bus, code: u8) {
        if self.latched_waveform != 0 && self.latched_waveform != code {
            self.init(bus);
        }
        bus.cmd(0xe0);
        bus.data8(0x12);
        bus.cmd(0xe6);
        bus.data8(code);
        bus.cmd(0xa5);
        bus.busy_wait(1, TAG_LATCH);
        bus.delay_ms(10);
        self.latched_waveform = code;
    }
    pub fn begin(&mut self, bus: &mut impl Bus, memory: &mut impl Psram) {
        self.init(bus);
        self.needs_initial_full = true;
        if self.old.ensure(memory) {
            self.old.bytes_mut().fill(255);
        }
    }
    fn packed(&self, bus: &mut impl Bus, hi: &[u8], lo: &[u8], chunk: &mut [u8; 1024]) {
        bus.cmd(0x10);
        bus.txn_begin();
        let mut n = 0;
        for row in 0..self.height {
            let offset = if self.sticky {
                self.height - 1 - row
            } else {
                row
            } * self.width_bytes;
            for xb in 0..self.width_bytes {
                let high = hi.get(offset + xb).copied().unwrap_or(255);
                let low = lo[offset + xb];
                let mut packed = 0u16;
                for bit in (0..8).rev() {
                    packed =
                        (packed << 2) | u16::from((((high >> bit) & 1) << 1) | ((low >> bit) & 1));
                }
                chunk[n] = (packed >> 8) as u8;
                chunk[n + 1] = packed as u8;
                n += 2;
                if n == chunk.len() {
                    bus.txn_write(chunk);
                    n = 0;
                }
            }
        }
        if n != 0 {
            bus.txn_write(&chunk[..n]);
        }
        bus.txn_end();
    }
    fn refresh(&mut self, bus: &mut impl Bus, off: bool) {
        if self.needs_power_on {
            bus.cmd(4);
            bus.busy_wait(1, TAG_ON);
            self.needs_power_on = false;
        }
        bus.cmd(0x12);
        bus.data8(0);
        bus.refresh_wait(TAG_REFRESH);
        if off {
            bus.cmd(2);
            bus.data8(0);
            bus.busy_wait(1, TAG_OFF);
            self.needs_power_on = true;
        }
    }
    fn refresh_lut(&mut self, bus: &mut impl Bus, lut: &[u8; 535], off: bool) {
        bus.cmd(0x20);
        bus.data_span(lut);
        self.refresh(bus, off);
    }
    pub fn display(
        &mut self,
        bus: &mut impl Bus,
        fb: &[u8],
        mut mode: u32,
        off: bool,
        chunk: &mut [u8; 1024],
    ) {
        if self.needs_initial_full {
            mode = 0;
        }
        if self.sticky {
            let fast = mode == 2;
            self.latch(bus, if fast { 0x19 } else { 0xee });
            self.packed(bus, if fast { self.old.bytes() } else { fb }, fb, chunk);
            self.refresh(bus, !fast || off);
        } else {
            self.packed(bus, self.old.bytes(), fb, chunk);
            self.refresh_lut(bus, if mode == 2 { &lut::FAST } else { &lut::FULL }, off);
        }
        if self.old.allocated() {
            self.old.bytes_mut().copy_from_slice(fb);
        }
        self.needs_initial_full = false;
    }
    pub fn gray_begin(
        &mut self,
        bus: &mut impl Bus,
        fb: &[u8],
        mode: u32,
        fallback: u32,
        off: bool,
        chunk: &mut [u8; 1024],
    ) {
        self.absolute_gray = self.sticky && mode != 0;
        if !self.absolute_gray {
            self.display(bus, fb, fallback, off, chunk);
        }
    }
    pub fn gray_lsb(&mut self, data: &[u8], memory: &mut impl Psram) {
        if self.lsb.ensure(memory) {
            self.lsb.bytes_mut().copy_from_slice(data);
        }
    }
    pub fn gray_msb(&mut self, bus: &mut impl Bus, data: &[u8], chunk: &mut [u8; 1024]) {
        if !self.lsb.allocated() {
            return;
        }
        if self.absolute_gray {
            self.latch(bus, 0xfa);
        } else if self.sticky {
            self.init(bus);
        }
        self.packed(bus, data, self.lsb.bytes(), chunk);
    }
    pub fn gray_display(
        &mut self,
        bus: &mut impl Bus,
        fb: &[u8],
        off: bool,
        chunk: &mut [u8; 1024],
    ) {
        if self.absolute_gray {
            self.absolute_gray = false;
            if !self.lsb.allocated() {
                self.display(bus, fb, 0, off, chunk);
                return;
            }
            self.refresh(bus, true);
            self.needs_initial_full = true;
            return;
        }
        if self.sticky {
            if !self.lsb.allocated() {
                return;
            }
            self.refresh_lut(bus, &lut::GRAY, off);
            self.latched_waveform = 0xff;
            return;
        }
        if !self.lsb.allocated() {
            self.display(bus, fb, 0, off, chunk);
            return;
        }
        self.refresh_lut(bus, &lut::GRAY, off);
    }
    pub fn sleep(&mut self, bus: &mut impl Bus) {
        if self.sticky && !self.needs_power_on {
            bus.cmd(2);
            bus.data8(0);
            bus.busy_wait(1, TAG_OFF);
        }
        bus.cmd(7);
        bus.data8(0xa5);
        self.needs_power_on = true;
        self.needs_initial_full = true;
    }
}
