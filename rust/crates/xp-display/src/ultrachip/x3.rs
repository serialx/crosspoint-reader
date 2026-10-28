use super::tags::*;
use super::{fill, flipped, wait_start, x3_luts::*};
use crate::{constants::*, Bus};

// Tags 30..40 preserve the SDK's UC8279-X3 wait messages.
const PON: u32 = TAG_UC8279_X3_PON;
const POF: u32 = TAG_UC8279_X3_POF;
const DRF: u32 = TAG_UC8279_X3_DRF;
const GRAY_PON: u32 = TAG_UC8279_X3_GRAY_PON;
const GRAY_DRF: u32 = TAG_UC8279_X3_GRAY_DRF;
const DIRECT_DRF: u32 = TAG_UC8279_X3_DIRECT_GRAY_DRF;
const GRAY_POF: u32 = TAG_UC8279_X3_GRAY_POF;
const BW_TARGET: u32 = TAG_UC8279_X3_BW_TARGET_DRF;
const POWER_DOWN: u32 = TAG_UC8279_X3_POWER_DOWN;

const STATE_SCREEN_ON: u16 = 1 << 0;
const STATE_FIRST: u16 = 1 << 1;
const STATE_OLD_VALID: u16 = 1 << 2;
const STATE_FORCE_FULL: u16 = 1 << 3;
const STATE_IN_GRAY: u16 = 1 << 4;
const STATE_LSB_VALID: u16 = 1 << 5;
const STATE_DIRECT_PASS: u16 = 1 << 6;
const STATE_DIRECT_ON_PANEL: u16 = 1 << 7;
const STATE_PENDING: u16 = 1 << 8;
const STATE_PENDING_OFF: u16 = 1 << 9;
const STATE_PENDING_GC: u16 = 1 << 10;

pub struct Uc8279X3 {
    width: u16,
    height: u16,
    initial_fulls: u8,
    state_flags: u16,
}

impl Uc8279X3 {
    fn has(&self, flag: u16) -> bool {
        self.state_flags & flag != 0
    }
    fn set(&mut self, flag: u16, value: bool) {
        if value {
            self.state_flags |= flag;
        } else {
            self.state_flags &= !flag;
        }
    }

