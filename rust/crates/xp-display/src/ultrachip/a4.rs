use super::tags::*;
use super::{a4_luts::*, fill, Memory, Plane};
use crate::{constants::*, Bus};

const SCREEN_ON: u16 = 1 << 0;
const GRAY_CONTROLLER: u16 = 1 << 1;
const REDRIVE: u16 = 1 << 2;
const FIRST: u16 = 1 << 3;
const OVERLAY: u16 = 1 << 4;
const BASE_PENDING: u16 = 1 << 5;
const CLEANUP_PAGE: u16 = 1 << 6;
const HISTORY_VALID: u16 = 1 << 7;
const SESSION: u16 = 1 << 8;

pub struct Uc8279cA4 {
    flags: u16,
    lsb: Plane,
    msb: Plane,
    base: Plane,
    white: Plane,
    middle: Plane,
}

impl Uc8279cA4 {
    fn has(&self, flag: u16) -> bool {
        self.flags & flag != 0
    }
    fn set(&mut self, flag: u16, value: bool) {
        if value {
            self.flags |= flag;
        } else {
            self.flags &= !flag;
        }
    }
    pub const fn new() -> Self {
        Self {
            flags: OVERLAY,
            lsb: Plane::new(),
            msb: Plane::new(),
            base: Plane::new(),
            white: Plane::new(),
            middle: Plane::new(),
        }
    }
    /// # Safety
    /// Use the allocator that supplied this panel's retained planes.
    pub unsafe fn release(&mut self, memory: &mut impl Memory) {
        for plane in [
            &mut self.lsb,
            &mut self.msb,
            &mut self.base,
            &mut self.white,
            &mut self.middle,
        ] {
            plane.release(memory);
        }
    }
    fn size(&self) -> usize {
        96 * 552
    }
    pub fn capabilities(&self, mode: u32) -> u32 {
        (if mode == 0 { CAP_OVERLAY } else { CAP_ABSOLUTE }) | CAP_COMBINED | CAP_STRIPS
    }
    fn reset(bus: &mut impl Bus) {
        bus.reset_level(1);
        bus.delay_ms(20);
        bus.reset_level(0);
        bus.delay_ms(10);
        bus.reset_level(1);
        bus.delay_ms(100);
    }
    fn init(&mut self, bus: &mut impl Bus, gray: bool) {
        bus.cmd_data(0, &[0x3f, 0x4a]);
        bus.cmd_data(3, &[0x20]);
        bus.cmd_data(
            1,
            if gray {
                &[3, 0, 0x72, 0x72]
            } else {
                &[0x43, 0, 0x78, 0x78, 0x17]
            },
        );
        bus.cmd_data(6, &[0x25, 0x25, 0x3c]);
        bus.cmd_data(0x82, &[if gray { 0x32 } else { 0x24 }]);
        bus.cmd_data(0x30, &[0x0f]);
        bus.cmd_data(0x61, &[3, 0, 2, 0x58]);
        bus.cmd_data(0x65, &[0, 0, 0, 0]);
        bus.cmd_data(0xe1, &[2]);
        fill(bus, 0x10, 600, 96);
        bus.cmd(4);
        bus.busy_wait(1, TAG_A4_POWER_ON);
        self.set(SCREEN_ON, true);
        self.set(GRAY_CONTROLLER, gray);
    }
    fn on(&mut self, bus: &mut impl Bus) {
        if !self.has(SCREEN_ON) {
            bus.cmd(4);
            bus.busy_wait(1, TAG_A4_POWER_ON);
            self.set(SCREEN_ON, true);
        }
    }
    fn frame(bus: &mut impl Bus, cmd: u8, data: &[u8], invert: bool) {
        let white = [0xff; 96];
        let mut row = [0; 96];
        bus.cmd(cmd);
        bus.txn_begin();
        for src in data.as_chunks::<96>().0.iter().rev() {
            if invert {
                for (dst, src) in row.iter_mut().zip(src) {
                    *dst = !src;
                }
                bus.txn_write(&row);
            } else {
                bus.txn_write(src);
            }
        }
        for _ in 0..48 {
            bus.txn_write(&white);
        }
        bus.txn_end();
    }
    fn refresh(&mut self, bus: &mut impl Bus, off: bool, baseline: Option<&[u8]>) {
        bus.cmd(0x12);
        bus.busy_wait(1, TAG_A4_REFRESH);
        if let Some(data) = baseline {
            Self::frame(bus, 0x10, data, false);
        }
        if off {
            bus.cmd(2);
            bus.busy_wait(1, TAG_A4_POWER_OFF);
            self.set(SCREEN_ON, false);
        }
    }
    pub fn begin(&mut self, bus: &mut impl Bus) {
        self.set(REDRIVE, false);
        self.set(HISTORY_VALID, false);
        self.set(SESSION, false);
        self.set(BASE_PENDING, false);
        Self::reset(bus);
        self.init(bus, false);
        self.set(FIRST, true);
    }
    pub fn display(
        &mut self,
        bus: &mut impl Bus,
        memory: &mut impl Memory,
        data: &[u8],
        mode: u32,
        off: bool,
    ) {
        self.set(HISTORY_VALID, false);
        self.set(SESSION, false);
        self.set(BASE_PENDING, false);
        if self.has(GRAY_CONTROLLER) {
            Self::reset(bus);
            self.init(bus, false);
        }
        self.on(bus);
        let fast = mode == 2 && !self.has(FIRST);
        bus.cmd_data(0x50, &[if fast { 0xd7 } else { 0x97 }]);
        for (i, row) in (if fast { &FAST } else { &FULL }).iter().enumerate() {
            bus.cmd_data(0x20 + i as u8, row);
        }
        if self.has(FIRST) {
            Self::frame(bus, 0x10, data, false);
            Self::frame(bus, 0x13, data, true);
            self.refresh(bus, false, None);
        }
        if self.has(FIRST) || self.has(REDRIVE) {
            Self::frame(bus, 0x10, data, true);
            self.set(FIRST, false);
        }
        Self::frame(bus, 0x13, data, false);
        self.refresh(bus, off, Some(data));
        let n = self.size();
        self.base.ensure(memory, n, false);
        if data.as_ptr() != self.base.0 {
            if let Some(base) = self.base.bytes_mut(n) {
                base.copy_from_slice(data);
            }
        }
        self.set(REDRIVE, false);
    }
    // Internal baseline rendering borrows one owned plane while mutating state.
    fn display_base(&mut self, bus: &mut impl Bus, memory: &mut impl Memory, off: bool) {
        if self.base.present() {
            let ptr = self.base.0;
            let data = unsafe { core::slice::from_raw_parts(ptr, self.size()) };
            self.display(
                bus,
                memory,
                data,
                if self.has(CLEANUP_PAGE) { 1 } else { 2 },
                off,
            );
        }
    }
    pub fn gray_begin(
        &mut self,
        memory: &mut impl Memory,
        data: Option<&[u8]>,
        mode: u32,
        fallback: u32,
    ) {
        self.set(OVERLAY, mode == 0);
        if !self.has(OVERLAY) {
            return;
        }
        let n = self.size();
        self.base.ensure(memory, n, false);
        if let (Some(base), Some(data)) = (self.base.bytes_mut(n), data) {
            base.copy_from_slice(data);
            self.set(BASE_PENDING, true);
            self.set(CLEANUP_PAGE, fallback != 2);
        }
    }
    pub fn copy(
        &mut self,
        memory: &mut impl Memory,
        data: Option<&[u8]>,
        plane: u32,
        y: u16,
        rows: u16,
    ) {
        let Some(data) = data else {
            return;
        };
        if rows == 0 || y as u32 + rows as u32 > 552u32 {
            return;
        }
        let n = self.size();
        let stride = 96;
        let dst = if plane == 0 {
            &mut self.lsb
        } else {
            &mut self.msb
        };
        dst.ensure(memory, n, false);
        if let Some(dst) = dst.bytes_mut(n) {
            dst[y as usize * stride..(y as usize + rows as usize) * stride].copy_from_slice(data);
        }
    }
    fn codes(&self, bus: &mut impl Bus, dtm1: bool) -> (bool, bool) {
        let n = self.size();
        let (Some(lsb), Some(msb), Some(white), Some(middle)) = (
            self.lsb.bytes(n),
            self.msb.bytes(n),
            self.white.bytes(n),
            self.middle.bytes(n),
        ) else {
            return (false, false);
        };
        let mut row = [0; 96];
        let mut black_present = false;
        let mut clear_present = false;
        bus.cmd(if dtm1 { 0x10 } else { 0x13 });
        bus.txn_begin();
        for y in (0..552usize).rev() {
            for (x, dst) in row.iter_mut().enumerate() {
                let i = y * 96 + x;
                let black = !(lsb[i] | msb[i]);
                let clear = lsb[i] & !white[i];
                let gray = msb[i] & !middle[i];
                black_present |= black != 0;
                clear_present |= clear != 0;
                *dst = clear | if dtm1 { black } else { gray };
            }
            bus.txn_write(&row);
        }
        row.fill(0);
        for _ in 0..48 {
            bus.txn_write(&row);
        }
        bus.txn_end();
        (black_present, clear_present)
    }
    fn show_gray3(&mut self, bus: &mut impl Bus, off: bool) {
        if !self.has(SESSION) {
            Self::reset(bus);
            self.init(bus, true);
        } else {
            self.on(bus);
        }
        let (black, clear) = self.codes(bus, true);
        self.codes(bus, false);
        bus.cmd_data(0x50, &[0]);
        for (i, source) in GRAY3.iter().enumerate() {
            let mut record = [0; 49];
            record[..14].copy_from_slice(source);
            if i == 4 && !clear {
                record[2] = 0x90;
            }
            if i == 2 && !black {
                record[1] = 0x4e;
            }
            if i == 3 && !black {
                record[2] = 9;
            }
            bus.cmd_data(0x20 + i as u8, &record);
        }
        self.refresh(bus, off, None);
        let n = self.size();
        if let (Some(dst), Some(src)) = (self.white.bytes_mut(n), self.lsb.bytes(n)) {
            dst.copy_from_slice(src);
        }
        if let (Some(dst), Some(src)) = (self.middle.bytes_mut(n), self.msb.bytes(n)) {
            dst.copy_from_slice(src);
        }
        self.set(HISTORY_VALID, true);
        self.set(SESSION, true);
        self.set(FIRST, false);
        self.set(REDRIVE, true);
    }
    pub fn gray(&mut self, bus: &mut impl Bus, memory: &mut impl Memory, off: bool) {
        let overlay = self.has(OVERLAY);
        self.set(OVERLAY, true);
        let n = self.size();
        if overlay {
            if self.lsb.present() && self.msb.present() && self.base.present() {
                self.white.ensure(memory, n, false);
                self.middle.ensure(memory, n, false);
            }
            if !self.lsb.present()
                || !self.msb.present()
                || !self.base.present()
                || !self.white.present()
                || !self.middle.present()
            {
                if self.has(BASE_PENDING) {
                    self.display_base(bus, memory, off);
                }
                return;
            }
            if !self.has(HISTORY_VALID) {
                if self.has(BASE_PENDING) {
                    self.display_base(bus, memory, false);
                }
                self.set(CLEANUP_PAGE, false);
                if let (Some(dst), Some(src)) = (self.white.bytes_mut(n), self.base.bytes(n)) {
                    dst.copy_from_slice(src);
                }
                if let Some(dst) = self.middle.bytes_mut(n) {
                    dst.fill(0);
                }
                self.set(HISTORY_VALID, true);
            }
            if let (Some(lsb), Some(msb), Some(base)) =
                (self.lsb.bytes_mut(n), self.msb.bytes(n), self.base.bytes(n))
            {
                for ((dst, gray), base) in lsb.iter_mut().zip(msb).zip(base) {
                    *dst = base & !gray;
                }
            }
            if self.has(CLEANUP_PAGE) {
                self.invert_gray3();
                self.show_gray3(bus, false);
                self.invert_gray3();
            }
            self.show_gray3(bus, off);
            self.set(BASE_PENDING, false);
            self.set(CLEANUP_PAGE, false);
            return;
        }
        if !self.lsb.present() || !self.msb.present() {
            return;
        }
        self.set(HISTORY_VALID, false);
        self.set(SESSION, false);
        Self::reset(bus);
        self.init(bus, true);
        if let Some(data) = self.msb.bytes(n) {
            Self::frame(bus, 0x10, data, false);
        }
        if let Some(data) = self.lsb.bytes(n) {
            Self::frame(bus, 0x13, data, false);
        }
        self.set(FIRST, false);
        for (i, row) in GRAY.iter().enumerate() {
            bus.cmd_data(0x20 + i as u8, row);
        }
        self.refresh(bus, off, None);
        self.set(REDRIVE, true);
    }
    fn invert_gray3(&mut self) {
        let n = self.size();
        if let (Some(lsb), Some(msb)) = (self.lsb.bytes_mut(n), self.msb.bytes(n)) {
            for (dst, gray) in lsb.iter_mut().zip(msb) {
                *dst = !(*dst | gray);
            }
        }
    }
    pub fn cleanup(&mut self, bus: &mut impl Bus, memory: &mut impl Memory) {
        if self.has(BASE_PENDING) && self.base.present() {
            self.display_base(bus, memory, false);
        }
        self.set(BASE_PENDING, false);
    }
    pub fn sleep(&mut self, bus: &mut impl Bus) {
        if self.has(SCREEN_ON) {
            bus.cmd(2);
            bus.busy_wait(1, TAG_A4_POWER_OFF);
            self.set(SCREEN_ON, false);
        }
        bus.cmd_data(7, &[0xa5]);
    }
}

impl Default for Uc8279cA4 {
    fn default() -> Self {
        Self::new()
    }
}
