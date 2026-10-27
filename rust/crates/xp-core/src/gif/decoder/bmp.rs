//! Streaming cover/thumbnail conversion with the existing integer box filter and Atkinson thresholds.
use super::alloc::vec::Vec;
use super::{zeros, Error, Info, Result, Rows};

pub trait Write {
    fn write(&mut self, bytes: &[u8]) -> bool;
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Config {
    pub width: i32,
    pub height: i32,
    pub one_bit: u8,
    pub crop: u8,
    pub original_thresholds: u8,
}

pub struct Bmp<'a> {
    output: &'a mut dyn Write,
    config: Config,
    source_x: usize,
    source_y: usize,
    source_width: usize,
    source_height: usize,
    canvas_height: usize,
    width: usize,
    height: usize,
    scale_x: u32,
    scale_y: u32,
    next_source_y: usize,
    out_y: usize,
    next_y_end: u32,
    gray: Vec<u8>,
    scaled: Vec<u8>,
    accum: Vec<u32>,
    packed: Vec<u8>,
    errors: Vec<i16>,
    error_row: usize,
}
impl<'a> Bmp<'a> {
    pub fn new(info: Info, config: Config, output: &'a mut dyn Write) -> Result<Self> {
        let sw = info.width as usize;
        let sh = info.height as usize;
        if sw == 0
            || sh == 0
            || sw > 3072
            || sh > 3072
            || config.width <= 0
            || config.height <= 0
            || config.width > 3072
            || config.height > 3072
        {
            return Err(Error::Format);
        }
        let (mut width, mut height) = (config.width as usize, config.height as usize);
        let (mut source_width, mut source_height) = (sw, sh);
        let (mut source_x, mut source_y) = (0, 0);
        if config.crop != 0 {
            if sw * height > sh * width {
                source_width = (sh * width / height).max(1);
                source_x = (sw - source_width) / 2;
            } else {
                source_height = (sw * height / width).max(1);
                source_y = (sh - source_height) / 2;
            }
        } else if sw * height > sh * width {
            height = (sh * width / sw).max(1);
        } else {
            width = (sw * height / sh).max(1);
        }
        let bits = if config.one_bit != 0 { 1 } else { 2 };
        let row_bytes = (width * bits).div_ceil(32) * 4;
        let colors = 1 << bits;
        let header_size = 54 + colors * 4;
        let image_size = row_bytes * height;
        if image_size + header_size > 4 * 1024 * 1024 {
            return Err(Error::Format);
        }
        let mut header = [0u8; 70];
        header[..2].copy_from_slice(b"BM");
        for (offset, value) in [
            (2, (image_size + header_size) as u32),
            (10, header_size as u32),
            (14, 40),
            (18, width as u32),
            (22, (-(height as i32)) as u32),
            (34, image_size as u32),
            (38, 2835),
            (42, 2835),
            (46, colors as u32),
            (50, colors as u32),
        ] {
            header[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        header[26] = 1;
        header[28] = bits as u8;
        for i in 0..colors {
            header[54 + i * 4..57 + i * 4].fill((i * 255 / (colors - 1)) as u8);
        }
        if !output.write(&header[..header_size]) {
            return Err(Error::Output);
        }
        let scaling = source_width != width || source_height != height;
        Ok(Self {
            output,
            config,
            source_x,
            source_y,
            source_width,
            source_height,
            canvas_height: sh,
            width,
            height,
            scale_x: ((source_width << 16) / width) as u32,
            scale_y: ((source_height << 16) / height) as u32,
            next_source_y: 0,
            out_y: 0,
            next_y_end: ((source_height << 16) / height) as u32,
            gray: zeros(sw)?,
            scaled: zeros(if scaling { width } else { 0 })?,
            accum: zeros(if scaling { width * 2 } else { 0 })?,
            packed: zeros(row_bytes)?,
            errors: zeros((width + 4) * 3)?,
            error_row: 0,
        })
    }
    fn emit(&mut self, scaled: bool) -> bool {
        self.packed.fill(0);
        let row_size = self.width + 4;
        let a = self.error_row * row_size;
        let b = ((self.error_row + 1) % 3) * row_size;
        let c = ((self.error_row + 2) % 3) * row_size;
        for x in 0..self.width {
            let gray = if scaled {
                self.scaled[x]
            } else {
                self.gray[self.source_x + x]
            };
            let adjusted = (gray as i32 + self.errors[a + x + 2] as i32).clamp(0, 255);
            let (quantized, value) = if self.config.one_bit != 0 {
                if adjusted < 128 {
                    (0, 0)
                } else {
                    (1, 255)
                }
            } else if self.config.original_thresholds != 0 {
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
            for index in [
                a + x + 3,
                a + x + 4,
                b + x + 1,
                b + x + 2,
                b + x + 3,
                c + x + 2,
            ] {
                self.errors[index] += error;
            }
            if self.config.one_bit != 0 {
                self.packed[x / 8] |= quantized << (7 - x % 8);
            } else {
                self.packed[x / 4] |= quantized << (6 - (x % 4) * 2);
            }
        }
        self.error_row = (self.error_row + 1) % 3;
        self.errors[a..a + row_size].fill(0);
        self.output.write(&self.packed)
    }
    fn canvas_row(&mut self, y: usize) -> bool {
        if y < self.source_y || y >= self.source_y + self.source_height {
            return true;
        }
        if self.scaled.is_empty() {
            return self.emit(false);
        }
        for x in 0..self.width {
            let begin = ((x as u32 * self.scale_x) >> 16) as usize;
            let end = (((x + 1) as u32 * self.scale_x) >> 16) as usize;
            let mut sum = 0;
            let mut count = 0;
            for sx in begin..end.min(self.source_width) {
                sum += self.gray[self.source_x + sx] as u32;
                count += 1;
            }
            if count == 0 && begin < self.source_width {
                sum = self.gray[self.source_x + begin] as u32;
                count = 1;
            }
            self.accum[x] += sum;
            self.accum[self.width + x] += count;
        }
        let end = ((y - self.source_y + 1) << 16) as u32;
        while end >= self.next_y_end && self.out_y < self.height {
            for x in 0..self.width {
                let count = self.accum[self.width + x];
                self.scaled[x] = self.accum[x].checked_div(count).unwrap_or(255) as u8;
            }
            if !self.emit(true) {
                return false;
            }
            self.out_y += 1;
            self.next_y_end = (self.out_y as u32 + 1) * self.scale_y;
            if end < self.next_y_end {
                self.accum.fill(0);
            }
        }
        true
    }
    fn gaps(&mut self, until: usize) -> bool {
        self.gray.fill(255);
        while self.next_source_y < until && self.next_source_y < self.canvas_height {
            if !self.canvas_row(self.next_source_y) {
                return false;
            }
            self.next_source_y += 1;
        }
        true
    }
    pub fn finish(&mut self) -> bool {
        self.gaps(self.canvas_height)
    }
}
impl Rows for Bmp<'_> {
    fn row(
        &mut self,
        info: Info,
        y: u16,
        pixels: &[u8],
        palette: &[u8],
        transparent: Option<u8>,
    ) -> bool {
        let y = info.y as usize + y as usize;
        if y >= self.canvas_height || !self.gaps(y) {
            return false;
        }
        for (dest, &index) in self.gray.iter_mut().skip(info.x as usize).zip(pixels) {
            if Some(index) != transparent {
                *dest = palette[index as usize];
            }
        }
        let result = self.canvas_row(y);
        self.next_source_y = y + 1;
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Output;
    impl Write for Output {
        fn write(&mut self, _: &[u8]) -> bool {
            true
        }
    }
    #[test]
    fn cropped_tall_and_wide_sources_stay_bounded() {
        for (width, height) in [(1, 3072), (3072, 1), (640, 480)] {
            for (target_w, target_h) in [(480, 800), (800, 480)] {
                let mut output = Output;
                let bmp = Bmp::new(
                    Info {
                        width,
                        height,
                        ..Info::default()
                    },
                    Config {
                        width: target_w,
                        height: target_h,
                        crop: 1,
                        one_bit: 0,
                        original_thresholds: 0,
                    },
                    &mut output,
                )
                .unwrap();
                assert_eq!(
                    (bmp.width, bmp.height),
                    (target_w as usize, target_h as usize)
                );
                assert!(bmp.source_x + bmp.source_width <= width as usize);
                assert!(bmp.source_y + bmp.source_height <= height as usize);
            }
        }
    }
    #[test]
    fn fit_retains_the_whole_source() {
        let mut output = Output;
        let bmp = Bmp::new(
            Info {
                width: 640,
                height: 480,
                ..Info::default()
            },
            Config {
                width: 240,
                height: 400,
                crop: 0,
                one_bit: 0,
                original_thresholds: 0,
            },
            &mut output,
        )
        .unwrap();
        assert_eq!((bmp.width, bmp.height), (240, 180));
        assert_eq!((bmp.source_width, bmp.source_height), (640, 480));
    }
    #[test]
    fn invalid_targets_are_rejected() {
        for (width, height) in [(0, 800), (480, -1), (480, 32767)] {
            let mut output = Output;
            assert!(Bmp::new(
                Info {
                    width: 1,
                    height: 1,
                    ..Info::default()
                },
                Config {
                    width,
                    height,
                    crop: 1,
                    one_bit: 0,
                    original_thresholds: 0
                },
                &mut output
            )
            .is_err());
        }
    }
}