    pub const fn new(width: u16, height: u16) -> Self {
        Self {
            state_flags: STATE_FIRST,
            width,
            height,
            initial_fulls: 0,
        }
    }
    pub fn capabilities(&self, mode: u32) -> u32 {
        CAP_ASYNC_DISPLAY
            | match mode {
                0 => CAP_OVERLAY | CAP_STRIPS,
                1 => CAP_ABSOLUTE | CAP_STRIPS,
                2 => CAP_ABSOLUTE | CAP_STRIPS | CAP_COMBINED,
                _ => 0,
            }
    }
    fn wait(bus: &mut impl Bus, tag: u32) {
        bus.busy_wait(2, tag);
    }
    fn window(bus: &mut impl Bus) {
        bus.cmd(0x91);
        bus.cmd_data(0x90, &[0, 0, 3, 0x17, 0, 0, 2, 0x0f, 1]);
    }
    fn bank(bus: &mut impl Bus, bank: &[[u8; 43]; 5]) {
        for row in bank {
            bus.cmd(row[0]);
            bus.data_span(&row[1..]);
        }
    }
    fn plane(&self, bus: &mut impl Bus, cmd: u8, data: &[u8], invert: bool) {
        flipped(bus, cmd, data, self.width / 8, invert);
        bus.cmd(0x11);
    }
    fn cdi(&self, bus: &mut impl Bus) {
        bus.cmd(0x50);
        bus.data8(if self.has(STATE_FIRST) { 0x97 } else { 0xd7 });
    }
    pub fn begin(&mut self, bus: &mut impl Bus) {
        self.set(STATE_DIRECT_ON_PANEL, false);
        self.set(STATE_DIRECT_PASS, false);
        bus.reset(50);
        self.set(STATE_FORCE_FULL, false);
        let mut i = 0;
        while i < INIT.len() {
            let cmd = INIT[i];
            let n = INIT[i + 1] as usize;
            i += 2;
            bus.cmd(cmd);
            for value in &INIT[i..i + n] {
                bus.data8(*value);
            }
            i += n;
        }
        self.set(STATE_SCREEN_ON, false);
        self.set(STATE_FIRST, true);
        self.set(STATE_OLD_VALID, false);
        self.initial_fulls = 2;
    }
    pub fn display(&mut self, bus: &mut impl Bus, data: &[u8], mode: u32, off: bool) {
        self.start(bus, data, mode, off);
        self.finish(bus, data);
    }
    pub fn start(&mut self, bus: &mut impl Bus, data: &[u8], mode: u32, off: bool) {
        let paint = self.has(STATE_DIRECT_ON_PANEL);
        self.set(STATE_DIRECT_ON_PANEL, false);
        self.set(STATE_DIRECT_PASS, false);
        let gc = mode != 2
            || !self.has(STATE_OLD_VALID)
            || self.has(STATE_FORCE_FULL)
            || self.initial_fulls > 0;
        if paint {
            Self::window(bus);
            self.plane(bus, 0x10, data, true);
            self.plane(bus, 0x13, data, false);
            self.cdi(bus);
            Self::bank(bus, &BW_DU);
            self.power_on(bus, PON);
            bus.cmd(0x12);
            Self::wait(bus, BW_TARGET);
            bus.cmd(0x92);
        }
        bus.cmd(0x91);
        if !self.has(STATE_OLD_VALID) {
            fill(bus, 0x10, self.height, self.width / 8);
            bus.cmd(0x11);
        }
        self.plane(bus, 0x13, data, false);
        self.cdi(bus);
        Self::bank(bus, if gc { &BW_GC } else { &BW_DU });
        self.set(STATE_PENDING_GC, gc);
        self.power_on(bus, PON);
        bus.cmd(0x12);
        wait_start(bus);
        self.set(STATE_PENDING_OFF, off);
        self.set(STATE_PENDING, true);
    }
    fn power_on(&mut self, bus: &mut impl Bus, tag: u32) {
        if !self.has(STATE_SCREEN_ON) {
            bus.cmd(4);
            Self::wait(bus, tag);
            self.set(STATE_SCREEN_ON, true);
        }
    }
    fn power_off(&mut self, bus: &mut impl Bus, tag: u32) {
        bus.cmd(2);
        Self::wait(bus, tag);
        self.set(STATE_SCREEN_ON, false);
    }
    pub fn finish(&mut self, bus: &mut impl Bus, data: &[u8]) {
        if !self.has(STATE_PENDING) {
            return;
        }
        self.set(STATE_PENDING, false);
        bus.refresh_wait(DRF);
        bus.cmd(0x50);
        bus.data8(0xd7);
        self.plane(bus, 0x10, data, false);
        bus.cmd(0x92);
        self.set(STATE_OLD_VALID, true);
        self.set(STATE_FIRST, false);
        self.set(STATE_FORCE_FULL, false);
        if self.has(STATE_PENDING_GC) && self.initial_fulls > 0 {
            self.initial_fulls -= 1;
        }
        if self.has(STATE_PENDING_OFF) {
            self.power_off(bus, POF);
        }
    }
    pub fn resync(&mut self) {
        self.set(STATE_DIRECT_PASS, false);
        self.set(STATE_LSB_VALID, false);
        self.set(STATE_FORCE_FULL, true);
    }
    pub fn skip_initial(&mut self) {
        self.set(STATE_OLD_VALID, true);
        self.initial_fulls = 0;
    }
    pub fn sleep(&mut self, bus: &mut impl Bus) {
        self.set(STATE_DIRECT_ON_PANEL, false);
        self.set(STATE_DIRECT_PASS, false);
        self.set(STATE_LSB_VALID, false);
        if self.has(STATE_SCREEN_ON) {
            self.power_off(bus, POWER_DOWN);
        }
        bus.cmd(7);
        bus.data8(0xa5);
    }
    fn gray_refresh(&mut self, bus: &mut impl Bus, off: bool) {
        self.power_on(bus, GRAY_PON);
        bus.cmd(0x12);
        Self::wait(
            bus,
            if self.has(STATE_DIRECT_PASS) {
                DIRECT_DRF
            } else {
                GRAY_DRF
            },
        );
        if off {
            self.power_off(bus, GRAY_POF);
        }
    }
    pub fn lsb(&mut self, bus: &mut impl Bus, data: Option<&[u8]>) {
        self.set(STATE_LSB_VALID, false);
        let Some(data) = data else {
            return;
        };
        Self::window(bus);
        self.plane(bus, 0x10, data, false);
        bus.cmd(0x92);
        self.set(STATE_LSB_VALID, true);
    }
    pub fn msb(&mut self, bus: &mut impl Bus, data: Option<&[u8]>) {
        let Some(data) = data else {
            return;
        };
        if !self.has(STATE_LSB_VALID) {
            return;
        }
        Self::window(bus);
        self.plane(bus, 0x13, data, false);
        bus.cmd(0x92);
    }
    pub fn strip(
        &mut self,
        bus: &mut impl Bus,
        data: Option<&[u8]>,
        plane: u32,
        y: u16,
        rows: u16,
    ) {
        let Some(data) = data else {
            return;
        };
        if rows == 0 || y >= self.height || rows > self.height - y {
            return;
        }
        let end = self.width - 1;
        let ys = self.height - y - rows;
        let ye = self.height - 1 - y;
        bus.cmd(0x91);
        bus.cmd_data(
            0x90,
            &[
                0,
                0,
                (end >> 8) as u8,
                end as u8,
                (ys >> 8) as u8,
                ys as u8,
                (ye >> 8) as u8,
                ye as u8,
                1,
            ],
        );
        flipped(
            bus,
            if plane == 0 { 0x10 } else { 0x13 },
            data,
            self.width / 8,
            false,
        );
        bus.cmd(0x92);
        if plane == 0 {
            self.set(STATE_LSB_VALID, true);
        }
    }
    pub fn gray(&mut self, bus: &mut impl Bus, off: bool, factory: bool) {
        if !self.has(STATE_LSB_VALID) {
            return;
        }
        let absolute = factory || self.has(STATE_DIRECT_PASS);
        self.set(STATE_IN_GRAY, !absolute);
        Self::window(bus);
        let regs = if absolute {
            [0x20, 0x24, 0x22, 0x23, 0x21]
        } else {
            [0x20, 0x23, 0x22, 0x21, 0x24]
        };
        for (reg, row) in regs.iter().zip(if absolute { &XTH4 } else { &XTF_AA }) {
            bus.cmd(*reg);
            bus.data_span(row);
        }
        self.cdi(bus);
        self.gray_refresh(bus, off);
        bus.cmd(0x92);
        self.set(STATE_FIRST, false);
        self.set(STATE_OLD_VALID, false);
        self.set(STATE_DIRECT_ON_PANEL, self.has(STATE_DIRECT_PASS));
        self.set(STATE_FORCE_FULL, absolute);
        self.set(STATE_LSB_VALID, false);
        if self.has(STATE_DIRECT_PASS) {
            self.initial_fulls = 0;
        }
        self.set(STATE_DIRECT_PASS, false);
    }
    pub fn gray_begin(
        &mut self,
        bus: &mut impl Bus,
        data: &[u8],
        mode: u32,
        fallback: u32,
        off: bool,
    ) {
        self.set(STATE_DIRECT_PASS, mode == 2);
        if self.has(STATE_DIRECT_PASS) {
            self.set(STATE_LSB_VALID, false);
            self.set(STATE_OLD_VALID, false);
            self.set(STATE_IN_GRAY, false);
            self.set(STATE_FORCE_FULL, true);
            return;
        }
        self.gray_base(bus, data, fallback, off);
    }
    fn pre_lut(bus: &mut impl Bus) {
        bus.cmd(0xe0);
        bus.data8(2);
        bus.cmd(0xe5);
        bus.data8(0x5a);
        Self::bank(bus, &XTF_PRE_BW_MID);
    }
    pub fn gray_base(&mut self, bus: &mut impl Bus, data: &[u8], fallback: u32, off: bool) {
        if self.has(STATE_IN_GRAY) {
            self.revert(bus);
        }
        let clean = !self.has(STATE_OLD_VALID)
            || self.has(STATE_LSB_VALID)
            || self.has(STATE_FORCE_FULL)
            || self.initial_fulls > 0;
        if clean {
            self.display(bus, data, fallback, false);
        }
        Self::window(bus);
        if !clean {
            self.plane(bus, 0x13, data, false);
        }
        bus.cmd(0x50);
        bus.data8(0xd7);
        Self::pre_lut(bus);
        self.gray_refresh(bus, off);
        if !clean {
            self.plane(bus, 0x10, data, false);
        }
        bus.cmd(0x92);
        if !clean {
            self.set(STATE_OLD_VALID, true);
        }
    }
    pub fn precondition(&mut self, bus: &mut impl Bus, x: u16, y: u16, w: u16, h: u16) {
        if w == 0
            || h == 0
            || x >= self.width
            || y >= self.height
            || self.has(STATE_IN_GRAY)
            || !self.has(STATE_OLD_VALID)
            || self.has(STATE_LSB_VALID)
        {
            return;
        }
        let xe = ((x as u32 + w as u32 - 1).min(self.width as u32 - 1) as u16) | 7;
        let yend = (y as u32 + h as u32 - 1).min(self.height as u32 - 1) as u16;
        let xs = x & !7;
        let ys = self.height - 1 - yend;
        let ye = self.height - 1 - y;
        bus.cmd(0x91);
        bus.cmd_data(
            0x90,
            &[
                (xs >> 8) as u8,
                xs as u8,
                (xe >> 8) as u8,
                xe as u8,
                (ys >> 8) as u8,
                ys as u8,
                (ye >> 8) as u8,
                ye as u8,
                1,
            ],
        );
        self.cdi(bus);
        Self::pre_lut(bus);
        self.gray_refresh(bus, false);
        bus.cmd(0x92);
    }
    pub fn cleanup(&mut self, bus: &mut impl Bus, data: Option<&[u8]>) {
        self.set(STATE_DIRECT_PASS, false);
        let Some(data) = data else {
            self.set(STATE_LSB_VALID, false);
            self.set(STATE_OLD_VALID, false);
            self.set(STATE_FORCE_FULL, true);
            return;
        };
        Self::window(bus);
        self.plane(bus, 0x13, data, false);
        self.plane(bus, 0x10, data, false);
        bus.cmd(0x92);
        self.set(STATE_LSB_VALID, false);
        self.set(STATE_OLD_VALID, true);
        self.set(STATE_IN_GRAY, false);
    }
    pub fn revert(&mut self, bus: &mut impl Bus) {
        if !self.has(STATE_IN_GRAY) {
            return;
        }
        self.set(STATE_IN_GRAY, false);
        Self::window(bus);
        for cmd in [0x10, 0x13] {
            fill(bus, cmd, self.height, self.width / 8);
            bus.cmd(0x11);
        }
        bus.cmd(0x50);
        bus.data8(0xd7);
        Self::bank(bus, &BW_GC);
        self.gray_refresh(bus, false);
        bus.cmd(0x92);
        self.set(STATE_OLD_VALID, true);
        self.set(STATE_LSB_VALID, false);
    }
}
