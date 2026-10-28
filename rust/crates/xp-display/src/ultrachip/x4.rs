use super::tags::*;
use super::{fill, wait_start, x3_luts::XTH4, x4_luts::*, Memory, Plane};
use crate::{constants::*, luts::DIRECT, Bus};

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Uc8179,
    Uc8279,
}

/// Register configuration and existing board/build switches; no panel policy.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Config {
    pub psr0: u8,
    pub psr1: u8,
    pub pfs: u8,
    pub pll: u8,
    pub booster: [u8; 4],
    pub gate_scan: u8,
    pub ccset: u8,
    pub temp_full: u8,
    pub temp_fast: u8,
    pub cdi_active: u8,
    pub cdi_idle: u8,
    pub cdi_fast: u8,
    pub power_save: u8,
    pub controller_height: u16,
    pub gate_offset: u16,
    pub psram: u8,
    pub classic: u8,
    pub row_reverse: u8,
    pub mirror: u8,
}
pub struct X4 {
    config: &'static Config,
    width: u16,
    height: u16,
    kind: Kind,
    variant: u8,
    base: Plane,
    base_valid: bool,
    absolute_planes: bool,
    absolute_input: bool,
    direct_pass: bool,
    direct_on_panel: bool,
    direct_configured: bool,
    direct_planes: u8,
    screen_on: bool,
    need_full: bool,
    old_valid: bool,
    synced: bool,
    redrive: bool,
    gray_once: bool,
    gray_image: bool,
    pending: bool,
    pending_off: bool,
    pending_partial: bool,
}

