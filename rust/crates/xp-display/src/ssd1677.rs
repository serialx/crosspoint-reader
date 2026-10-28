//! SSD1677 controller policy, ported from FreeInk SDK 08effe64 Ssd1677Driver.
//! Framebuffers are borrowed for each call; controller RED RAM owns the baseline.
use crate::{constants::*, ssd1677_luts::*, Bus};

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Refresh {
    Full,
    Half,
    Fast,
}

// Allocations stay on the SDK heap; each returned owner must free its own block.
pub trait WindowAllocator {
    type Buffer: AsRef<[u8]> + AsMut<[u8]>;
    fn allocate(&self, size: usize) -> Option<Self::Buffer>;
}

#[derive(Debug)]
pub struct WindowAllocationFailed {
    pub bytes: usize,
}

#[derive(Clone, Copy)]
pub struct Window {
    pub x: u16,
    pub y: u16,
    pub width: u16,
    pub height: u16,
}

struct Config {
    scan: u8,
    border_init: u8,
    half_temp: u8,
    gray: &'static [u8],
    factory: &'static [u8],
    full_seq: u8,
    fast_seq: u8,
    half_seq: u8,
    border_full: u8,
    border_fast: u8,
    border_half: u8,
    border_gray: u8,
    gray_power_first: bool,
    second_control: bool,
    restore_temp: bool,
    black_pulse: bool,
    fast_lut: Option<&'static [u8]>,
    full_lut: Option<&'static [u8]>,
    half_lut: Option<&'static [u8]>,
}
const X4: Config = Config {
    scan: 2,
    border_init: 0x80,
    half_temp: 0x5a,
    gray: &LUT_GRAYSCALE,
    factory: &LUT_FACTORY_QUALITY,
    full_seq: 0xf7,
    fast_seq: 0xfc,
    half_seq: 0xd7,
    border_full: 0xc0,
    border_fast: 0xc0,
    border_half: 0xc0,
    border_gray: 0xc0,
    gray_power_first: false,
    second_control: false,
    restore_temp: false,
    black_pulse: false,
    fast_lut: None,
    full_lut: None,
    half_lut: None,
};
static STICKY: Config = Config {
    border_init: 1,
    gray: &LUT_GRAYSCALE_STICKY,
    fast_seq: 0xff,
    half_seq: 0,
    border_full: 1,
    border_fast: 0x80,
    border_half: 0,
    border_gray: 0x80,
    gray_power_first: true,
    ..X4
};
static METALIO: Config = Config {
    border_init: 1,
    half_temp: 0x6a,
    gray: &LUT_GRAYSCALE_METALIO,
    factory: &LUT_FACTORY_QUALITY_METALIO,
    border_full: 1,
    border_fast: 0x80,
    border_half: 1,
    second_control: true,
    restore_temp: true,
    black_pulse: true,
    fast_lut: Some(&LUT_FAST_METALIO),
    ..X4
};
const PICCO: Config = Config {
    border_init: 1,
    gray: &LUT_GRAYSCALE_STICKY,
    fast_seq: 0xdc,
    border_full: 0,
    border_fast: 0,
    border_half: 0,
    border_gray: 0x80,
    gray_power_first: true,
    fast_lut: Some(&LUT_PICCO_FAST),
    ..X4
};
static YRD: Config = PICCO;
static DEPG: Config = Config {
    fast_lut: Some(&LUT_PICCO_DEPG_FAST),
    full_lut: Some(&LUT_PICCO_DEPG_FULL),
    half_lut: Some(&LUT_PICCO_DEPG_FULL),
    ..PICCO
};
static OPM: Config = Config {
    border_init: 3,
    ..PICCO
};

