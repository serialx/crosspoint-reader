// MIT: Copyright (c) 2026 FreeInk. See LICENSE.freeink.
// Port of FreeInk SDK 37d272d4 Uc8253X3Driver.cpp; waveform policy is unchanged.
use crate::constants::*;
use crate::{luts, Bus, Panel, Request};

#[derive(Clone, Copy, PartialEq, Eq)]
enum Refresh {
    Full,
    Half,
    Fast,
}
impl Refresh {
    fn decode(value: u32) -> Result<Self, u32> {
        match value {
            0 => Ok(Self::Full),
            1 => Ok(Self::Half),
            2 => Ok(Self::Fast),
            _ => Err(INVALID),
        }
    }
}
#[derive(Clone, Copy)]
enum Bank {
    Normal,
    Half,
    Fast,
    Full,
    Gray,
    PreBwMid,
    Direct,
    Absolute,
}

#[repr(C)]
pub struct X3Config {
    pub banks: [[*const u8; 5]; 6],
    pub direct: *const *const u8,
    pub lut_len: u32,
}

pub fn default_config() -> X3Config {
    X3Config {
        banks: [
            &luts::NORMAL,
            &luts::HALF,
            &luts::FAST,
            &luts::FULL,
            &luts::GC,
            &luts::AA_PRE_BW_MID,
        ]
        .map(|b| b.each_ref().map(|row| row.as_ptr())),
        direct: core::ptr::null(),
        lut_len: 42,
    }
}

pub fn direct_luts() -> [*const u8; 5] {
    luts::DIRECT.each_ref().map(|row| row.as_ptr())
}

pub struct Uc8253 {
    width: u16,
    height: u16,
    custom: *const X3Config,
    screen_on: bool,
    red_synced: bool,
    in_gray: bool,
    absolute: bool,
    direct_pass: bool,
    direct_on_panel: bool,
    initial_syncs: u8,
    force_sync: bool,
    condition_passes: u8,
    lsb_valid: bool,
    pending: bool,
    pending_off: bool,
    pending_full: bool,
    pending_fast: bool,
}

