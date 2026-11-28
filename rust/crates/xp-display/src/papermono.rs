//! Paper Mono controller policy, ported from FreeInk SDK 08effe64.
use crate::{
    retained::{Plane, Psram},
    Bus,
};

pub const WIDTH: usize = 800;
pub const HEIGHT: usize = 480;
pub const SIZE: usize = 48000;
pub const CHUNK: usize = 16384;
pub const TAG_RESET: u32 = 96;
pub const TAG_REFRESH: u32 = 97;
pub const TAG_IDLE: u32 = 98;

#[derive(Clone, Copy, PartialEq, Eq)]
enum LutState {
    Unknown,
    OtpBw,
    Custom,
}

pub trait Diagnostics {
    fn event(&mut self, event: u32, values: [u32; 4]);
}

pub struct PaperMono {
    log: bool,
    rotate: bool,
    initialized: bool,
    needs_full: bool,
    dark: bool,
    boot_paints: u8,
    last_valid: bool,
    preparing_gray: bool,
    panel_gray: bool,
    gray_ready: [bool; 2],
    pending: bool,
    corrective: bool,
    committed: bool,
    powered: bool,
    window_valid: bool,
    lut: LutState,
    pending_generation: u32,
    gray_generation: [u32; 2],
    render_generation: u32,
    work_generation: u32,
    gray_frames: u8,
    planes: [Plane; 8],
    coverage: [[u8; 60]; 2],
    covered: [u16; 2],
}
const LAST: usize = 0;
const PENDING: usize = 1;
const LSB: usize = 2;
const MSB: usize = 3;
const GLASS_NONWHITE: usize = 4;
const GLASS_BLACK: usize = 5;
const SELECT_NEW: usize = 6;
const SELECT_OLD: usize = 7;