// Only scalar state is retained. The static config and LUTs live in flash.
pub struct Ssd1677 {
    cfg: &'static Config,
    pub width: u16,
    pub height: u16,
    mirror_x: bool,
    mirror_y: bool,
    fast_du: bool,
    pending_sync: bool,
    needs_gray_clear: bool,
    absolute_input: bool,
    screen_on: bool,
    in_gray: bool,
    custom_lut: bool,
    pending_off: bool,
    initial_full: bool,
}
impl Ssd1677 {
    pub fn new(width: u16, height: u16, variant: u32, flags: u32) -> Option<Self> {
        if width == 0
            || !width.is_multiple_of(8)
            || height == 0
            || u32::from(width / 8) * u32::from(height) > u16::MAX as u32
            || flags & !7 != 0
        {
            return None;
        }
        let cfg = match variant {
            SSD1677_X4 => &X4,
            SSD1677_STICKY => &STICKY,
            SSD1677_METALIO => &METALIO,
            SSD1677_PICCO_YRD => &YRD,
            SSD1677_PICCO_DEPG => &DEPG,
            SSD1677_PICCO_OPM => &OPM,
            _ => return None,
        };
        Some(Self {
            cfg,
            width,
            height,
            mirror_x: flags & SSD1677_MIRROR_X != 0,
            mirror_y: flags & SSD1677_MIRROR_Y != 0,
            fast_du: flags & SSD1677_FAST_DU != 0,
            pending_sync: false,
            needs_gray_clear: false,
            absolute_input: false,
            screen_on: false,
            in_gray: false,
            custom_lut: false,
            pending_off: false,
            initial_full: false,
        })
    }
    pub fn frame_size(&self) -> usize {
        usize::from(self.width / 8) * usize::from(self.height)
    }
    pub fn capabilities(&self, mode: u32) -> u32 {
        CAP_ASYNC_DISPLAY
            | match mode {
                0 => CAP_OVERLAY | CAP_STRIPS | CAP_ASYNC_BASE,
                1 => CAP_ABSOLUTE | CAP_COMBINED | CAP_STRIPS,
                _ => 0,
            }
    }
    pub fn begin(&mut self, bus: &mut impl Bus) {
        bus.reset(0);
        bus.cmd(0x12);
        bus.delay_ms(10);
        bus.busy_wait(0, TAG_SSD_SOFT_RESET);
        Self::reg(bus, 0x18, 0x80);
        bus.cmd(0x0c);
        for b in [0xae, 0xc7, 0xc3, 0xc0, 0x80] {
            bus.data8(b);
        }
        bus.cmd(0x01);
        Self::word(bus, self.height - 1);
        bus.data8(self.cfg.scan | u8::from(self.mirror_y));
        Self::reg(bus, 0x3c, self.cfg.border_init);
        self.area(bus, 0, 0, self.width, self.height);
        Self::reg(bus, 0x46, 0xf7);
        bus.busy_wait(0, TAG_SSD_AUTO_BW);
        Self::reg(bus, 0x47, 0xf7);
        bus.busy_wait(0, TAG_SSD_AUTO_RED);
        self.screen_on = false;
        self.initial_full = self.cfg.full_seq != 0;
    }
    fn reg(bus: &mut impl Bus, command: u8, value: u8) {
        bus.cmd(command);
        bus.data8(value);
    }
    fn word(bus: &mut impl Bus, v: u16) {
        bus.data8(v as u8);
        bus.data8((v >> 8) as u8);
    }
    fn area(&self, bus: &mut impl Bus, x: u16, y: u16, width: u16, height: u16) {
        let hi = x.wrapping_add(width).wrapping_sub(1);
        let (start, end) = if self.mirror_x { (hi, x) } else { (x, hi) };
        let y = self.height.wrapping_sub(y).wrapping_sub(height);
        let top = y.wrapping_add(height).wrapping_sub(1);
        Self::reg(bus, 0x11, u8::from(!self.mirror_x));
        bus.cmd(0x44);
        Self::word(bus, start);
        Self::word(bus, end);
        bus.cmd(0x45);
        Self::word(bus, top);
        Self::word(bus, y);
        bus.cmd(0x4e);
        Self::word(bus, start);
        bus.cmd(0x4f);
        Self::word(bus, top);
    }
    fn write(bus: &mut impl Bus, command: u8, data: &[u8]) {
        bus.cmd(command);
        bus.data_span(data);
    }
    fn fill(&self, bus: &mut impl Bus, command: u8, byte: u8) {
        let chunk = [byte; 128];
        bus.cmd(command);
        bus.txn_begin();
        for _ in 0..self.height {
            let mut remaining = usize::from(self.width / 8);
            while remaining != 0 {
                let n = remaining.min(chunk.len());
                bus.txn_write(&chunk[..n]);
                remaining -= n;
            }
        }
        bus.txn_end();
    }
    pub fn set_lut(&mut self, bus: &mut impl Bus, data: Option<&[u8]>) {
        let Some(data) = data else {
            self.custom_lut = false;
            return;
        };
        if data.len() < 110 {
            return;
        }
        bus.cmd(0x32);
        for b in &data[..105] {
            bus.data8(*b);
        }
        Self::reg(bus, 0x03, data[105]);
        bus.cmd(0x04);
        for b in &data[106..109] {
            bus.data8(*b);
        }
        Self::reg(bus, 0x2c, data[109]);
        if self.cfg.border_gray != 0 {
            Self::reg(bus, 0x3c, self.cfg.border_gray);
        }
        self.custom_lut = true;
    }
    fn refresh(&mut self, bus: &mut impl Bus, mode: Refresh, turn_off: bool, deferred: bool) {
        self.pending_off = false;
        Self::reg(bus, 0x21, if mode == Refresh::Fast { 0 } else { 0x40 });
        if self.cfg.second_control {
            bus.data8(0);
        }
        if self.cfg.restore_temp && mode != Refresh::Half {
            Self::reg(bus, 0x18, 0x80);
        }
        let lut = match mode {
            Refresh::Fast => self.cfg.fast_lut,
            Refresh::Half => self.cfg.half_lut,
            Refresh::Full => self.cfg.full_lut,
        };
        let board_lut = lut.is_some() && !self.custom_lut;
        if board_lut {
            self.set_lut(bus, lut);
            if mode == Refresh::Fast && self.cfg.border_fast != 0 {
                Self::reg(bus, 0x3c, self.cfg.border_fast);
            }
        }
        let seq = match mode {
            Refresh::Fast if self.fast_du => 0,
            Refresh::Fast => self.cfg.fast_seq,
            Refresh::Half if self.cfg.half_seq != 0 => self.cfg.half_seq,
            _ => self.cfg.full_seq,
        };
        if seq != 0 && !self.custom_lut {
            let border = match mode {
                Refresh::Fast => self.cfg.border_fast,
                Refresh::Half if self.cfg.border_half != 0 => self.cfg.border_half,
                _ => self.cfg.border_full,
            };
            if border != 0 {
                Self::reg(bus, 0x3c, border);
            }
            if mode == Refresh::Half {
                Self::reg(bus, 0x1a, self.cfg.half_temp);
            }
            Self::reg(bus, 0x22, seq);
            bus.cmd(0x20);
            if !deferred {
                bus.refresh_wait(63);
            }
            self.screen_on = seq & 3 == 0;
            self.pending_off = turn_off && self.screen_on;
            return;
        }
        if board_lut && mode != Refresh::Fast {
            let mut control = 0xe4;
            if mode == Refresh::Half {
                Self::reg(bus, 0x1a, self.cfg.half_temp);
                control = 0xc4;
            }
            if turn_off {
                control |= 3;
            }
            self.screen_on = !turn_off;
            Self::reg(bus, 0x22, control);
            bus.cmd(0x20);
            if !deferred {
                bus.refresh_wait(63);
            }
            self.set_lut(bus, None);
            return;
        }
        let mut control = if self.screen_on { 0 } else { 0xc0 };
        self.screen_on = !turn_off;
        if turn_off {
            control |= 3;
        }
        match mode {
            Refresh::Full => control |= 0x34,
            Refresh::Half => {
                Self::reg(bus, 0x1a, self.cfg.half_temp);
                control |= 0xd4;
            }
            Refresh::Fast if self.custom_lut => control = 0xcc | if turn_off { 3 } else { 0 },
            Refresh::Fast => control |= 0x1c,
        }
        Self::reg(bus, 0x22, control);
        bus.cmd(0x20);
        if !deferred {
            bus.refresh_wait(63);
        }
        if board_lut {
            self.set_lut(bus, None);
        }
    }
    fn power_on(&mut self, bus: &mut impl Bus) {
        if self.screen_on {
            return;
        }
        Self::reg(bus, 0x22, 0xc0);
        bus.cmd(0x20);
        bus.busy_wait(0, TAG_SSD_GRAY_POWER_ON);
        self.screen_on = true;
    }
    fn power_off(&mut self, bus: &mut impl Bus) {
        if !self.screen_on {
            return;
        }
        Self::reg(bus, 0x3c, self.cfg.border_init);
        Self::reg(bus, 0x22, 3);
        bus.cmd(0x20);
        bus.delay_ms(200);
        bus.busy_wait(0, TAG_SSD_POWER_DOWN);
        self.screen_on = false;
    }
    pub fn sleep(&mut self, bus: &mut impl Bus) {
        self.power_off(bus);
        Self::reg(bus, 0x10, 3);
    }
    fn clean_mode(&self) -> Refresh {
        if self.cfg.half_seq != 0 {
            Refresh::Half
        } else {
            Refresh::Full
        }
    }
    pub fn display(
        &mut self,
        bus: &mut impl Bus,
        frame: &[u8],
        previous: Option<&[u8]>,
        mut mode: Refresh,
        turn_off: bool,
        deferred: bool,
    ) {
        let cold = self.initial_full;
        self.pending_sync = self.cfg.black_pulse && deferred;
        if self.needs_gray_clear {
            if mode == Refresh::Fast {
                mode = self.clean_mode();
            }
            self.needs_gray_clear = false;
        }
        if self.initial_full {
            if mode == Refresh::Fast {
                mode = self.clean_mode();
            }
            self.initial_full = false;
        } else if !turn_off && !self.screen_on && self.cfg.full_seq == 0 {
            mode = Refresh::Half;
        }
        if self.in_gray {
            self.in_gray = false;
            if mode == Refresh::Fast {
                mode = Refresh::Half;
            }
        }
        self.area(bus, 0, 0, self.width, self.height);
        if self.cfg.black_pulse && mode == Refresh::Half {
            if cold {
                self.fill(bus, 0x26, 0xff);
            } else if let Some(previous) = previous {
                Self::write(bus, 0x26, previous);
            }
            self.fill(bus, 0x24, 0);
            self.refresh(bus, Refresh::Fast, false, false);
            self.area(bus, 0, 0, self.width, self.height);
            self.fill(bus, 0x24, 0);
            self.fill(bus, 0x26, 0);
            Self::write(bus, 0x24, frame);
            self.refresh(bus, Refresh::Fast, turn_off, deferred);
        } else {
            Self::write(bus, 0x24, frame);
            if mode != Refresh::Fast {
                Self::write(bus, 0x26, frame);
            } else if let Some(previous) = previous {
                Self::write(bus, 0x26, previous);
            }
            self.refresh(bus, mode, turn_off, deferred);
        }
        if (previous.is_none() || self.cfg.black_pulse) && !deferred {
            self.sync(bus, frame);
        }
        if !deferred {
            self.finish_power_off(bus);
        }
    }
    fn sync(&self, bus: &mut impl Bus, frame: &[u8]) {
        self.area(bus, 0, 0, self.width, self.height);
        Self::write(bus, 0x24, frame);
        Self::write(bus, 0x26, frame);
    }
    fn finish_power_off(&mut self, bus: &mut impl Bus) {
        if self.pending_off {
            self.pending_off = false;
            self.power_off(bus);
        }
    }
    pub fn finish(&mut self, bus: &mut impl Bus, frame: Option<&[u8]>) {
        bus.refresh_wait(63);
        if self.pending_sync {
            if let Some(frame) = frame {
                self.sync(bus, frame);
            }
        }
        self.pending_sync = false;
        self.finish_power_off(bus);
    }
    pub fn display_window(
        &mut self,
        bus: &mut impl Bus,
        allocator: &impl WindowAllocator,
        frame: &[u8],
        previous: Option<&[u8]>,
        rect: Window,
        turn_off: bool,
    ) -> Result<(), WindowAllocationFailed> {
        let Window {
            x,
            y,
            width,
            height,
        } = rect;
        if u32::from(x) + u32::from(width) > u32::from(self.width)
            || u32::from(y) + u32::from(height) > u32::from(self.height)
            || !x.is_multiple_of(8)
            || !width.is_multiple_of(8)
        {
            return Ok(());
        }
        if self.in_gray || self.needs_gray_clear {
            self.display(bus, frame, None, self.clean_mode(), turn_off, false);
            return Ok(());
        }
        let size = usize::from(width / 8) * usize::from(height);
        // The C++ window path retains the current packed window through refresh,
        // but releases the previous packed window immediately after its upload.
        let mut current = allocator
            .allocate(size)
            .ok_or(WindowAllocationFailed { bytes: size })?;
        self.pack_window(frame, current.as_mut(), rect);
        self.area(bus, x, y, width, height);
        Self::write(bus, 0x24, current.as_ref());
        if let Some(previous) = previous {
            let mut old = allocator
                .allocate(size)
                .ok_or(WindowAllocationFailed { bytes: size })?;
            self.pack_window(previous, old.as_mut(), rect);
            Self::write(bus, 0x26, old.as_ref());
        }
        self.refresh(bus, Refresh::Fast, turn_off, false);
        if previous.is_none() {
            self.area(bus, x, y, width, height);
            Self::write(bus, 0x24, current.as_ref());
            Self::write(bus, 0x26, current.as_ref());
        }
        self.finish_power_off(bus);
        Ok(())
    }
    fn pack_window(&self, frame: &[u8], output: &mut [u8], rect: Window) {
        let row_bytes = usize::from(rect.width / 8);
        let stride = usize::from(self.width / 8);
        for row in 0..usize::from(rect.height) {
            let source = (usize::from(rect.y) + row) * stride + usize::from(rect.x / 8);
            output[row * row_bytes..(row + 1) * row_bytes]
                .copy_from_slice(&frame[source..source + row_bytes]);
        }
    }
    pub fn seed(&self, bus: &mut impl Bus, frame: &[u8]) {
        self.area(bus, 0, 0, self.width, self.height);
        Self::write(bus, 0x26, frame);
    }
    pub fn begin_gray(
        &mut self,
        bus: &mut impl Bus,
        frame: &[u8],
        mode: u32,
        fallback: Refresh,
        turn_off: bool,
    ) {
        self.absolute_input = mode == 1;
        if !self.absolute_input {
            self.display(bus, frame, None, fallback, turn_off, false);
        }
    }
    pub fn gray_strip(&self, bus: &mut impl Bus, msb: bool, rows: &[u8], y: u16, height: u16) {
        if height == 0 {
            return;
        }
        self.area(bus, 0, y, self.width, height);
        let cmd = if msb { 0x26 } else { 0x24 };
        if !self.absolute_input {
            Self::write(bus, cmd, rows);
            return;
        }
        let mut chunk = [0u8; 128];
        bus.cmd(cmd);
        bus.txn_begin();
        for input in rows.chunks(128) {
            for (dst, src) in chunk.iter_mut().zip(input) {
                *dst = !src;
            }
            bus.txn_write(&chunk[..input.len()]);
        }
        bus.txn_end();
    }
    pub fn gray_display(
        &mut self,
        bus: &mut impl Bus,
        turn_off: bool,
        lut: Option<&[u8]>,
        factory: bool,
    ) {
        self.in_gray = !factory;
        self.set_lut(
            bus,
            Some(lut.unwrap_or(if factory {
                self.cfg.factory
            } else {
                self.cfg.gray
            })),
        );
        if factory {
            Self::reg(bus, 0x21, 0);
            Self::reg(bus, 0x22, 0xcc);
            bus.cmd(0x20);
            bus.refresh_wait(66);
            self.screen_on = true;
            if turn_off {
                self.power_off(bus);
            }
            self.needs_gray_clear = true;
        } else {
            if self.cfg.gray_power_first {
                self.power_on(bus);
            }
            self.refresh(bus, Refresh::Fast, turn_off, false);
        }
        self.set_lut(bus, None);
        self.absolute_input = false;
    }
    pub fn cleanup_gray(&mut self, bus: &mut impl Bus, frame: &[u8]) {
        self.seed(bus, frame);
        self.in_gray = false;
    }
    pub fn resync(&mut self) {
        self.needs_gray_clear = true;
        self.absolute_input = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_invalid_geometry_and_configuration() {
        for (w, h, v, f) in [
            (0, 480, 0, 0),
            (799, 480, 0, 0),
            (800, 0, 0, 0),
            (800, 656, 0, 0),
            (800, 480, 6, 0),
            (800, 480, 0, 8),
        ] {
            assert!(Ssd1677::new(w, h, v, f).is_none());
        }
        for variant in 0..6 {
            for flags in 0..8 {
                let panel = Ssd1677::new(800, 480, variant, flags).unwrap();
                assert_eq!(panel.frame_size(), 48_000);
                assert_eq!(
                    panel.capabilities(0),
                    CAP_ASYNC_DISPLAY | CAP_OVERLAY | CAP_STRIPS | CAP_ASYNC_BASE
                );
                assert_eq!(
                    panel.capabilities(1),
                    CAP_ASYNC_DISPLAY | CAP_ABSOLUTE | CAP_COMBINED | CAP_STRIPS
                );
                assert_eq!(panel.capabilities(2), CAP_ASYNC_DISPLAY);
            }
        }
    }
}
