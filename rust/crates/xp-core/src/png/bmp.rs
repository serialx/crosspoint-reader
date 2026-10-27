//! PNG cover scaling and Atkinson output, preserving the existing BMP bytes.
use super::alloc::vec::Vec;
use super::{zeros, Decoder, Info, Source};
use crate::image::Row;

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
    fn write(&mut self, bytes: &[u8]) -> bool;
    fn yield_rows(&mut self);
}

struct Bmp {
    width: usize,
    height: u32,
    scale_x: u32,
    scale_y: u32,
    scaling: bool,
    y: u32,
    next_y: u32,
    row: Vec<u8>,
    gray: Vec<u8>,
    errors: Vec<i16>,
    accum: Vec<u32>,
    counts: Vec<u32>,
    error_row: usize,
    config: Config,
}

impl Bmp {
    fn new(info: Info, config: Config) -> Option<Self> {
        let mut width = info.width;
        let mut height = info.height;
        let scaling = config.width > 0
            && config.height > 0
            && (width as i32 != config.width || height as i32 != config.height);
        let (scale_x, scale_y) = if scaling {
            let sx = config.width as f32 / info.width as f32;
            let sy = config.height as f32 / info.height as f32;
            let scale = if config.crop { sx.max(sy) } else { sx.min(sy) };
            width = ((info.width as f32 * scale) as u32).max(1);
            height = ((info.height as f32 * scale) as u32).max(1);
            ((info.width << 16) / width, (info.height << 16) / height)
        } else {
            (65536, 65536)
        };
        // The BMP header stores signed dimensions; reject unrepresentable output.
        if width > i32::MAX as u32 || height > i32::MAX as u32 || scale_x == 0 || scale_y == 0 {
            return None;
        }
        let width = width as usize;
        let stride = (width * if config.one_bit { 1 } else { 2 }).div_ceil(32) * 4;
        Some(Self {
            width,
            height,
            scale_x,
            scale_y,
            scaling,
            y: 0,
            next_y: scale_y,
            row: zeros(stride)?,
            gray: zeros(info.width as usize)?,
            errors: zeros(width.checked_add(4)?.checked_mul(3)?)?,
            accum: zeros(if scaling { width } else { 0 })?,
            counts: zeros(if scaling { width } else { 0 })?,
            error_row: 0,
            config,
        })
    }

    fn header(&self, output: &mut impl Output) -> bool {
        let colors = if self.config.one_bit { 2 } else { 4 };
        let mut header = [0u8; 70];
        let size = 54 + colors * 4;
        let Some(image_size) = (self.row.len() as u32)
            .checked_mul(self.height)
            .filter(|n| *n <= u32::MAX - 70)
        else {
            return false;
        };
        header[..2].copy_from_slice(b"BM");
        for (at, value) in [
            (2, image_size + size as u32),
            (10, size as u32),
            (14, 40),
            (18, self.width as u32),
            (22, (-(self.height as i32)) as u32),
            (34, image_size),
            (38, 2835),
            (42, 2835),
            (46, colors as u32),
            (50, colors as u32),
        ] {
            header[at..at + 4].copy_from_slice(&value.to_le_bytes());
        }
        header[26] = 1;
        header[28] = if self.config.one_bit { 1 } else { 2 };
        for i in 0..colors {
            header[54 + i * 4..57 + i * 4].fill((i * 255 / (colors - 1)) as u8);
        }
        output.write(&header[..size])
    }

    fn emit(&mut self, output: &mut impl Output) -> bool {
        self.row.fill(0);
        let stride = self.width + 4;
        let r0 = self.error_row * stride;
        let r1 = ((self.error_row + 1) % 3) * stride;
        let r2 = ((self.error_row + 2) % 3) * stride;
        for x in 0..self.width {
            let gray = if self.scaling {
                self.accum[x].checked_div(self.counts[x]).unwrap_or(0)
            } else {
                self.gray[x] as u32
            };
            let adjusted = (gray as i32 + self.errors[r0 + x + 2] as i32).clamp(0, 255);
            let (quantized, level) = if self.config.one_bit {
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
            let error = ((adjusted - level) >> 3) as i16;
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
                self.row[x / 8] |= quantized << (7 - x % 8);
            } else {
                self.row[x / 4] |= quantized << (6 - (x % 4) * 2);
            }
        }
        self.errors[r0..r0 + stride].fill(0);
        self.error_row = (self.error_row + 1) % 3;
        self.y += 1;
        let ok = output.write(&self.row);
        if self.y.is_multiple_of(8) {
            output.yield_rows();
        }
        ok
    }

    fn row(&mut self, row: Row<'_>, entries: usize, output: &mut impl Output) -> bool {
        let depth = row.depth as usize;
        let rgb = |p: &[u8], step: usize| -> u8 {
            ((p[0] as u32 * 25 + p[step] as u32 * 50 + p[step * 2] as u32 * 25) / 100) as u8
        };
        for (x, gray) in self.gray.iter_mut().enumerate() {
            let pixel = match row.pixel_type {
                0 => {
                    if depth >= 8 {
                        row.pixels[x * (depth / 8)]
                    } else {
                        let mask = (1 << depth) - 1;
                        (((row.pixels[x * depth / 8] as usize >> (8 - depth - (x * depth % 8)))
                            & mask)
                            * 255
                            / mask) as u8
                    }
                }
                2 | 6 => {
                    let step = depth / 8;
                    let channels = if row.pixel_type == 2 { 3 } else { 4 };
                    rgb(&row.pixels[x * channels * step..], step)
                }
                3 => {
                    let mask = (1 << depth) - 1;
                    let mut index =
                        (row.pixels[x * depth / 8] as usize >> (8 - depth - x * depth % 8)) & mask;
                    if index >= entries {
                        index = 0;
                    }
                    rgb(&row.palette[index * 3..], 1)
                }
                4 => row.pixels[x * 2 * (depth / 8)],
                _ => return false,
            };
            *gray = pixel;
        }
        if !self.scaling {
            return self.emit(output);
        }
        for x in 0..self.width {
            let start = (x as u32 * self.scale_x) >> 16;
            let end = ((x + 1) as u32 * self.scale_x) >> 16;
            let mut sum = 0;
            let mut count = 0;
            for sx in start..end.min(self.gray.len() as u32) {
                sum += self.gray[sx as usize] as u32;
                count += 1;
            }
            if count == 0 && start < self.gray.len() as u32 {
                sum = self.gray[start as usize] as u32;
                count = 1;
            }
            self.accum[x] += sum;
            self.counts[x] += count;
        }
        let source_y = (row.y as u32 + 1) << 16;
        while source_y >= self.next_y && self.y < self.height {
            if !self.emit(output) {
                return false;
            }
            self.next_y = (self.y + 1) * self.scale_y;
            if source_y >= self.next_y {
                continue;
            }
            self.accum.fill(0);
            self.counts.fill(0);
        }
        true
    }
}

pub fn convert(
    decoder: &mut Decoder,
    source: &mut impl Source,
    config: Config,
    output: &mut impl Output,
) -> bool {
    if !decoder.chunk(source) {
        return false;
    }
    let Some(mut bmp) = Bmp::new(decoder.info, config) else {
        return false;
    };
    if !bmp.header(output) {
        return false;
    }
    decoder.decode(source, |row, entries| bmp.row(row, entries, output))
}