impl X4 {
    pub const fn new(
        width: u16,
        height: u16,
        kind: Kind,
        variant: u8,
        config: &'static Config,
    ) -> Self {
        Self {
            config,
            width,
            height,
            kind,
            variant,
            base: Plane::new(),
            base_valid: false,
            absolute_planes: false,
            absolute_input: false,
            direct_pass: false,
            direct_on_panel: false,
            direct_configured: false,
            direct_planes: 0,
            screen_on: false,
            need_full: true,
            old_valid: false,
            synced: false,
            redrive: false,
            gray_once: false,
            gray_image: false,
            pending: false,
            pending_off: false,
            pending_partial: false,
        }
    }
    fn is8179(&self) -> bool {
        self.kind == Kind::Uc8179
    }
    fn size(&self) -> usize {
        self.width as usize / 8 * self.height as usize
    }
    /// # Safety
    /// Use the allocator that supplied this panel's retained planes.
    pub unsafe fn release(&mut self, memory: &mut impl Memory) {
        self.base.release(memory);
    }
    pub fn capabilities(&self, mode: u32) -> u32 {
        CAP_ASYNC_DISPLAY
            | if !self.is8179() && self.variant == 0x67 {
                0
            } else {
                match mode {
                    0 => CAP_OVERLAY,
                    1 => CAP_ABSOLUTE,
                    2 => CAP_ABSOLUTE | CAP_COMBINED,
                    _ => 0,
                }
            }
    }
    fn bytes(bus: &mut impl Bus, cmd: u8, data: &[u8]) {
        bus.cmd(cmd);
        for b in data {
            bus.data8(*b);
        }
    }
    fn wait(bus: &mut impl Bus, tag: u32) {
        bus.busy_wait(3, tag);
    }
    fn tag(&self, a: u32, b: u32) -> u32 {
        if self.is8179() {
            a
        } else {
            b
        }
    }
    fn on(&mut self, bus: &mut impl Bus, tag: u32) {
        if !self.screen_on {
            bus.cmd(4);
            Self::wait(bus, tag);
            self.screen_on = true;
        }
    }
    fn off(&mut self, bus: &mut impl Bus, tag: u32) {
        bus.cmd(2);
        Self::wait(bus, tag);
        self.screen_on = false;
    }
    fn init(&mut self, bus: &mut impl Bus) {
        let c = self.config;
        Self::bytes(bus, 0, &[c.psr0, c.psr1]);
        Self::bytes(
            bus,
            0x61,
            &[
                (self.width >> 8) as u8,
                self.width as u8,
                (c.controller_height >> 8) as u8,
                c.controller_height as u8,
            ],
        );
        Self::bytes(bus, 0x65, &[0; 4]);
        Self::bytes(bus, 3, &[c.pfs]);
        if self.is8179() {
            Self::bytes(bus, 6, &c.booster);
        } else if c.classic == 0 {
            Self::bytes(bus, 0x30, &[c.pll]);
        }
        Self::bytes(bus, 0xe1, &[c.gate_scan]);
        if self.is8179() && c.power_save != 0 {
            Self::bytes(bus, 0xe3, &[c.power_save]);
        }
        self.screen_on = false;
        self.gray_once = false;
        if self.is8179() {
            self.synced = false;
            self.base_valid = false;
            self.absolute_planes = false;
        }
    }
    pub fn begin(&mut self, bus: &mut impl Bus, memory: &mut impl Memory) {
        self.direct_on_panel = false;
        self.direct_pass = false;
        self.need_full = true;
        self.old_valid = false;
        self.redrive = false;
        let n = self.size();
        if self.is8179() {
            self.direct_configured = false;
            self.direct_planes = 0;
            if self.config.psram != 0 {
                self.base.ensure(memory, n, true);
            }
        } else {
            self.gray_image = false;
            self.absolute_input = false;
        }
        bus.reset(50);
        self.init(bus);
        if !self.is8179() {
            self.base.ensure(memory, n, false);
            self.base_valid = false;
            self.absolute_planes = false;
        }
    }
    fn stream(&self, bus: &mut impl Bus, cmd: u8, data: &[u8], xor: Option<&[u8]>, invert: bool) {
        let mut row = [0xff; 128];
        let stride = self.width as usize / 8;
        let wb = stride.min(row.len());
        bus.cmd(cmd);
        bus.txn_begin();
        for _ in 0..self.config.gate_offset {
            bus.txn_write(&row[..wb]);
        }
        for n in 0..self.height as usize {
            let y = if self.config.row_reverse != 0 {
                self.height as usize - 1 - n
            } else {
                n
            };
            let src = &data[y * stride..(y + 1) * stride];
            if self.is8179() && xor.is_none() && !invert {
                bus.txn_write(src);
                continue;
            }
            for (i, dst) in row[..wb].iter_mut().enumerate() {
                let x = if self.config.mirror != 0 {
                    wb - 1 - i
                } else {
                    i
                };
                let mut b = src[x] ^ xor.map_or(0, |rhs| rhs[y * stride + x]);
                if self.config.mirror != 0 {
                    b = b.reverse_bits();
                }
                *dst = if invert { !b } else { b };
            }
            bus.txn_write(&row[..wb]);
        }
        row.fill(0xff);
        for _ in self.config.gate_offset + self.height..self.config.controller_height {
            bus.txn_write(&row[..wb]);
        }
        bus.txn_end();
    }
    fn stream_base(&self, bus: &mut impl Bus, cmd: u8, xor: Option<&[u8]>, invert: bool) {
        if let Some(data) = self.base.bytes(self.size()) {
            self.stream(bus, cmd, data, xor, invert);
        }
    }
    fn snapshot(&mut self, data: &[u8]) {
        self.base_valid = false;
        self.absolute_planes = false;
        let n = self.size();
        if let Some(base) = self.base.bytes_mut(n) {
            base.copy_from_slice(data);
            self.base_valid = true;
        }
    }
    fn restore_bw(&mut self, bus: &mut impl Bus) {
        if !self.direct_configured {
            return;
        }
        if self.screen_on {
            bus.cmd(2);
            Self::wait(bus, TAG_UC8179_DIRECT_EXIT_POF);
        }
        bus.reset(50);
        self.init(bus);
        self.direct_configured = false;
        self.need_full = true;
        self.old_valid = false;
        self.redrive = false;
    }
    fn direct_setup(&mut self, bus: &mut impl Bus) {
        if self.direct_configured {
            return;
        }
        if self.screen_on {
            bus.cmd(2);
            Self::wait(bus, TAG_UC8179_DIRECT_SETUP_POF);
        }
        bus.reset(50);
        self.init(bus);
        let c = &DIRECT_CONFIG[0];
        Self::bytes(bus, 0x52, &[(c[0] & 8) | (c[1] >> 6)]);
        Self::bytes(bus, 0x30, &[c[0] >> 4]);
        Self::bytes(
            bus,
            1,
            &[0x17, c[0] & 7, c[1] & 0x3f, c[2] & 0x3f, c[3] & 0x3f],
        );
        Self::bytes(bus, 0x2a, &[c[2] & 0xc0, c[4]]);
        Self::bytes(bus, 0x82, &[c[5]]);
        bus.cmd_data(0x50, &[self.config.cdi_active, 7]);
        self.direct_configured = true;
    }
    pub fn display(&mut self, bus: &mut impl Bus, data: &[u8], mode: u32, off: bool) {
        if mode == 2 && self.redrive && self.gray_once && self.old_valid && !self.need_full {
            self.transition(bus, data, off);
            return;
        }
        self.start(bus, data, mode, off);
        self.finish(bus, data);
    }
    pub fn start(&mut self, bus: &mut impl Bus, data: &[u8], mode: u32, off: bool) {
        let paint = self.direct_on_panel;
        self.direct_on_panel = false;
        if self.is8179() {
            self.restore_bw(bus);
            self.direct_planes = 0;
            self.synced = false;
        } else {
            self.gray_image = false;
        }
        self.absolute_input = false;
        self.direct_pass = false;
        self.snapshot(data);
        let fast = mode == 2 && !self.need_full && self.old_valid;
        if paint {
            self.stream(bus, 0x10, data, None, true);
            self.stream(bus, 0x13, data, None, false);
            self.bw_refresh(bus, true);
            bus.refresh_wait(self.tag(TAG_UC8179_BW_TARGET_DRF, TAG_UC8279_X4_BW_TARGET_DRF));
            bus.cmd(0x92);
        }
        self.stream(bus, 0x13, data, None, false);
        if !fast {
            if mode == 1 {
                self.stream(bus, 0x10, data, None, true);
            } else {
                fill(bus, 0x10, self.config.controller_height, self.width / 8);
            }
        } else if !self.is8179() && self.redrive {
            self.stream(bus, 0x10, data, None, true);
        }
        self.redrive = false;
        self.bw_refresh(bus, fast);
        self.pending_partial = fast;
        self.pending_off = off;
        self.pending = true;
    }
    fn window(&self, bus: &mut impl Bus, span: bool) {
        let xe = self.width - 1;
        let ys = self.config.gate_offset;
        let ye = ys + self.height - 1;
        let data = [
            0,
            0,
            (xe >> 8) as u8,
            (xe | 7) as u8,
            (ys >> 8) as u8,
            ys as u8,
            (ye >> 8) as u8,
            ye as u8,
            1,
        ];
        bus.cmd(0x91);
        if span {
            bus.cmd_data(0x90, &data);
        } else {
            Self::bytes(bus, 0x90, &data);
        }
    }
    fn bw_refresh(&mut self, bus: &mut impl Bus, fast: bool) {
        let c = self.config;
        if self.is8179() {
            Self::bytes(bus, 0x50, &[c.cdi_active, 7]);
        } else {
            Self::bytes(bus, 0x50, &[if fast { c.cdi_fast } else { c.cdi_idle }]);
        }
        Self::bytes(bus, 0xe0, &[c.ccset]);
        Self::bytes(bus, 0xe5, &[if fast { c.temp_fast } else { c.temp_full }]);
        if self.is8179() {
            Self::bytes(bus, 0, &[c.psr0 & 0xdf, c.psr1]);
        }
        if fast {
            Self::bytes(bus, 3, &[c.pfs]);
            Self::bytes(bus, 0xe1, &[c.gate_scan]);
        }
        self.on(bus, self.tag(TAG_UC8179_PON, TAG_UC8279_X4_PON));
        if fast {
            if self.is8179() {
                bus.cmd(0x91);
            } else {
                self.window(bus, false);
            }
        }
        if !self.is8179() {
            Self::bytes(bus, 0, &[c.psr0 & 0xdf, c.psr1]);
        }
        bus.cmd(0x12);
        wait_start(bus);
    }
    pub fn finish(&mut self, bus: &mut impl Bus, data: &[u8]) {
        if !self.pending {
            return;
        }
        self.pending = false;
        bus.refresh_wait(self.tag(TAG_UC8179_DRF, TAG_UC8279_X4_DRF));
        if self.pending_partial {
            bus.cmd(0x92);
        }
        if self.is8179() {
            Self::bytes(bus, 0x50, &[self.config.cdi_idle, 7]);
        }
        self.stream(bus, 0x10, data, None, false);
        self.old_valid = true;
        self.synced = true;
        self.need_full = false;
        if self.pending_off {
            self.off(bus, self.tag(TAG_UC8179_POF, TAG_UC8279_X4_POF));
        }
    }
    pub fn resync(&mut self) {
        self.gray_image = false;
        self.absolute_input = false;
        self.direct_pass = false;
        self.direct_planes = 0;
        self.need_full = true;
    }
    pub fn skip_initial(&mut self) {
        self.need_full = false;
    }
    pub fn sleep(&mut self, bus: &mut impl Bus) {
        self.direct_on_panel = false;
        self.absolute_input = false;
        self.direct_pass = false;
        if self.is8179() {
            self.direct_planes = 0;
            self.base_valid = false;
            self.absolute_planes = false;
        } else {
            self.gray_image = false;
        }
        if self.screen_on {
            self.off(
                bus,
                self.tag(TAG_UC8179_POWER_DOWN, TAG_UC8279_X4_POWER_DOWN),
            );
        }
        Self::bytes(bus, 7, &[0xa5]);
    }
    pub fn gray_base(&mut self, bus: &mut impl Bus, data: Option<&[u8]>, mode: u32, off: bool) {
        let Some(data) = data else {
            return;
        };
        if mode != 2 || !self.gray_once || !self.old_valid || self.need_full {
            self.display(bus, data, mode, off);
        } else {
            self.transition(bus, data, off);
        }
    }
    fn transition(&mut self, bus: &mut impl Bus, data: &[u8], off: bool) {
        self.snapshot(data);
        Self::wait(
            bus,
            self.tag(TAG_UC8179_GRAY_BASE_READY, TAG_UC8279_X4_GRAY_BASE_READY),
        );
        self.stream(bus, 0x13, data, None, false);
        self.synced = false;
        self.precondition(bus);
        self.stream(bus, 0x10, data, None, false);
        self.old_valid = true;
        self.synced = true;
        self.redrive = false;
        self.need_full = false;
        if off && self.screen_on {
            self.off(
                bus,
                self.tag(TAG_UC8179_GRAY_BASE_POF, TAG_UC8279_X4_GRAY_BASE_POF),
            );
        }
    }
    fn precondition(&mut self, bus: &mut impl Bus) {
        if !self.old_valid || !self.gray_once {
            return;
        }
        Self::wait(
            bus,
            self.tag(TAG_UC8179_GRAY_PRE_READY, TAG_UC8279_X4_GRAY_PRE_READY),
        );
        self.window(bus, self.is8179());
        let c = self.config;
        Self::bytes(bus, 0, &[c.psr0, c.psr1]);
        Self::bytes(bus, 3, &[c.pfs]);
        Self::bytes(bus, 0xe1, &[c.gate_scan]);
        if self.is8179() {
            Self::bytes(bus, 0x50, &[c.cdi_active, 7]);
        } else {
            Self::bytes(bus, 0x50, &[c.cdi_fast]);
        }
        Self::bytes(bus, 0xe0, &[c.ccset]);
        Self::bytes(bus, 0xe5, &[c.temp_fast]);
        for row in if self.is8179() { &PRE8179 } else { &PRE8279 } {
            bus.cmd(row[0]);
            bus.data_span(&row[1..]);
        }
        self.on(
            bus,
            self.tag(TAG_UC8179_GRAY_PRE_PON, TAG_UC8279_X4_GRAY_PRE_PON),
        );
        bus.cmd(0x12);
        Self::wait(
            bus,
            self.tag(TAG_UC8179_GRAY_PRE_DRF, TAG_UC8279_X4_GRAY_PRE_DRF),
        );
        bus.cmd(0x92);
        if self.is8179() {
            Self::bytes(bus, 0x50, &[c.cdi_idle, 7]);
        }
    }
    pub fn gray_begin(
        &mut self,
        bus: &mut impl Bus,
        data: Option<&[u8]>,
        mode: u32,
        fallback: u32,
        off: bool,
    ) {
        if !self.is8179() {
            self.gray_image = false;
            self.absolute_input = false;
            self.direct_pass = false;
        }
        if mode == 2 {
            if self.is8179() {
                self.direct_setup(bus);
            }
            self.absolute_input = true;
            self.direct_pass = true;
            if self.is8179() {
                for (i, row) in DIRECT.iter().enumerate() {
                    bus.cmd(0x20 + i as u8);
                    bus.data_span(row);
                }
            }
            self.direct_planes = 0;
            self.base_valid = false;
            self.absolute_planes = false;
            self.old_valid = false;
            self.synced = false;
            self.need_full = true;
            self.redrive = false;
            return;
        }
        self.absolute_input = false;
        self.direct_pass = false;
        self.direct_planes = 0;
        self.gray_base(bus, data, fallback, off);
        self.absolute_input = mode == 1;
    }
    pub fn lsb(&mut self, bus: &mut impl Bus, data: Option<&[u8]>) {
        let Some(data) = data else {
            return;
        };
        if self.absolute_input {
            Self::wait(bus, TAG_ABSOLUTE_PLANE);
            self.stream(bus, 0x10, data, None, !self.is8179());
            if self.direct_pass {
                self.direct_planes |= 1;
            }
            self.synced = false;
            return;
        }
        if self.is8179() {
            Self::wait(bus, TAG_UC8179_GRAY_LSB);
        }
        self.absolute_planes = false;
        if self.base_valid {
            let n = self.size();
            if let Some(base) = self.base.bytes_mut(n) {
                for (b, d) in base.iter_mut().zip(data) {
                    *b |= d;
                }
            }
            self.stream_base(bus, 0x10, None, !self.is8179());
            self.absolute_planes = true;
        } else {
            self.stream(bus, 0x10, data, None, !self.is8179());
        }
        self.base_valid = false;
        self.synced = false;
    }
    pub fn msb(&mut self, bus: &mut impl Bus, data: Option<&[u8]>) {
        let Some(data) = data else {
            return;
        };
        if self.absolute_input {
            Self::wait(bus, TAG_ABSOLUTE_PLANE);
            self.stream(bus, 0x13, data, None, !self.is8179());
            if self.direct_pass {
                self.direct_planes |= 2;
            }
            self.synced = false;
            return;
        }
        if self.is8179() {
            Self::wait(bus, TAG_UC8179_GRAY_MSB);
        } else {
            let gray: u32 = data.iter().map(|b| b.count_ones()).sum();
            self.gray_image = gray * 100 > self.size() as u32 * 8 * QUALITY_COVERAGE;
        }
        if self.absolute_planes {
            self.stream_base(bus, 0x13, Some(data), !self.is8179());
            let n = self.size();
            if let Some(base) = self.base.bytes_mut(n) {
                for (b, d) in base.iter_mut().zip(data) {
                    *b &= *b ^ d;
                }
            }
            self.base_valid = true;
        } else {
            self.stream(bus, 0x13, data, None, !self.is8179());
        }
    }
    pub fn gray(&mut self, bus: &mut impl Bus, off: bool, factory: bool) {
        if self.is8179() {
            self.gray8179(bus, off, factory);
        } else {
            self.gray8279(bus, off, factory);
        }
    }
    fn gray8179(&mut self, bus: &mut impl Bus, off: bool, factory: bool) {
        if self.direct_pass {
            if self.direct_planes != 3 {
                return;
            }
            Self::wait(bus, TAG_UC8179_DIRECT_READY);
            self.on(bus, TAG_UC8179_DIRECT_PON);
            bus.cmd(0x12);
            Self::wait(bus, TAG_UC8179_DIRECT_GRAY_DRF);
            self.direct_on_panel = true;
            self.direct_pass = false;
            self.direct_planes = 0;
            self.absolute_input = false;
            self.need_full = true;
            self.old_valid = false;
            self.redrive = false;
            if off {
                self.off(bus, TAG_UC8179_DIRECT_POF);
            }
            return;
        }
        Self::wait(bus, TAG_UC8179_GRAY_READY);
        self.synced = false;
        Self::bytes(bus, 0, &[self.config.psr0, self.config.psr1]);
        for (i, row) in AA8179.iter().enumerate() {
            bus.cmd(0x20 + i as u8);
            bus.data_span(if i == 3 { &DARK8179[0] } else { row });
        }
        Self::bytes(bus, 0x50, &[self.config.cdi_active, 7]);
        self.gray_once = true;
        let slower = factory && self.absolute_input;
        if slower {
            Self::bytes(bus, 0x30, &[5]);
        }
        self.on(bus, TAG_UC8179_GRAY_PON);
        bus.cmd(0x12);
        Self::wait(bus, TAG_UC8179_GRAY_SPLIT_DRF);
        if slower {
            Self::bytes(bus, 0x30, &[6]);
        }
        if self.base_valid {
            self.stream_base(bus, 0x10, None, false);
            self.stream_base(bus, 0x13, None, false);
            self.old_valid = true;
            self.synced = true;
            self.need_full = false;
        }
        self.base_valid = false;
        self.absolute_planes = false;
        self.redrive = true;
        self.absolute_input = false;
        self.direct_pass = false;
        self.direct_planes = 0;
        if factory && off && self.screen_on {
            self.off(bus, TAG_UC8179_ABSOLUTE_POF);
        }
    }
    fn gray8279(&mut self, bus: &mut impl Bus, off: bool, factory: bool) {
        Self::bytes(bus, 0, &[self.config.psr0, self.config.psr1]);
        if self.direct_pass || self.absolute_input || factory || self.gray_image {
            for (reg, row) in [0x20, 0x21, 0x23, 0x22, 0x24].iter().zip(QUALITY.iter()) {
                bus.cmd(*reg);
                bus.data_span(row);
            }
        } else {
            for (i, row) in (if self.variant == 2 || self.variant == 3 {
                &AA02
            } else {
                &AA68
            })
            .iter()
            .enumerate()
            {
                bus.cmd(0x20 + i as u8);
                bus.data_span(row);
            }
        }
        self.gray_image = false;
        Self::bytes(bus, 0x50, &[self.config.cdi_active]);
        self.gray_once = true;
        self.on(bus, TAG_UC8279_X4_GRAY_PON);
        Self::bytes(bus, 0, &[self.config.psr0, self.config.psr1]);
        bus.cmd(0x12);
        Self::wait(
            bus,
            if self.direct_pass {
                TAG_UC8279_X4_DIRECT_GRAY_DRF
            } else {
                TAG_UC8279_X4_GRAY
            },
        );
        if off {
            self.off(bus, TAG_UC8279_X4_GRAY_POF);
        }
        if self.base_valid {
            self.stream_base(bus, 0x10, None, false);
            self.stream_base(bus, 0x13, None, false);
            self.old_valid = true;
            self.need_full = false;
        }
        self.base_valid = false;
        self.absolute_planes = false;
        self.direct_on_panel = self.direct_pass;
        self.redrive = !self.direct_pass;
        if self.direct_pass {
            self.need_full = true;
        }
        self.gray_image = false;
        self.absolute_input = false;
        self.direct_pass = false;
    }
    pub fn cleanup(&mut self, bus: &mut impl Bus, data: Option<&[u8]>) {
        if self.is8179() {
            Self::wait(bus, TAG_UC8179_GRAY_CLEANUP);
        }
        self.gray_image = false;
        self.absolute_input = false;
        self.direct_pass = false;
        self.direct_planes = 0;
        self.base_valid = false;
        self.absolute_planes = false;
        if self.is8179() && self.direct_configured {
            self.need_full = true;
            self.old_valid = false;
            self.synced = false;
            return;
        }
        let Some(data) = data else {
            self.need_full = true;
            self.old_valid = false;
            return;
        };
        if self.is8179() && self.synced && self.old_valid {
            return;
        }
        self.stream(bus, 0x10, data, None, false);
        if self.is8179() {
            self.stream(bus, 0x13, data, None, false);
            self.synced = true;
        }
        self.old_valid = true;
    }
}

