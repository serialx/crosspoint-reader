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

pub struct Uc8279X3 {
    width: u16,
    height: u16,
    screen_on: bool,
    first: bool,
    old_valid: bool,
    force_full: bool,
    initial_fulls: u8,
    in_gray: bool,
    lsb_valid: bool,
    direct_pass: bool,
    direct_on_panel: bool,
    pending: bool,
    pending_off: bool,
    pending_gc: bool,
}

impl Uc8279X3 {
    pub const fn new(width: u16, height: u16) -> Self {
        Self {
            width,
            height,
            screen_on: false,
            first: true,
            old_valid: false,
            force_full: false,
            initial_fulls: 0,
            in_gray: false,
            lsb_valid: false,
            direct_pass: false,
            direct_on_panel: false,
            pending: false,
            pending_off: false,
            pending_gc: false,
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
        bus.data8(if self.first { 0x97 } else { 0xd7 });
    }
    pub fn begin(&mut self, bus: &mut impl Bus) {
        self.direct_on_panel = false;
        self.direct_pass = false;
        bus.reset(50);
        self.force_full = false;
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
        self.screen_on = false;
        self.first = true;
        self.old_valid = false;
        self.initial_fulls = 2;
    }
    pub fn display(&mut self, bus: &mut impl Bus, data: &[u8], mode: u32, off: bool) {
        self.start(bus, data, mode, off);
        self.finish(bus, data);
    }
    pub fn start(&mut self, bus: &mut impl Bus, data: &[u8], mode: u32, off: bool) {
        let paint = self.direct_on_panel;
        self.direct_on_panel = false;
        self.direct_pass = false;
        let gc = mode != 2 || !self.old_valid || self.force_full || self.initial_fulls > 0;
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
        if !self.old_valid {
            fill(bus, 0x10, self.height, self.width / 8);
            bus.cmd(0x11);
        }
        self.plane(bus, 0x13, data, false);
        self.cdi(bus);
        Self::bank(bus, if gc { &BW_GC } else { &BW_DU });
        self.pending_gc = gc;
        self.power_on(bus, PON);
        bus.cmd(0x12);
        wait_start(bus);
        self.pending_off = off;
        self.pending = true;
    }
    fn power_on(&mut self, bus: &mut impl Bus, tag: u32) {
        if !self.screen_on {
            bus.cmd(4);
            Self::wait(bus, tag);
            self.screen_on = true;
        }
    }
    fn power_off(&mut self, bus: &mut impl Bus, tag: u32) {
        bus.cmd(2);
        Self::wait(bus, tag);
        self.screen_on = false;
    }
    pub fn finish(&mut self, bus: &mut impl Bus, data: &[u8]) {
        if !self.pending {
            return;
        }
        self.pending = false;
        bus.refresh_wait(DRF);
        bus.cmd(0x50);
        bus.data8(0xd7);
        self.plane(bus, 0x10, data, false);
        bus.cmd(0x92);
        self.old_valid = true;
        self.first = false;
        self.force_full = false;
        if self.pending_gc && self.initial_fulls > 0 {
            self.initial_fulls -= 1;
        }
        if self.pending_off {
            self.power_off(bus, POF);
        }
    }
    pub fn resync(&mut self) {
        self.direct_pass = false;
        self.lsb_valid = false;
        self.force_full = true;
    }
    pub fn skip_initial(&mut self) {
        self.old_valid = true;
        self.initial_fulls = 0;
    }
    pub fn sleep(&mut self, bus: &mut impl Bus) {
        self.direct_on_panel = false;
        self.direct_pass = false;
        self.lsb_valid = false;
        if self.screen_on {
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
            if self.direct_pass {
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
        self.lsb_valid = false;
        let Some(data) = data else {
            return;
        };
        Self::window(bus);
        self.plane(bus, 0x10, data, false);
        bus.cmd(0x92);
        self.lsb_valid = true;
    }
    pub fn msb(&mut self, bus: &mut impl Bus, data: Option<&[u8]>) {
        let Some(data) = data else {
            return;
        };
        if !self.lsb_valid {
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
            self.lsb_valid = true;
        }
    }
    pub fn gray(&mut self, bus: &mut impl Bus, off: bool, factory: bool) {
        if !self.lsb_valid {
            return;
        }
        let absolute = factory || self.direct_pass;
        self.in_gray = !absolute;
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
        self.first = false;
        self.old_valid = false;
        self.direct_on_panel = self.direct_pass;
        self.force_full = absolute;
        self.lsb_valid = false;
        if self.direct_pass {
            self.initial_fulls = 0;
        }
        self.direct_pass = false;
    }
    pub fn gray_begin(
        &mut self,
        bus: &mut impl Bus,
        data: &[u8],
        mode: u32,
        fallback: u32,
        off: bool,
    ) {
        self.direct_pass = mode == 2;
        if self.direct_pass {
            self.lsb_valid = false;
            self.old_valid = false;
            self.in_gray = false;
            self.force_full = true;
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
        if self.in_gray {
            self.revert(bus);
        }
        let clean = !self.old_valid || self.lsb_valid || self.force_full || self.initial_fulls > 0;
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
            self.old_valid = true;
        }
    }
    pub fn precondition(&mut self, bus: &mut impl Bus, x: u16, y: u16, w: u16, h: u16) {
        if w == 0
            || h == 0
            || x >= self.width
            || y >= self.height
            || self.in_gray
            || !self.old_valid
            || self.lsb_valid
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
        self.direct_pass = false;
        let Some(data) = data else {
            self.lsb_valid = false;
            self.old_valid = false;
            self.force_full = true;
            return;
        };
        Self::window(bus);
        self.plane(bus, 0x13, data, false);
        self.plane(bus, 0x10, data, false);
        bus.cmd(0x92);
        self.lsb_valid = false;
        self.old_valid = true;
        self.in_gray = false;
    }
    pub fn revert(&mut self, bus: &mut impl Bus) {
        if !self.in_gray {
            return;
        }
        self.in_gray = false;
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
        self.old_valid = true;
        self.lsb_valid = false;
    }
}
