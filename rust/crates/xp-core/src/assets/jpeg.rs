extern crate alloc;
#[cfg(not(target_arch = "riscv32"))]
use super::memory::{Buffer, Zeroed};
#[cfg(target_arch = "riscv32")]
use alloc::vec::Vec;
#[cfg(target_arch = "riscv32")]
type Buffer<T> = Vec<T>;
#[cfg(target_arch = "riscv32")]
use core::alloc::Layout;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Config {
    pub width: i32,
    pub height: i32,
    pub one_bit: bool,
    pub crop: bool,
    pub original_thresholds: bool,
}

pub trait Output {
    fn write(&mut self, bytes: &[u8]);
    fn progress(&mut self);
    fn diagnostic(&mut self, event: u32, a: i32, b: i32, c: i32, d: i32);
}

/// Only these primitive buffers accept an all-zero representation.
/// # Safety
/// Implementors have no invalid all-zero bit pattern and no destructor.
#[cfg(target_arch = "riscv32")]
unsafe trait Zeroed: Copy {}
#[cfg(target_arch = "riscv32")]
unsafe impl Zeroed for u8 {}
#[cfg(target_arch = "riscv32")]
unsafe impl Zeroed for u32 {}
#[cfg(target_arch = "riscv32")]
unsafe impl Zeroed for i16 {}

#[cfg(target_arch = "riscv32")]
#[inline(never)]
fn zero_allocation(layout: Layout) -> *mut u8 {
    // SAFETY: callers supply a nonempty checked layout and handle allocation failure.
    unsafe { alloc::alloc::alloc_zeroed(layout) }
}
#[cfg(target_arch = "riscv32")]
fn zeros<T: Zeroed>(len: usize) -> Option<Buffer<T>> {
    if len == 0 {
        return Some(Vec::new());
    }
    let layout = Layout::array::<T>(len).ok()?;
    let data = zero_allocation(layout).cast::<T>();
    if data.is_null() {
        return None;
    }
    // SAFETY: allocation uses T's exact layout; Zeroed guarantees initialized elements.
    Some(unsafe { Vec::from_raw_parts(data, len, len) })
}
#[cfg(not(target_arch = "riscv32"))]
fn zeros<T: Zeroed>(len: usize) -> Option<Buffer<T>> {
    let mut buffer = Buffer::new();
    buffer.grow(len).then_some(buffer)
}
fn step(src: usize, dst: usize) -> u32 {
    if src <= 1 || dst <= 1 {
        0
    } else {
        (((src - 1) << 16) / (dst - 1)) as u32
    }
}
fn position(index: usize, dst: usize, src: usize, step: u32) -> u32 {
    if src <= 1 || dst <= 1 {
        0
    } else if index >= dst - 1 {
        ((src - 1) << 16) as u32
    } else {
        index as u32 * step
    }
}

pub struct Block<'a> {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub stride: i32,
    pub pixels: &'a [u8],
}