impl PaperMono {
    pub fn new(rotate: bool, log: bool) -> Self {
        Self {
            log,
            rotate,
            initialized: false,
            needs_full: true,
            dark: false,
            boot_paints: 0,
            last_valid: false,
            preparing_gray: false,
            panel_gray: false,
            gray_ready: [false; 2],
            pending: false,
            corrective: false,
            committed: false,
            powered: false,
            window_valid: false,
            lut: LutState::Unknown,
            pending_generation: 0,
            gray_generation: [0; 2],
            render_generation: 0,
            work_generation: 0,
            gray_frames: 24,
            planes: core::array::from_fn(|_| Plane::new(SIZE)),
            coverage: [[0; 60]; 2],
            covered: [0; 2],
        }
    }
    /// # Safety
    /// Use the same allocator that supplied every retained plane.
    pub unsafe fn release(&mut self, memory: &mut impl Psram) {
        for plane in &mut self.planes {
            plane.release(memory);
        }
    }
    pub fn capabilities(mode: u32) -> u32 {
        use crate::constants::*;
        if mode == 0 {
            CAP_OVERLAY | CAP_COMBINED | CAP_STRIPS | CAP_BUSY_STAGING
        } else {
            0
        }
    }
    fn allocate(&mut self, memory: &mut impl Psram) -> bool {
        let glass_missing =
            !self.planes[GLASS_NONWHITE].allocated() || !self.planes[GLASS_BLACK].allocated();
        for plane in &mut self.planes {
            if !plane.ensure(memory) {
                return false;
            }
        }
        if glass_missing {
            self.planes[GLASS_NONWHITE].bytes_mut().fill(0);
            self.planes[GLASS_BLACK].bytes_mut().fill(0);
        }
        true
    }
    pub fn begin(&mut self, bus: &mut (impl Bus + Diagnostics), memory: &mut impl Psram) {
        self.allocate(memory);
        bus.reset(0);
        self.init(bus);
        self.needs_full = true;
        self.boot_paints = 3;
        self.last_valid = false;
        self.work_generation = 0;
        self.powered = false;
        self.lut = LutState::Unknown;
        self.window_valid = false;
        self.reset_gray();
    }
    fn init(&mut self, bus: &mut (impl Bus + Diagnostics)) {
        bus.cmd(0x12);
        bus.busy_wait(0, TAG_RESET);
        self.powered = false;
        self.lut = LutState::Unknown;
        self.window_valid = false;
        bus.cmd(0x18);
        bus.data8(0x80);
        bus.cmd_data(0x0c, &[0xae, 0xc7, 0xc3, 0xc0, 0x80]);
        bus.cmd(1);
        bus.data8(0xdf);
        bus.data8(1);
        bus.data8(2);
        bus.cmd(0x3c);
        bus.data8(0x80);
        bus.cmd(0x11);
        bus.data8(2);
        bus.cmd(0x44);
        bus.data8(0x1f);
        bus.data8(3);
        bus.data8(0);
        bus.data8(0);
        bus.cmd(0x45);
        bus.data8(0);
        bus.data8(0);
        bus.data8(0xdf);
        bus.data8(1);
        bus.cmd(0x21);
        bus.data8(0);
        Self::reset_counter(bus);
        self.initialized = true;
    }
    fn reset_counter(bus: &mut (impl Bus + Diagnostics)) {
        bus.cmd(0x4e);
        bus.data8(0x1f);
        bus.data8(3);
        bus.cmd(0x4f);
        bus.data8(0);
        bus.data8(0);
    }
    fn window(bus: &mut (impl Bus + Diagnostics), x: u16, y: u16, w: u16, h: u16) {
        let xs = x + w - 1;
        let ys = HEIGHT as u16 - y - h;
        let ye = HEIGHT as u16 - 1 - y;
        bus.cmd(0x44);
        Self::data16(bus, xs);
        Self::data16(bus, x);
        bus.cmd(0x45);
        Self::data16(bus, ys);
        Self::data16(bus, ye);
        bus.cmd(0x4e);
        Self::data16(bus, xs);
        bus.cmd(0x4f);
        Self::data16(bus, ys);
    }
    fn data16(bus: &mut (impl Bus + Diagnostics), value: u16) {
        bus.data8(value as u8);
        bus.data8((value >> 8) as u8);
    }
    fn write(
        &self,
        bus: &mut (impl Bus + Diagnostics),
        command: u8,
        data: &[u8],
        chunk: &mut [u8; CHUNK],
    ) {
        Self::reset_counter(bus);
        bus.cmd(command);
        if !self.rotate {
            bus.data_span(data);
            return;
        }
        bus.txn_begin();
        for sent in (0..SIZE).step_by(CHUNK) {
            let count = (SIZE - sent).min(CHUNK);
            for (i, target) in chunk[..count].iter_mut().enumerate() {
                *target = data[SIZE - 1 - sent - i].reverse_bits();
            }
            bus.txn_write(&chunk[..count]);
        }
        bus.txn_end();
    }
    #[allow(clippy::too_many_arguments)]
    fn write_window(
        bus: &mut (impl Bus + Diagnostics),
        command: u8,
        data: &[u8],
        x: u16,
        y: u16,
        w: u16,
        h: u16,
        chunk: &mut [u8; CHUNK],
    ) {
        Self::window(bus, x, y, w, h);
        bus.cmd(command);
        bus.txn_begin();
        let mut n = 0;
        for row in (usize::from(y)..usize::from(y + h)).rev() {
            for byte in (usize::from(x / 8)..usize::from((x + w) / 8)).rev() {
                chunk[n] = data[row * 100 + byte].reverse_bits();
                n += 1;
                if n == CHUNK {
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
    fn activate(&mut self, bus: &mut (impl Bus + Diagnostics), control: u8) {
        self.window_valid = false;
        bus.cmd(0x22);
        bus.data8(control);
        bus.cmd(0x20);
        bus.refresh_wait(TAG_REFRESH);
    }
    fn activate_otp(&mut self, bus: &mut (impl Bus + Diagnostics)) {
        let warm = self.powered && self.lut == LutState::OtpBw;
        self.activate(bus, if warm { 0x0c } else { 0xfc });
        self.powered = true;
        self.lut = LutState::OtpBw;
    }
    fn power_off(&mut self, bus: &mut (impl Bus + Diagnostics)) {
        if self.powered {
            self.activate(bus, 3);
            self.powered = false;
        }
    }
    fn load_lut(&mut self, bus: &mut (impl Bus + Diagnostics), lut: &[u8; 111]) {
        bus.cmd(0x32);
        bus.data_span(&lut[..105]);
        bus.cmd(3);
        bus.data8(0x17);
        bus.cmd_data(4, &[0x41, 0xa8, 0x32]);
        bus.cmd(0x2c);
        bus.data8(0x30);
        self.lut = LutState::Custom;
    }
    fn run_otp(
        &mut self,
        bus: &mut (impl Bus + Diagnostics),
        target: &[u8],
        force: bool,
        chunk: &mut [u8; CHUNK],
    ) -> bool {
        let mut changed = 0u8;
        let mut changed_count = 0u32;
        for (i, &bw) in target.iter().enumerate() {
            let value = if force {
                changed = 255;
                if self.log {
                    changed_count += 8;
                }
                !bw
            } else {
                let nonwhite = self.planes[GLASS_NONWHITE].bytes()[i];
                let black = self.planes[GLASS_BLACK].bytes()[i];
                let mask = (nonwhite ^ !bw) | (black ^ !bw);
                changed |= mask;
                if self.log {
                    changed_count += popcount(mask);
                }
                !nonwhite | (!bw & nonwhite & !black) | if self.dark { !bw } else { 0 }
            };
            self.planes[SELECT_OLD].bytes_mut()[i] = value;
        }
        if changed == 0 {
            return false;
        }
        let started = bus.clock_ms();
        bus.cmd(0x3c);
        bus.data8(0x80);
        self.write(bus, 0x24, target, chunk);
        self.write(bus, 0x26, self.planes[SELECT_OLD].bytes(), chunk);
        self.activate_otp(bus);
        if self.boot_paints != 0 {
            self.boot_paints -= 1;
            for _ in 0..2 {
                self.write(bus, 0x24, target, chunk);
                self.write(bus, 0x26, self.planes[SELECT_OLD].bytes(), chunk);
                self.activate(bus, 0x0c);
            }
        }
        for (i, &bw) in target.iter().enumerate() {
            self.planes[GLASS_NONWHITE].bytes_mut()[i] = !bw;
            self.planes[GLASS_BLACK].bytes_mut()[i] = !bw;
        }
        self.panel_gray = false;
        if self.log {
            bus.event(99, [changed_count, u32::from(force), started, 0]);
        }
        true
    }
    fn run_update(
        &mut self,
        bus: &mut (impl Bus + Diagnostics),
        target: &[u8],
        gray: bool,
        corrective: bool,
        overlay: bool,
        chunk: &mut [u8; CHUNK],
    ) -> bool {
        if !self.initialized {
            bus.reset(0);
            self.init(bus);
        }
        if !gray {
            let ran = self.run_otp(bus, target, corrective, chunk);
            if ran {
                self.committed = true;
            }
            return ran;
        }
        let mut changed = 0u8;
        let mut changed_count = 0u32;
        let mut driven_count = 0u32;
        for (i, &bw) in target.iter().enumerate() {
            let gray = self.planes[LSB].bytes()[i] | self.planes[MSB].bytes()[i];
            let q24 = gray | !bw;
            let q26 = !bw & !gray;
            // A corrective paint ignores history, including a partially allocated
            // first glass model that has never been initialized.
            let mask = if corrective {
                0
            } else {
                (self.planes[GLASS_NONWHITE].bytes()[i] ^ q24)
                    | (self.planes[GLASS_BLACK].bytes()[i] ^ q26)
            };
            let drive = if corrective {
                255
            } else if overlay {
                mask
            } else {
                mask | q24
            };
            changed |= if corrective { 255 } else { mask };
            if self.log {
                changed_count += if corrective { 8 } else { popcount(mask) };
                driven_count += popcount(drive);
            }
            self.planes[SELECT_NEW].bytes_mut()[i] = drive & !(q24 ^ q26);
            self.planes[SELECT_OLD].bytes_mut()[i] = drive & q24;
        }
        if changed == 0 {
            return false;
        }
        let started = bus.clock_ms();
        self.write(bus, 0x24, self.planes[SELECT_NEW].bytes(), chunk);
        self.write(bus, 0x26, self.planes[SELECT_OLD].bytes(), chunk);
        // Plane uploads are complete; reuse their static staging memory for
        // the 111-byte waveform instead of keeping a second array on the stack.
        let lut: &mut [u8; 111] = (&mut chunk[..111]).try_into().unwrap();
        make_tri_lut(self.gray_frames, !overlay, lut);
        self.load_lut(bus, lut);
        self.activate(bus, if self.powered { 0x0c } else { 0xcc });
        self.powered = true;
        for (i, &bw) in target.iter().enumerate() {
            let gray = self.planes[LSB].bytes()[i] | self.planes[MSB].bytes()[i];
            self.planes[GLASS_NONWHITE].bytes_mut()[i] = gray | !bw;
            self.planes[GLASS_BLACK].bytes_mut()[i] = !bw & !gray;
        }
        if self.log {
            let frames =
                16 + (16 + u32::from(self.gray_frames) / 3 + u32::from(self.gray_frames)).max(48);
            bus.event(
                100,
                [
                    changed_count,
                    driven_count,
                    frames | (u32::from(corrective) << 16),
                    started,
                ],
            );
        }
        self.panel_gray = self.gray_ready[0] || self.gray_ready[1];
        self.committed = true;
        true
    }
    fn commit(
        &mut self,
        bus: &mut (impl Bus + Diagnostics),
        gray: bool,
        chunk: &mut [u8; CHUNK],
    ) -> bool {
        if !self.pending {
            return false;
        }
        if self.pending_generation != self.render_generation {
            self.pending = false;
            self.corrective = false;
            self.clear_gray();
            return false;
        }
        self.pending = false;
        let corrective = self.corrective;
        // PENDING is allocated and never mutated by run_update; raw ownership
        // separates this retained target from the other seven writable planes.
        let target = unsafe { core::slice::from_raw_parts(self.planes[PENDING].as_ptr(), SIZE) };
        let ran = self.run_update(bus, target, gray, corrective, false, chunk);
        if ran {
            self.needs_full = false;
        }
        self.corrective = false;
        if ran || !corrective {
            self.planes[LAST].bytes_mut().copy_from_slice(target);
            self.last_valid = true;
        }
        if !gray {
            self.panel_gray = false;
        }
        self.clear_gray();
        ran
    }
    pub fn display(
        &mut self,
        bus: &mut (impl Bus + Diagnostics),
        memory: &mut impl Psram,
        fb: &[u8],
        mode: u32,
        chunk: &mut [u8; CHUNK],
    ) {
        if !self.initialized {
            bus.reset(0);
            self.init(bus);
        }
        if !self.allocate(memory) {
            return;
        }
        let corrective = self.needs_full || mode == 0;
        if !corrective
            && self.last_valid
            && !self.panel_gray
            && !self.gray_ready[0]
            && !self.gray_ready[1]
            && self.planes[LAST].bytes() == fb
        {
            return;
        }
        self.planes[PENDING].bytes_mut().copy_from_slice(fb);
        self.pending = true;
        self.corrective = corrective;
        self.pending_generation = self.render_generation;
        if !self.preparing_gray {
            self.commit(bus, false, chunk);
        }
    }
    #[allow(clippy::too_many_arguments)]
    pub fn display_window(
        &mut self,
        bus: &mut (impl Bus + Diagnostics),
        memory: &mut impl Psram,
        fb: &[u8],
        rect: [u16; 4],
        off: bool,
        chunk: &mut [u8; CHUNK],
    ) {
        let [x, y, w, h] = rect;
        if w == 0
            || h == 0
            || u32::from(x) + u32::from(w) > 800
            || u32::from(y) + u32::from(h) > 480
            || x % 8 != 0
            || w % 8 != 0
        {
            return;
        }
        if !self.initialized {
            bus.reset(0);
            self.init(bus);
        }
        if !self.allocate(memory) {
            return;
        }
        if !self.rotate
            || self.needs_full
            || !self.last_valid
            || self.panel_gray
            || self.gray_ready[0]
            || self.gray_ready[1]
        {
            self.display(bus, memory, fb, 2, chunk);
            return;
        }
        let changed = (usize::from(y)..usize::from(y + h)).any(|row| {
            let start = row * 100 + usize::from(x / 8);
            let end = start + usize::from(w / 8);
            fb[start..end] != self.planes[LAST].bytes()[start..end]
        });
        if !changed {
            return;
        }
        if !self.window_valid {
            Self::window(bus, 0, 0, 800, 480);
            self.write(bus, 0x24, self.planes[LAST].bytes(), chunk);
            self.write(bus, 0x26, self.planes[LAST].bytes(), chunk);
            self.window_valid = true;
        }
        Self::write_window(bus, 0x24, fb, x, y, w, h, chunk);
        Self::write_window(bus, 0x26, self.planes[LAST].bytes(), x, y, w, h, chunk);
        self.activate_otp(bus);
        for row in usize::from(y)..usize::from(y + h) {
            for byte in usize::from(x / 8)..usize::from((x + w) / 8) {
                let i = row * 100 + byte;
                self.planes[GLASS_NONWHITE].bytes_mut()[i] = !fb[i];
                self.planes[GLASS_BLACK].bytes_mut()[i] = !fb[i];
                self.planes[LAST].bytes_mut()[i] = fb[i];
            }
        }
        Self::write_window(bus, 0x24, fb, x, y, w, h, chunk);
        Self::write_window(bus, 0x26, fb, x, y, w, h, chunk);
        Self::window(bus, 0, 0, 800, 480);
        self.window_valid = true;
        self.panel_gray = false;
        self.committed = true;
        if off {
            self.power_off(bus);
        }
    }
    pub fn seed(&mut self, memory: &mut impl Psram, fb: &[u8]) {
        if self.allocate(memory) {
            self.planes[LAST].bytes_mut().copy_from_slice(fb);
            self.last_valid = true;
        }
    }
    pub fn gray_base(
        &mut self,
        bus: &mut (impl Bus + Diagnostics),
        memory: &mut impl Psram,
        fb: &[u8],
        fallback: u32,
        chunk: &mut [u8; CHUNK],
    ) {
        self.preparing_gray = true;
        self.display(bus, memory, fb, fallback, chunk);
        self.preparing_gray = false;
    }
    pub fn begin_work(&mut self, abort: u32) {
        self.work_generation = abort;
        self.committed = false;
        self.render_generation = self.render_generation.wrapping_add(1);
        if self.render_generation == 0 {
            self.render_generation = 1;
        }
        self.pending = false;
        self.corrective = false;
        self.clear_gray();
    }
    pub fn aborted(&self, abort: u32) -> bool {
        abort != self.work_generation
    }
    pub fn committed(&self) -> bool {
        self.committed
    }
    pub fn gray_copy(&mut self, memory: &mut impl Psram, plane: usize, data: &[u8]) {
        if !self.allocate(memory) {
            return;
        }
        self.planes[LSB + plane].bytes_mut().copy_from_slice(data);
        self.coverage[plane].fill(255);
        self.covered[plane] = 480;
        self.gray_generation[plane] = self.render_generation;
        self.gray_ready[plane] = true;
    }
    pub fn gray_strip(
        &mut self,
        memory: &mut impl Psram,
        plane: usize,
        data: &[u8],
        y: u16,
        rows: u16,
    ) {
        if rows == 0 || u32::from(y) + u32::from(rows) > 480 || !self.allocate(memory) {
            return;
        }
        if self.gray_generation[plane] != self.render_generation {
            self.coverage[plane].fill(0);
            self.covered[plane] = 0;
            self.gray_ready[plane] = false;
            self.gray_generation[plane] = self.render_generation;
        }
        self.planes[LSB + plane].bytes_mut()[usize::from(y) * 100..usize::from(y + rows) * 100]
            .copy_from_slice(data);
        for row in usize::from(y)..usize::from(y + rows) {
            let mask = 1 << (row & 7);
            let slot = &mut self.coverage[plane][row >> 3];
            if *slot & mask == 0 {
                *slot |= mask;
                self.covered[plane] += 1;
            }
        }
        self.gray_ready[plane] = self.covered[plane] == 480;
    }
    fn has_gray(&self) -> bool {
        self.gray_ready[0]
            && self.gray_ready[1]
            && self.gray_generation == [self.render_generation; 2]
    }
    pub fn gray_display(
        &mut self,
        bus: &mut (impl Bus + Diagnostics),
        memory: &mut impl Psram,
        abort: impl Fn() -> u32,
        chunk: &mut [u8; CHUNK],
    ) {
        if !self.allocate(memory) {
            return;
        }
        if self.aborted(abort()) {
            self.pending = false;
            self.corrective = false;
            self.clear_gray();
            return;
        }
        let gray = self.has_gray();
        if self.pending {
            self.commit(bus, gray, chunk);
            return;
        }
        if !gray || !self.last_valid {
            self.clear_gray();
            return;
        }
        // LAST stays immutable through run_update; selectors/glass are separate allocations.
        let target = unsafe { core::slice::from_raw_parts(self.planes[LAST].as_ptr(), SIZE) };
        self.run_update(bus, target, true, false, true, chunk);
        self.clear_gray();
    }
    pub fn calibrate(
        &mut self,
        bus: &mut (impl Bus + Diagnostics),
        memory: &mut impl Psram,
        fb: &[u8],
        chunk: &mut [u8; CHUNK],
    ) {
        if !self.allocate(memory) {
            return;
        }
        self.planes[PENDING].bytes_mut().copy_from_slice(fb);
        self.pending = true;
        self.corrective = true;
        self.pending_generation = self.render_generation;
        self.commit(bus, self.has_gray(), chunk);
    }
    pub fn cleanup(
        &mut self,
        bus: &mut (impl Bus + Diagnostics),
        bw: Option<&[u8]>,
        abort: impl Fn() -> u32,
        chunk: &mut [u8; CHUNK],
    ) {
        if !self.pending {
            self.clear_gray();
            return;
        }
        if self.aborted(abort()) {
            self.pending = false;
            self.corrective = false;
            self.clear_gray();
            return;
        }
        if let Some(bw) = bw {
            self.planes[PENDING].bytes_mut().copy_from_slice(bw);
        }
        self.commit(bus, self.has_gray(), chunk);
    }
    pub fn idle(&mut self, bus: &mut (impl Bus + Diagnostics)) {
        if !self.initialized {
            return;
        }
        self.power_off(bus);
        bus.cmd(0x10);
        bus.data8(3);
        bus.delay_ms(2);
        self.initialized = false;
        self.powered = false;
        self.window_valid = false;
    }
    pub fn gray_params(&mut self, light_frames: u8) {
        let requested = 2 * u16::from(light_frames.clamp(1, 12)) + 2;
        self.gray_frames = (((requested.clamp(6, 60) + 1) / 3) * 3) as u8;
    }
    pub fn resync(&mut self) {
        self.needs_full = true;
        self.last_valid = false;
        self.reset_gray();
    }
    pub fn skip_resync(&mut self) {
        self.needs_full = true;
    }
    pub fn background(&mut self, dark: bool) {
        self.dark = dark;
    }
    pub fn reset_gray(&mut self) {
        self.panel_gray = false;
        self.clear_gray();
        self.pending = false;
        self.corrective = false;
        self.pending_generation = 0;
    }
    fn clear_gray(&mut self) {
        self.gray_ready = [false; 2];
        self.gray_generation = [0; 2];
        self.covered = [0; 2];
        self.coverage = [[0; 60]; 2];
    }
    pub fn sleep(&mut self, bus: &mut (impl Bus + Diagnostics)) {
        if self.initialized {
            bus.busy_wait(0, TAG_IDLE);
            self.power_off(bus);
            bus.cmd(0x10);
            bus.data8(3);
            bus.delay_ms(100);
        }
        self.initialized = false;
        self.needs_full = true;
        self.last_valid = false;
        self.window_valid = false;
        self.reset_gray();
    }
}

// SDK's production tri waveform: kick=16, black=32; right-aligned classes,
// optional unchanged-white top-up. Retired post-clean cycles are always zero.
fn make_tri_lut(gray: u8, top_up: bool, lut: &mut [u8; 111]) {
    lut.fill(0);
    for phase in 0..3 {
        set_vs(lut, 1, 0, phase, 1);
        set_vs(lut, 2, 0, phase, 1);
        set_vs(lut, 3, 0, phase, 2);
    }
    if top_up {
        set_vs(lut, 0, 0, 0, 1);
        set_vs(lut, 0, 0, 1, 2);
    }
    lut[50..55].copy_from_slice(&[1, 5, 10, 0, 0]);
    let white = [16u16, 16 + u16::from(gray) / 3, 16];
    let trajectory = [white[0], white[1] + u16::from(gray), 48];
    let tail = trajectory[0].max(trajectory[1]).max(trajectory[2]);
    let start = trajectory.map(|n| tail - n);
    let mut bounds = [
        0,
        start[0],
        start[1],
        start[1] + white[1],
        start[2],
        start[2] + white[2],
        tail,
    ];
    bounds.sort_unstable();
    let mut previous = 0;
    let mut group = 1;
    for current in bounds {
        if current <= previous || group >= 10 {
            continue;
        }
        if previous >= start[0] {
            set_vs(lut, 1, group, 0, 2);
        }
        if previous >= start[1] {
            set_vs(
                lut,
                2,
                group,
                0,
                if previous < start[1] + white[1] { 2 } else { 3 },
            );
        }
        if previous >= start[2] {
            set_vs(
                lut,
                3,
                group,
                0,
                if previous < start[2] + white[2] { 2 } else { 1 },
            );
        }
        lut[50 + group * 5] = (current - previous) as u8;
        previous = current;
        group += 1;
    }
    lut[100..105].fill(0x88);
}
fn set_vs(lut: &mut [u8; 111], entry: usize, group: usize, phase: usize, value: u8) {
    let shift = (3 - phase) * 2;
    let slot = &mut lut[entry * 10 + group];
    *slot = (*slot & !(3 << shift)) | (value << shift);
}

fn popcount(value: u8) -> u32 {
    let value = value - ((value >> 1) & 0x55);
    let value = (value & 0x33) + ((value >> 2) & 0x33);
    u32::from((value + (value >> 4)) & 15)
}