const fn tuning(value: Option<&str>, default: u32) -> u32 {
    let Some(value) = value else {
        return default;
    };
    let bytes = value.as_bytes();
    assert!(!bytes.is_empty(), "empty UltraChip build setting");
    let mut result = 0u32;
    let mut i = 0;
    while i < bytes.len() {
        assert!(
            bytes[i] >= b'0' && bytes[i] <= b'9',
            "UltraChip build setting must be decimal"
        );
        result = result * 10 + (bytes[i] - b'0') as u32;
        i += 1;
    }
    result
}
const GRAY_SPEED: u32 = tuning(option_env!("FREEINK_UC8279X4_GRAY_SPEED"), 60);
const QUALITY_COVERAGE: u32 = tuning(option_env!("FREEINK_UC8279X4_QUALITY_COVERAGE_PCT"), 25);

// SDK makeQualityBank(): restore DC balance and equalize duration.
// The waveform source is the stock X3 XTH4 bank; all work is compile-time.
const fn quality_bank() -> [[u8; 49]; 5] {
    let mut phases = [[[[0u8; 2]; 4]; 7]; 5];
    let mut used = [0usize; 5];
    let mut t = 0;
    while t < 5 {
        let mut g = 0;
        while g < 7 {
            let mut empty = true;
            let mut i = 0;
            while i < 4 {
                if XTH4[t][g * 7 + 1 + i] != 0 {
                    empty = false;
                }
                i += 1;
            }
            if empty {
                break;
            }
            i = 0;
            while i < 4 {
                let b = XTH4[t][g * 7 + 1 + i];
                let frames = b & 63;
                let mut scaled = (frames as u32 * GRAY_SPEED + 50) / 100;
                if frames != 0 && scaled == 0 {
                    scaled = 1;
                }
                if scaled > 63 {
                    scaled = 63;
                }
                phases[t][g][i] = [b >> 6, scaled as u8];
                i += 1;
            }
            g += 1;
        }
        used[t] = g;
        loop {
            let mut net = 0i32;
            g = 0;
            while g < used[t] {
                let mut i = 0;
                while i < 4 {
                    let p = phases[t][g][i];
                    if p[0] == 1 {
                        net += p[1] as i32;
                    } else if p[0] == 2 {
                        net -= p[1] as i32;
                    }
                    i += 1;
                }
                g += 1;
            }
            if net == 0 {
                break;
            }
            let want = if net > 0 { 2 } else { 1 };
            let mut bg = 7;
            let mut bi = 0;
            let mut best = 0;
            g = 0;
            while g < used[t] {
                let mut i = 0;
                while i < 4 {
                    let p = phases[t][g][i];
                    if p[0] == want && p[1] > best && p[1] < 63 {
                        best = p[1];
                        bg = g;
                        bi = i;
                    }
                    i += 1;
                }
                g += 1;
            }
            if bg == 7 {
                break;
            }
            phases[t][bg][bi][1] += 1;
        }
        t += 1;
    }
    let mut totals = [0u16; 5];
    let mut longest = 0;
    t = 0;
    while t < 5 {
        let mut g = 0;
        while g < used[t] {
            let mut i = 0;
            while i < 4 {
                totals[t] += phases[t][g][i][1] as u16;
                i += 1;
            }
            g += 1;
        }
        if totals[t] > longest {
            longest = totals[t];
        }
        t += 1;
    }
    let mut out = [[0u8; 49]; 5];
    t = 0;
    while t < 5 {
        let mut deficit = longest - totals[t];
        while deficit > 0 && used[t] < 6 {
            let chunk = if deficit > 63 { 63 } else { deficit };
            phases[t][used[t]][0] = [0, chunk as u8];
            used[t] += 1;
            deficit -= chunk;
        }
        let mut g = 0;
        while g < used[t] {
            out[t][g * 7] = 1;
            let mut i = 0;
            while i < 4 {
                let p = phases[t][g][i];
                out[t][g * 7 + 1 + i] = (p[0] << 6) | (p[1] & 63);
                i += 1;
            }
            out[t][g * 7 + 5] = 1;
            out[t][g * 7 + 6] = 1;
            g += 1;
        }
        t += 1;
    }
    out
}
static QUALITY: [[u8; 49]; 5] = quality_bank();