pub struct Converter {
    sw: usize,
    sh: usize,
    width: usize,
    height: usize,
    config: Config,
    smooth: bool,
    scaling: bool,
    sx: u32,
    sy: u32,
    next_y: u32,
    out_y: usize,
    previous: Option<usize>,
    row_index: usize,
    rows_since_yield: u8,
    blocks_since_yield: u8,
    mcu: Buffer<u8>,
    row: Buffer<u8>,
    smooth_rows: Buffer<u8>,
    accum: Buffer<u32>,
    counts: Buffer<u32>,
    errors: Buffer<i16>,
}
impl Converter {
    pub fn new(
        sw: i32,
        sh: i32,
        progressive: bool,
        config: Config,
        output: &mut dyn Output,
    ) -> Option<Self> {
        let dw = if progressive {
            ((i64::from(sw) + 7) >> 3) as i32
        } else {
            sw
        };
        let dh = if progressive {
            ((i64::from(sh) + 7) >> 3) as i32
        } else {
            sh
        };
        output.diagnostic(0, sw, sh, 0, 0);
        if progressive {
            output.diagnostic(1, dw, dh, 0, 0);
        }
        if sw <= 0 || sh <= 0 || sw > 2048 || sh > 3072 {
            output.diagnostic(2, sw, sh, 0, 0);
            return None;
        }
        let (mut w, mut h) = (sw, sh);
        if config.width <= 0 || config.height <= 0 {
            w = dw;
            h = dh;
        }
        if config.width > 0 && config.height > 0 && (sw != config.width || sh != config.height) {
            let x = config.width as f32 / sw as f32;
            let y = config.height as f32 / sh as f32;
            let scale = if config.crop { x.max(y) } else { x.min(y) };
            w = ((sw as f32 * scale) as i32).max(1);
            h = ((sh as f32 * scale) as i32).max(1);
            output.diagnostic(3, dw, dh, w, h);
        }
        let (width, height) = (w as usize, h as usize);
        // The public callers supply panel/thumbnail dimensions; reject unrepresentable layouts.
        let bits = if config.one_bit { 1 } else { 2 };
        let row_bytes = width.checked_mul(bits)?.checked_add(31)? / 32 * 4;
        if row_bytes.checked_mul(height)? > i32::MAX as usize {
            return None;
        }
        let (sw, sh) = (dw as usize, dh as usize);
        let scaling = sw != width || sh != height;
        let smooth = progressive && scaling && sw <= width && sh <= height;
        let mut header = [0; 70];
        let len = super::bmp_header(w as u32, -h, bits as u8, &mut header);
        // JPEG's Print contract writes header bytes individually and ignores short writes.
        for byte in &header[..len] {
            output.write(core::slice::from_ref(byte));
        }
        let mut alloc = |len, event| {
            zeros::<u8>(len).or_else(|| {
                output.diagnostic(event, (sw * 16) as i32, 0, 0, 0);
                None
            })
        };
        let mcu = alloc(sw * 16, 4)?;
        let row = alloc(row_bytes, 5)?;
        let smooth_rows = alloc(if smooth { width.checked_mul(3)? } else { 0 }, 6)?;
        if smooth {
            output.diagnostic(7, dw, dh, w, h);
        }
        let count = if scaling && !smooth { width } else { 0 };
        let accum = zeros(count);
        let counts = zeros(count);
        let (Some(accum), Some(counts)) = (accum, counts) else {
            output.diagnostic(8, 0, 0, 0, 0);
            return None;
        };
        let Some(errors) = zeros(width.checked_add(4)?.checked_mul(3)?) else {
            output.diagnostic(if config.one_bit { 9 } else { 10 }, 0, 0, 0, 0);
            return None;
        };
        let sy = ((sh << 16) / height) as u32;
        Some(Self {
            sw,
            sh,
            width,
            height,
            config,
            smooth,
            scaling,
            sx: ((sw << 16) / width) as u32,
            sy,
            next_y: sy,
            out_y: 0,
            previous: None,
            row_index: 0,
            rows_since_yield: 0,
            blocks_since_yield: 0,
            mcu,
            row,
            smooth_rows,
            accum,
            counts,
            errors,
        })
    }
    fn write_row(&mut self, source: Option<usize>, output: &mut dyn Output) {
        self.row.fill(0);
        let stride = self.width + 4;
        let r0 = self.row_index * stride;
        let r1 = ((self.row_index + 1) % 3) * stride;
        let r2 = ((self.row_index + 2) % 3) * stride;
        for x in 0..self.width {
            let gray = if let Some(start) = source {
                if self.smooth {
                    self.smooth_rows[start + x] as u32
                } else {
                    self.mcu[start + x] as u32
                }
            } else {
                self.accum[x].checked_div(self.counts[x]).unwrap_or(0)
            };
            let adjusted = (gray as i32 + self.errors[r0 + x + 2] as i32).clamp(0, 255);
            let (q, value) = if self.config.one_bit {
                if adjusted < 128 {
                    (0, 0)
                } else {
                    (1, 255)
                }
            } else if self.config.original_thresholds {
                if adjusted < 43 {
                    (0, 0)
                } else if adjusted < 128 {
                    (1, 85)
                } else if adjusted < 213 {
                    (2, 170)
                } else {
                    (3, 255)
                }
            } else if adjusted < 30 {
                (0, 15)
            } else if adjusted < 55 {
                (1, 35)
            } else if adjusted < 150 {
                (2, 90)
            } else {
                (3, 210)
            };
            let error = ((adjusted - value) >> 3) as i16;
            for i in [
                r0 + x + 3,
                r0 + x + 4,
                r1 + x + 1,
                r1 + x + 2,
                r1 + x + 3,
                r2 + x + 2,
            ] {
                self.errors[i] += error;
            }
            if self.config.one_bit {
                self.row[x / 8] |= q << (7 - x % 8);
            } else {
                self.row[x / 4] |= q << (6 - (x % 4) * 2);
            }
        }
        self.errors[r0..r0 + stride].fill(0);
        self.row_index = (self.row_index + 1) % 3;
        output.write(&self.row);
        self.out_y += 1;
        self.rows_since_yield += 1;
        if self.rows_since_yield == 8 {
            self.rows_since_yield = 0;
            output.progress();
        }
    }
    fn smooth_row(&mut self, start: usize, y: usize, output: &mut dyn Output) {
        let sx = step(self.sw, self.width);
        // Previous/current rows alternate without allocating or moving them.
        let current = (y % 2) * self.width;
        let previous = ((y + 1) % 2) * self.width;
        for x in 0..self.width {
            let fp = position(x, self.width, self.sw, sx);
            let x0 = (fp >> 16) as usize;
            let x1 = (x0 + 1).min(self.sw - 1);
            let f = fp & 65535;
            self.smooth_rows[current + x] = ((u32::from(self.mcu[start + x0]) * (65536 - f)
                + u32::from(self.mcu[start + x1]) * f)
                >> 16) as u8;
        }
        if self.previous.is_none() {
            self.previous = Some(y);
            if self.sh <= 1 {
                while self.out_y < self.height {
                    self.write_row(Some(current), output);
                }
            }
            return;
        }
        while self.out_y < self.height {
            let fp = position(self.out_y, self.height, self.sh, step(self.sh, self.height));
            let y0 = (fp >> 16) as usize;
            let y1 = (y0 + 1).min(self.sh - 1);
            let f = fp & 65535;
            if y1 > y {
                break;
            }
            let row0 = if y0 == y { current } else { previous };
            let row1 = if y1 == y { current } else { previous };
            for x in 0..self.width {
                self.smooth_rows[2 * self.width + x] = ((u32::from(self.smooth_rows[row0 + x])
                    * (65536 - f)
                    + u32::from(self.smooth_rows[row1 + x]) * f)
                    >> 16) as u8;
            }
            self.write_row(Some(2 * self.width), output);
        }
        self.previous = Some(y);
    }
    pub fn block(&mut self, block: Block<'_>, output: &mut dyn Output) -> bool {
        let Block {
            x,
            y,
            width,
            height,
            stride,
            pixels,
        } = block;
        self.blocks_since_yield += 1;
        if self.blocks_since_yield == 16 {
            self.blocks_since_yield = 0;
            output.progress();
        }
        if x < 0 || y < 0 || x as usize >= self.sw || y as usize >= self.sh {
            output.diagnostic(11, x, y, self.sw as i32, self.sh as i32);
            return false;
        }
        if width < 0 || !(0..=16).contains(&height) || stride < width {
            return false;
        }
        let (x, y, width, height, stride) = (
            x as usize,
            y as usize,
            width as usize,
            height as usize,
            stride as usize,
        );
        let copy = width.min(self.sw - x);
        if pixels.len() < height.saturating_mul(stride) {
            return false;
        }
        for r in 0..height {
            self.mcu[r * self.sw + x..r * self.sw + x + copy]
                .copy_from_slice(&pixels[r * stride..r * stride + copy]);
        }
        if x + width < self.sw {
            return true;
        }
        for sy in y..(y + height).min(self.sh) {
            let start = (sy - y) * self.sw;
            if self.smooth {
                self.smooth_row(start, sy, output);
            } else if !self.scaling {
                self.write_row(Some(start), output);
            } else {
                for ox in 0..self.width {
                    let first = ((ox as u32 * self.sx) >> 16) as usize;
                    let end = (((ox + 1) as u32 * self.sx) >> 16) as usize;
                    let mut sum = 0;
                    let mut count = 0;
                    for sx in first..end.min(self.sw) {
                        sum += u32::from(self.mcu[start + sx]);
                        count += 1;
                    }
                    if count == 0 && first < self.sw {
                        sum = u32::from(self.mcu[start + first]);
                        count = 1;
                    }
                    self.accum[ox] += sum;
                    self.counts[ox] += count;
                }
                let fp = ((sy + 1) << 16) as u32;
                while fp >= self.next_y && self.out_y < self.height {
                    self.write_row(None, output);
                    self.next_y = (self.out_y as u32 + 1) * self.sy;
                    if fp < self.next_y {
                        self.accum.fill(0);
                        self.counts.fill(0);
                    }
                }
            }
        }
        true
    }
    pub fn finish(&mut self, output: &mut dyn Output) -> bool {
        if self.smooth {
            let Some(y) = self.previous else {
                output.diagnostic(12, 0, 0, 0, 0);
                return false;
            };
            while self.out_y < self.height {
                self.write_row(Some((y % 2) * self.width), output);
            }
        }
        true
    }
}