impl Uc8253 {
    /// # Safety
    /// A nonnull custom config and all its LUT arrays are immutable and valid
    /// until the controller is dropped. Each LUT covers lut_len bytes.
    pub unsafe fn new(width: u16, height: u16, custom: *const X3Config) -> Result<Self, u32> {
        if width == 0 || !width.is_multiple_of(8) || height == 0 {
            return Err(INVALID);
        }
        if !custom.is_null() {
            let cfg = &*custom;
            if cfg.lut_len > 255
                || (cfg.lut_len != 0 && cfg.banks.iter().flatten().any(|p| p.is_null()))
            {
                return Err(INVALID);
            }
            if cfg.lut_len != 0
                && !cfg.direct.is_null()
                && core::slice::from_raw_parts(cfg.direct, 5)
                    .iter()
                    .any(|p| p.is_null())
            {
                return Err(INVALID);
            }
        }
        Ok(Self {
            width,
            height,
            custom,
            screen_on: false,
            red_synced: false,
            in_gray: false,
            absolute: false,
            direct_pass: false,
            direct_on_panel: false,
            initial_syncs: 0,
            force_sync: false,
            condition_passes: 0,
            lsb_valid: false,
            pending: false,
            pending_off: false,
            pending_full: false,
            pending_fast: false,
        })
    }
    fn direct_supported(&self) -> bool {
        self.custom.is_null() || unsafe { !(*self.custom).direct.is_null() }
    }
    fn frame_size(&self) -> usize {
        usize::from(self.width / 8) * usize::from(self.height)
    }
    fn frame<'a>(&self, data: Option<&'a [u8]>) -> Result<&'a [u8], u32> {
        data.filter(|d| d.len() >= self.frame_size()).ok_or(INVALID)
    }
    fn load(&self, bus: &mut impl Bus, bank: Bank) {
        let (index, bytes) = match bank {
            Bank::Normal => (0, &luts::NORMAL),
            Bank::Half => (1, &luts::HALF),
            Bank::Fast => (2, &luts::FAST),
            Bank::Full => (3, &luts::FULL),
            Bank::Gray | Bank::Absolute => (4, &luts::GC),
            Bank::PreBwMid => (5, &luts::AA_PRE_BW_MID),
            Bank::Direct => (6, &luts::DIRECT),
        };
        for (i, fallback) in bytes.iter().enumerate() {
            let source = if matches!(bank, Bank::Absolute) {
                [0, 4, 2, 1, 4][i]
            } else {
                i
            };
            let data = if self.custom.is_null() {
                if matches!(bank, Bank::Absolute) {
                    &luts::GC[source][..]
                } else {
                    &fallback[..]
                }
            } else {
                // Constructor validates every retained pointer and length.
                let cfg = unsafe { &*self.custom };
                let pointer = if matches!(bank, Bank::Direct) {
                    unsafe { *cfg.direct.add(source) }
                } else {
                    cfg.banks[index][source]
                };
                if cfg.lut_len == 0 {
                    &[]
                } else {
                    unsafe { core::slice::from_raw_parts(pointer, cfg.lut_len as usize) }
                }
            };
            bus.cmd_data(0x20 + i as u8, data);
        }
    }
    fn load_cdi(&self, bus: &mut impl Bus, cdi: u8, bank: Bank) {
        bus.cmd_data(0x50, &[cdi, 0x07]);
        self.load(bus, bank);
    }
    fn fill(&self, bus: &mut impl Bus, command: u8) {
        // Match EpdBus's 128-byte fill chunks, including each row boundary.
        let chunk = [0xff; 128];
        bus.cmd(command);
        bus.txn_begin();
        for _ in 0..self.height {
            let mut remaining = self.width / 8;
            while remaining > 0 {
                let n = remaining.min(128);
                bus.txn_write(&chunk[..usize::from(n)]);
                remaining -= n;
            }
        }
        bus.txn_end();
    }
    fn plane(&self, bus: &mut impl Bus, command: u8, data: &[u8], rows: u16) {
        let stride = usize::from(self.width / 8);
        bus.cmd(command);
        bus.txn_begin();
        for y in (0..usize::from(rows)).rev() {
            bus.txn_write(&data[y * stride..(y + 1) * stride]);
        }
        bus.txn_end();
    }
    fn window(&self, bus: &mut impl Bus, xs: u16, xe: u16, ys: u16, ye: u16) {
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
    }
    fn full_window(&self, bus: &mut impl Bus) {
        self.window(bus, 0, self.width - 1, 0, self.height - 1);
    }
    fn trigger(&mut self, bus: &mut impl Bus, off: bool, tag: u32) {
        if !self.screen_on {
            bus.cmd(0x04);
            bus.busy_wait(2, TAG_X3_PON);
            self.screen_on = true;
        }
        bus.cmd(0x12);
        bus.busy_wait(2, tag);
        if off {
            bus.cmd(0x02);
            bus.busy_wait(2, TAG_X3_POF);
            self.screen_on = false;
        }
    }
    fn display_start(&mut self, bus: &mut impl Bus, fb: &[u8], mut mode: Refresh, off: bool) {
        let leaving_direct = self.direct_on_panel;
        self.absolute = false;
        self.direct_pass = false;
        if self.direct_on_panel {
            self.direct_on_panel = false;
            self.red_synced = false;
            self.in_gray = false;
        }
        if !self.screen_on && !off {
            mode = Refresh::Half;
        }
        if self.in_gray {
            self.revert(bus);
        }
        let fast = mode == Refresh::Fast;
        let full =
            mode == Refresh::Full || !self.red_synced || self.initial_syncs > 0 || self.force_sync;
        if leaving_direct && full {
            self.load_cdi(bus, 0xa9, Bank::Half);
            self.full_window(bus);
            self.plane(bus, 0x13, fb, self.height);
            bus.cmd(0x92);
            self.trigger(bus, false, TAG_X3_BW_TARGET_DRF);
        }
        if full {
            self.load_cdi(bus, 0x29, Bank::Full);
            self.fill(bus, 0x10);
            bus.cmd(0x11);
        } else if mode == Refresh::Half {
            self.load_cdi(bus, 0xa9, Bank::Half);
        } else {
            self.load_cdi(bus, 0x29, Bank::Fast);
        }
        self.plane(bus, 0x13, fb, self.height);
        if !self.screen_on {
            bus.cmd(0x04);
            bus.busy_wait(2, TAG_X3_PON);
            self.screen_on = true;
        }
        bus.cmd(0x12);
        let start = bus.clock_ms();
        while bus.busy_level() == 1 && bus.clock_ms().wrapping_sub(start) < 50 {
            bus.delay_ms(1);
        }
        self.pending_off = off;
        self.pending_full = full;
        self.pending_fast = fast;
        self.pending = true;
    }
    fn display_finish(&mut self, bus: &mut impl Bus, fb: &[u8]) {
        if !self.pending {
            return;
        }
        self.pending = false;
        let off = self.pending_off;
        let full = self.pending_full;
        bus.refresh_wait(TAG_X3_DRF);
        if off {
            bus.cmd(0x02);
            bus.busy_wait(2, TAG_X3_POF);
            self.screen_on = false;
        }
        if !self.pending_fast {
            bus.delay_ms(200);
        }
        let passes = if full && self.force_sync {
            self.condition_passes
        } else if full && self.initial_syncs == 1 {
            1
        } else {
            0
        };
        if passes > 0 {
            self.load_cdi(bus, 0xa9, Bank::Normal);
            for _ in 0..passes {
                self.full_window(bus);
                self.plane(bus, 0x13, fb, self.height);
                bus.cmd(0x92);
                self.trigger(bus, false, TAG_X3_DRF);
            }
        }
        self.plane(bus, 0x10, fb, self.height);
        bus.cmd(0x11);
        self.lsb_valid = false;
        self.red_synced = true;
        if full {
            self.load_cdi(bus, 0x29, Bank::Fast);
            self.plane(bus, 0x13, fb, self.height);
            self.trigger(bus, off, TAG_X3_DRF);
            self.plane(bus, 0x10, fb, self.height);
            bus.cmd(0x11);
        }
        if full && self.initial_syncs > 0 {
            self.initial_syncs -= 1;
        }
        self.force_sync = false;
        self.condition_passes = 0;
    }
    fn display(&mut self, bus: &mut impl Bus, fb: &[u8], mode: Refresh, off: bool) {
        self.display_start(bus, fb, mode, off);
        self.display_finish(bus, fb);
    }
    fn gray_base(&mut self, bus: &mut impl Bus, fb: &[u8], mode: Refresh, off: bool) {
        self.absolute = false;
        self.direct_pass = false;
        if self.in_gray {
            self.revert(bus);
        }
        if !self.red_synced || self.lsb_valid || self.force_sync || self.initial_syncs > 0 {
            self.display(bus, fb, mode, false);
            self.load_cdi(bus, 0xa9, Bank::PreBwMid);
            self.trigger(bus, off, TAG_X3_DRF);
            return;
        }
        self.plane(bus, 0x13, fb, self.height);
        self.load_cdi(bus, 0xa9, Bank::PreBwMid);
        self.trigger(bus, off, TAG_X3_DRF);
        self.plane(bus, 0x10, fb, self.height);
        bus.cmd(0x11);
        self.red_synced = true;
    }
    fn precondition(&mut self, bus: &mut impl Bus, x: u16, y: u16, w: u16, h: u16) {
        if w == 0
            || h == 0
            || x >= self.width
            || y >= self.height
            || self.in_gray
            || !self.red_synced
            || self.lsb_valid
        {
            return;
        }
        let xe = (u32::from(x) + u32::from(w) - 1).min(u32::from(self.width - 1)) as u16;
        let ye = (u32::from(y) + u32::from(h) - 1).min(u32::from(self.height - 1)) as u16;
        self.window(
            bus,
            x & !7,
            xe | 7,
            self.height - 1 - ye,
            self.height - 1 - y,
        );
        self.load_cdi(bus, 0xa9, Bank::PreBwMid);
        self.trigger(bus, false, TAG_X3_DRF);
        bus.cmd(0x92);
    }
    fn strip(
        &mut self,
        bus: &mut impl Bus,
        data: Option<&[u8]>,
        msb: bool,
        y: u16,
        rows: u16,
    ) -> Result<(), u32> {
        let Some(data) = data else {
            return Ok(());
        };
        if rows == 0 {
            return Ok(());
        }
        if data.len() < usize::from(rows) * usize::from(self.width / 8) {
            return Err(INVALID);
        }
        // Preserve SDK's 16-bit gate arithmetic, including raw out-of-range calls.
        let end = y.wrapping_add(rows).wrapping_sub(1);
        self.window(
            bus,
            0,
            self.width - 1,
            (self.height - 1).wrapping_sub(end),
            (self.height - 1).wrapping_sub(y),
        );
        self.plane(bus, if msb { 0x13 } else { 0x10 }, data, rows);
        bus.cmd(0x92);
        if !msb {
            self.lsb_valid = true;
        }
        Ok(())
    }
    fn gray_display(&mut self, bus: &mut impl Bus, off: bool, factory: bool) {
        if !self.lsb_valid {
            return;
        }
        if self.direct_pass {
            self.load_cdi(bus, 0xa9, Bank::Direct);
            self.full_window(bus);
            self.trigger(bus, off, TAG_X3_DIRECT_GRAY_DRF);
            bus.cmd(0x92);
            self.direct_on_panel = true;
            self.absolute = false;
            self.direct_pass = false;
            self.lsb_valid = false;
            self.red_synced = false;
            self.in_gray = false;
            self.initial_syncs = 0;
            self.force_sync = false;
            self.condition_passes = 0;
            return;
        }
        self.in_gray = self.absolute || !factory;
        self.load_cdi(
            bus,
            0x29,
            if self.absolute {
                Bank::Absolute
            } else if factory {
                Bank::Full
            } else {
                Bank::Gray
            },
        );
        self.trigger(bus, off, TAG_X3_DRF);
        self.red_synced = false;
        self.force_sync = false;
        self.condition_passes = 0;
        self.lsb_valid = false;
        self.absolute = false;
        self.direct_pass = false;
    }
    fn cleanup(&mut self, bus: &mut impl Bus, bw: Option<&[u8]>) -> Result<(), u32> {
        if self.direct_pass || self.direct_on_panel {
            self.absolute = false;
            self.direct_pass = false;
            self.lsb_valid = false;
            self.red_synced = false;
            return Ok(());
        }
        if let Some(bw) = bw {
            self.frame(Some(bw))?;
        }
        self.absolute = false;
        self.direct_pass = false;
        let Some(bw) = bw else {
            self.lsb_valid = false;
            self.red_synced = false;
            return Ok(());
        };
        self.plane(bus, 0x13, bw, self.height);
        bus.cmd(0x11);
        self.plane(bus, 0x10, bw, self.height);
        bus.cmd(0x11);
        self.lsb_valid = false;
        self.red_synced = true;
        self.force_sync = false;
        self.condition_passes = 0;
        self.in_gray = false;
        Ok(())
    }
    fn revert(&mut self, bus: &mut impl Bus) {
        if !self.in_gray {
            return;
        }
        self.in_gray = false;
        self.fill(bus, 0x10);
        bus.cmd(0x11);
        self.fill(bus, 0x13);
        bus.cmd(0x11);
        self.load_cdi(bus, 0xa9, Bank::Half);
        self.trigger(bus, false, TAG_X3_DRF);
        self.lsb_valid = false;
        self.red_synced = true;
    }
    pub fn execute(&mut self, bus: &mut impl Bus, r: Request<'_>) -> Result<u32, u32> {
        let off = r.flags & FLAG_TURN_OFF != 0;
        let mut result = RESULT_COMMITTED;
        match r.operation {
            OP_BEGIN => self.begin(bus),
            OP_SLEEP => {
                self.lsb_valid = false;
                self.absolute = false;
                self.direct_pass = false;
                if self.screen_on {
                    bus.cmd(0x02);
                    bus.busy_wait(2, TAG_X3_POWER_DOWN);
                    self.screen_on = false;
                }
                bus.cmd(0x07);
                bus.data8(0xa5);
            }
            OP_DISPLAY | OP_DISPLAY_START | OP_DISPLAY_WINDOW => {
                let fb = self.frame(r.data)?;
                let mode = if r.operation == OP_DISPLAY_WINDOW {
                    Refresh::Fast
                } else {
                    Refresh::decode(r.mode)?
                };
                if r.operation == OP_DISPLAY_START {
                    self.display_start(bus, fb, mode, off);
                    result |= RESULT_ASYNC;
                } else {
                    self.display(bus, fb, mode, off);
                }
            }
            OP_DISPLAY_FINISH => {
                if self.pending {
                    self.display_finish(bus, self.frame(r.data)?);
                }
            }
            OP_GRAY_BEGIN => {
                if r.mode > 2 {
                    return Err(INVALID);
                }
                let fallback = Refresh::decode(r.arg)?;
                if r.mode == 2 && self.direct_supported() {
                    self.absolute = true;
                    self.direct_pass = true;
                    self.lsb_valid = false;
                    self.red_synced = false;
                    self.in_gray = false;
                    bus.cmd_data(0x00, &[0x3f, 0x0a]);
                } else {
                    self.gray_base(bus, self.frame(r.data)?, fallback, off);
                    self.absolute = r.mode == 1;
                }
            }
            OP_GRAY_BASE => self.gray_base(bus, self.frame(r.data)?, Refresh::decode(r.mode)?, off),
            OP_GRAY_PRECONDITION => self.precondition(bus, r.x, r.y, r.width, r.height),
            OP_GRAY_LSB => {
                if self.direct_pass {
                    self.strip(bus, r.data, false, 0, self.height)?;
                } else if let Some(data) = r.data {
                    self.plane(bus, 0x10, self.frame(Some(data))?, self.height);
                    bus.cmd(0x11);
                    self.lsb_valid = true;
                } else {
                    self.lsb_valid = false;
                }
            }
            OP_GRAY_MSB => {
                if let Some(data) = r.data.filter(|_| self.lsb_valid) {
                    if self.direct_pass {
                        self.strip(bus, Some(data), true, 0, self.height)?;
                    } else {
                        self.plane(bus, 0x13, self.frame(Some(data))?, self.height);
                        bus.cmd(0x11);
                    }
                }
            }
            OP_GRAY_STRIP => {
                if r.mode > 1 {
                    return Err(INVALID);
                }
                self.strip(bus, r.data, r.mode == 1, r.y, r.height)?;
            }
            OP_GRAY_DISPLAY => self.gray_display(bus, off, r.flags & FLAG_FACTORY != 0),
            OP_GRAY_CALIBRATE => self.gray_display(bus, false, true),
            OP_GRAY_CLEANUP => self.cleanup(bus, r.data)?,
            OP_GRAY_REVERT => self.revert(bus),
            OP_RESYNC => {
                if r.arg > 255 {
                    return Err(INVALID);
                }
                self.lsb_valid = false;
                self.absolute = false;
                self.direct_pass = false;
                self.force_sync = true;
                self.condition_passes = r.arg as u8;
            }
            OP_SKIP_INITIAL_RESYNC => {
                self.initial_syncs = 0;
                self.red_synced = true;
            }
            OP_SEED_PREVIOUS | OP_GRAY_PREPARE | OP_BACKGROUND | OP_BEGIN_WORK | OP_ABORT_POST
            | OP_QUERY | OP_MAINTENANCE | OP_IDLE | OP_COMPLETE_NEXT | OP_FULL_COMPLETES
            | OP_ACCENT | OP_FAST_CUTOFF | OP_HOLD_FULL | OP_CUSTOM_LUT => {}
            _ => return Err(UNSUPPORTED),
        }
        Ok(result)
    }
}
impl Panel for Uc8253 {
    fn begin(&mut self, bus: &mut impl Bus) {
        bus.reset(50);
        self.red_synced = false;
        self.initial_syncs = 1;
        self.force_sync = false;
        self.condition_passes = 0;
        self.in_gray = false;
        self.lsb_valid = false;
        self.absolute = false;
        self.direct_pass = false;
        self.direct_on_panel = false;
        // Separate cmd/data calls preserve the reset script's CS/DC edges.
        for (command, data) in [
            (0x00, &[0x3f, 0x0a][..]),
            (0x61, &[0x03, 0x18, 0x02, 0x58][..]),
            (0x65, &[0, 0, 0, 0][..]),
            (0x03, &[0x20][..]),
            (0x01, &[0x07, 0x17, 0x3f, 0x3f, 0x17][..]),
            (0x82, &[0x24][..]),
            (0x06, &[0x25, 0x25, 0x3c, 0x37][..]),
            (0x30, &[0x09][..]),
            (0xe1, &[0x02][..]),
        ] {
            bus.cmd(command);
            for value in data {
                bus.data8(*value);
            }
        }
        self.fill(bus, 0x10);
        bus.cmd(0x11);
        self.fill(bus, 0x13);
        bus.cmd(0x11);
        self.screen_on = false;
    }
    fn capabilities(&self, mode: u32) -> u32 {
        let gray = match mode {
            0 => CAP_OVERLAY | CAP_STRIPS,
            1 => CAP_ABSOLUTE | CAP_STRIPS,
            2 if self.direct_supported() => CAP_ABSOLUTE | CAP_COMBINED | CAP_STRIPS,
            _ => 0,
        };
        gray | CAP_ASYNC_DISPLAY
    }
}
