//! Integer image processing; codecs and storage handles remain with the caller.
extern crate alloc;
use alloc::vec::Vec;
pub mod cache;
pub mod frame;
pub mod probe;
use cache::{ByteSink, PixelCacheWriter};
use frame::{Target, Writer};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Config {
    pub x: i32,
    pub y: i32,
    pub max_width: i32,
    pub max_height: i32,
    pub screen_width: i32,
    pub screen_height: i32,
    pub crop_x: f32,
    pub crop_y: f32,
    pub exact: u8,
    pub dither: u8,
    pub alpha: u8,
    pub kind: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Plan {
    pub width: i32,
    pub height: i32,
    pub denominator: i32,
    pub max_rows: i32,
}
#[derive(Clone, Copy, PartialEq, Eq)]
enum Kind {
    Png,
    Jpeg,
    Gif,
}

/// # Safety
/// Each target lends exclusive writable memory until the next target call or the
/// enclosing pipeline call returns. ByteSink operations must preserve that loan.
pub unsafe trait Sink: ByteSink {
    /// The returned target remains exclusively writable until the next target callback; IO preserves it.
    fn target(&mut self) -> Target;
}

pub struct Row<'a> {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub stride: i32,
    pub pixels: &'a [u8],
    pub palette: &'a [u8],
    pub pixel_type: u8,
    pub depth: u8,
    pub has_alpha: bool,
    pub transparent: u32,
}

pub struct Pipeline {
    config: Config,
    kind: Kind,
    pub plan: Plan,
    src_width: i32,
    src_height: i32,
    crop_left: i32,
    crop_top: i32,
    visible_width: i32,
    visible_height: i32,
    fine_x: i32,
    fine_y: i32,
    inv_x: i32,
    inv_y: i32,
    last_y: i32,
    next_y: i32,
    line: Vec<u8>,
    extra: Vec<u8>,
    cache: PixelCacheWriter,
    failed: bool,
}

impl Pipeline {
    pub fn new(config: Config, width: i32, height: i32, progressive: bool) -> Option<Self> {
        if width <= 0
            || height <= 0
            || width > 32767
            || height > 32767
            || width as i64 * height as i64 > 8388608
            || !config.crop_x.is_finite()
            || !config.crop_y.is_finite()
        {
            return None;
        }
        let kind = match config.kind {
            0 => Kind::Png,
            1 => Kind::Jpeg,
            2 => Kind::Gif,
            _ => return None,
        };
        let crop_x = if kind == Kind::Gif {
            0.0
        } else {
            config.crop_x.clamp(0.0, 0.99)
        };
        let crop_y = if kind == Kind::Gif {
            0.0
        } else {
            config.crop_y.clamp(0.0, 0.99)
        };
        let mut crop_left = (width as f32 * crop_x / 2.0) as i32;
        let mut crop_top = (height as f32 * crop_y / 2.0) as i32;
        let mut visible_width = width - 2 * crop_left;
        let mut visible_height = height - 2 * crop_top;
        let (dw, dh, scale) = if config.exact != 0 && config.max_width > 0 && config.max_height > 0
        {
            (
                config.max_width,
                config.max_height,
                config.max_width as f32 / visible_width as f32,
            )
        } else {
            let sx = if kind == Kind::Jpeg
                && (config.max_width <= 0 || visible_width <= config.max_width)
            {
                1.0
            } else {
                config.max_width as f32 / visible_width as f32
            };
            let sy = if kind == Kind::Jpeg
                && (config.max_height <= 0 || visible_height <= config.max_height)
            {
                1.0
            } else {
                config.max_height as f32 / visible_height as f32
            };
            let scale = (if sx < sy { sx } else { sy }).min(1.0);
            (
                (visible_width as f32 * scale) as i32,
                (visible_height as f32 * scale) as i32,
                scale,
            )
        };
        if dw <= 0
            || dh <= 0
            || dw > 32767
            || dh > 32767
            || config.x < 0
            || config.y < 0
            || config.x > 32767
            || config.y > 32767
        {
            return None;
        }
        let denominator = if kind != Kind::Jpeg {
            1
        } else if progressive || scale <= 0.125 {
            8
        } else if scale <= 0.25 {
            4
        } else if scale <= 0.5 {
            2
        } else {
            1
        };
        if kind == Kind::Jpeg {
            let sw = (width + denominator - 1) / denominator;
            let sh = (height + denominator - 1) / denominator;
            crop_left = (sw as f32 * crop_x / 2.0) as i32;
            crop_top = (sh as f32 * crop_y / 2.0) as i32;
            visible_width = sw - 2 * crop_left;
            visible_height = sh - 2 * crop_top;
        }
        let fine_x = (dw as i64 * 65536 / visible_width as i64) as i32;
        let fine_y = (dh as i64 * 65536 / visible_height as i64) as i32;
        Some(Self {
            config,
            kind,
            plan: Plan {
                width: dw,
                height: dh,
                denominator,
                max_rows: if kind == Kind::Jpeg {
                    ((16i64 * fine_y as i64) >> 16) as i32 + 2
                } else {
                    1
                },
            },
            src_width: width,
            src_height: height,
            crop_left,
            crop_top,
            visible_width,
            visible_height,
            fine_x,
            fine_y,
            inv_x: (visible_width as i64 * 65536 / dw as i64) as i32,
            inv_y: (visible_height as i64 * 65536 / dh as i64) as i32,
            last_y: -1,
            next_y: 0,
            line: Vec::new(),
            extra: Vec::new(),
            cache: PixelCacheWriter::default(),
            failed: false,
        })
    }
    pub fn prepare(&mut self) -> bool {
        let bytes = match self.kind {
            Kind::Png => self.src_width as usize * if self.config.alpha != 0 { 2 } else { 1 },
            Kind::Gif => self.src_width as usize,
            Kind::Jpeg => return true,
        };
        if self.line.try_reserve_exact(bytes).is_err() {
            return false;
        }
        self.line.resize(bytes, 0);
        if self.kind == Kind::Gif {
            if self.extra.try_reserve_exact(bytes).is_err() {
                return false;
            }
            self.extra.resize(bytes, 255);
        }
        true
    }
    pub fn start_cache(&mut self, sink: &mut impl Sink) -> bool {
        self.cache
            .begin(sink, self.plan.width, self.plan.height, self.plan.max_rows)
    }
    pub fn abort(&mut self, sink: &mut impl Sink) {
        self.cache.abort(sink);
    }
    pub fn finish(&mut self, sink: &mut impl Sink) -> bool {
        if self.kind == Kind::Gif {
            self.gaps(self.src_height, sink);
        }
        if self.failed {
            self.cache.abort(sink);
            return false;
        }
        if self.cache.active {
            self.cache.finish(sink)
        } else {
            self.cache.abort(sink);
            false
        }
    }
    pub fn success(&self) -> bool {
        !self.failed
    }
    pub fn row(&mut self, row: Row<'_>, sink: &mut impl Sink) -> bool {
        if self.failed {
            return false;
        }
        let ok = match self.kind {
            Kind::Png => self.png(row, sink),
            Kind::Jpeg => self.jpeg(row, sink),
            Kind::Gif => self.gif(row, sink),
        };
        if !ok {
            self.failed = true;
        }
        ok
    }
    fn range(&self, y: i32) -> (i32, i32) {
        let first = y * self.plan.height / self.visible_height;
        let end = if self.plan.height > self.visible_height {
            (y + 1) * self.plan.height / self.visible_height
        } else {
            first + 1
        };
        (first.max(self.last_y + 1), end.min(self.plan.height))
    }
    fn render_line(&mut self, first: i32, end: i32, sink: &mut impl Sink) {
        if first >= end || first >= self.plan.height {
            return;
        }
        // SAFETY: Sink contract lends exclusive access until the next target callback; IO preserves it.
        let mut writer = unsafe { Writer::new(sink.target()) };
        for y in first..end {
            self.last_y = y;
            let out_y = self.config.y + y;
            if out_y >= self.config.screen_height {
                continue;
            }
            writer.begin_row(out_y);
            if self.cache.active && !self.cache.advance(sink, y) && self.kind == Kind::Gif {
                self.failed = true;
                return;
            }
            let mut sx = self.crop_left;
            let mut error = 0;
            for x in 0..self.plan.width {
                let out_x = self.config.x + x;
                if out_x < self.config.screen_width {
                    let alpha = if self.kind == Kind::Png && self.config.alpha != 0 {
                        self.line[self.src_width as usize + sx as usize]
                    } else {
                        255
                    };
                    if alpha >= 8
                        && alpha > frame::BAYER[((out_y & 3) * 4 + (out_x & 3)) as usize] * 16
                    {
                        let gray = self.line[sx as usize];
                        let value = if self.config.dither != 0 {
                            frame::dither(gray, out_x, out_y)
                        } else if self.kind == Kind::Png {
                            gray >> 6
                        } else {
                            gray / 85
                        };
                        writer.pixel(
                            out_x,
                            value,
                            self.kind == Kind::Png && self.config.alpha != 0,
                        );
                        if self.cache.active {
                            self.cache.pixel(x, y, value);
                        }
                    }
                }
                error += self.visible_width;
                while error >= self.plan.width {
                    error -= self.plan.width;
                    sx += 1;
                }
            }
        }
    }
    fn png(&mut self, row: Row<'_>, sink: &mut impl Sink) -> bool {
        if row.y < self.crop_top || row.y >= self.crop_top + self.visible_height {
            return true;
        }
        let (first, end) = self.range(row.y - self.crop_top);
        if first >= end || first >= self.plan.height {
            return true;
        }
        let width = self.src_width as usize;
        let retain = self.config.alpha != 0;
        if self.line.len() < width * (if retain { 2 } else { 1 })
            || ![1, 2, 4, 8].contains(&row.depth)
        {
            return false;
        }
        if row.pixel_type == 0 && row.depth == 8 && !row.has_alpha && !retain {
            let Some(pixels) = row.pixels.get(..width) else {
                return false;
            };
            self.line[..width].copy_from_slice(pixels);
            self.render_line(first, end, sink);
            return true;
        }
        for x in 0..width {
            let sample = |x: usize| -> Option<u8> {
                if row.depth == 8 {
                    return row.pixels.get(x).copied();
                }
                let bit = x * row.depth as usize;
                row.pixels.get(bit / 8).map(|b| {
                    (b >> (8 - row.depth - (bit & 7) as u8)) & ((1u16 << row.depth) - 1) as u8
                })
            };
            let rgb = |p: &[u8]| -> u8 {
                ((p[0] as u32 * 77 + p[1] as u32 * 150 + p[2] as u32 * 29) >> 8) as u8
            };
            let expand = |s: u8| -> u8 {
                if row.depth == 8 {
                    s
                } else {
                    (s as u32 * 255 / ((1u32 << row.depth) - 1)) as u8
                }
            };
            let (gray, alpha) = match row.pixel_type {
                0 => {
                    let Some(s) = sample(x) else {
                        return false;
                    };
                    (
                        expand(s),
                        if row.has_alpha && s == row.transparent as u8 {
                            0
                        } else {
                            255
                        },
                    )
                }
                2 => {
                    let Some(p) = row.pixels.get(x * 3..x * 3 + 3) else {
                        return false;
                    };
                    let color = (p[0] as u32) << 16 | (p[1] as u32) << 8 | p[2] as u32;
                    (
                        rgb(p),
                        if row.has_alpha && color == row.transparent {
                            0
                        } else {
                            255
                        },
                    )
                }
                3 => {
                    let Some(s) = sample(x) else {
                        return false;
                    };
                    if row.palette.is_empty() {
                        (expand(s), 255)
                    } else {
                        let i = s as usize;
                        let Some(p) = row.palette.get(i * 3..i * 3 + 3) else {
                            return false;
                        };
                        let alpha = if row.has_alpha {
                            let Some(&a) = row.palette.get(768 + i) else {
                                return false;
                            };
                            a
                        } else {
                            255
                        };
                        (rgb(p), alpha)
                    }
                }
                4 => {
                    let Some(p) = row.pixels.get(x * 2..x * 2 + 2) else {
                        return false;
                    };
                    (p[0], p[1])
                }
                6 => {
                    let Some(p) = row.pixels.get(x * 4..x * 4 + 4) else {
                        return false;
                    };
                    (rgb(p), p[3])
                }
                _ => (128, 255),
            };
            self.line[x] = if retain || alpha == 255 {
                gray
            } else {
                ((gray as u32 * alpha as u32 + 255 * (255 - alpha as u32)) / 255) as u8
            };
            if retain {
                self.line[width + x] = alpha;
            }
        }
        self.render_line(first, end, sink);
        true
    }
    fn gaps(&mut self, until: i32, sink: &mut impl Sink) {
        while !self.failed && self.next_y < until && self.next_y < self.src_height {
            self.line.copy_from_slice(&self.extra);
            let (first, end) = self.range(self.next_y);
            self.render_line(first, end, sink);
            self.next_y += 1;
        }
    }
    fn gif(&mut self, row: Row<'_>, sink: &mut impl Sink) -> bool {
        if row.y < 0
            || row.y >= self.src_height
            || row.width < 0
            || row.pixels.len() < row.width as usize
            || self.line.len() != self.src_width as usize
        {
            return false;
        }
        self.gaps(row.y, sink);
        if self.failed {
            return false;
        }
        self.line.copy_from_slice(&self.extra);
        for x in row.x.max(0)..(row.x + row.width).min(self.src_width) {
            let index = row.pixels[(x - row.x) as usize];
            if row.has_alpha && index == row.transparent as u8 {
                continue;
            }
            let i = index as usize * 3;
            let gray = if row.palette.is_empty() {
                0
            } else {
                let Some(p) = row.palette.get(i..i + 3) else {
                    return false;
                };
                ((p[0] as u32 * 77 + p[1] as u32 * 150 + p[2] as u32 * 29) >> 8) as u8
            };
            self.line[x as usize] = gray;
        }
        let (first, end) = self.range(row.y);
        self.render_line(first, end, sink);
        self.next_y = row.y + 1;
        !self.failed
    }
    fn jpeg(&mut self, row: Row<'_>, sink: &mut impl Sink) -> bool {
        if row.stride <= 0 || row.width <= 0 || row.height <= 0 {
            return true;
        }
        if row.width > row.stride || row.pixels.len() < (row.stride as usize * row.height as usize)
        {
            return false;
        }
        let bx = row.x - self.crop_left;
        let by = row.y - self.crop_top;
        let x_end = bx + row.width;
        let y_end = by + row.height;
        let mut y0 = ((by as i64 * self.fine_y as i64) >> 16) as i32;
        let mut y1 = if y_end >= self.visible_height {
            self.plan.height
        } else {
            ((y_end as i64 * self.fine_y as i64) >> 16) as i32
        };
        let mut x0 = ((bx as i64 * self.fine_x as i64) >> 16) as i32;
        let mut x1 = if x_end >= self.visible_width {
            self.plan.width
        } else {
            ((x_end as i64 * self.fine_x as i64) >> 16) as i32
        };
        y0 = y0.max(-self.config.y);
        y1 = y1.min(
            self.plan
                .height
                .min(self.config.screen_height - self.config.y),
        );
        x0 = x0.max(-self.config.x);
        x1 = x1.min(
            self.plan
                .width
                .min(self.config.screen_width - self.config.x),
        );
        if y0 >= y1 || x0 >= x1 {
            return true;
        }
        // SAFETY: Sink contract lends the active renderer target.
        let mut writer = unsafe { Writer::new(sink.target()) };
        if self.cache.active {
            self.cache.advance(sink, y0);
        }
        let exact = self.fine_x == 65536 && self.fine_y == 65536;
        let bilinear = self.fine_x > 65536 && self.fine_y > 65536;
        for y in y0..y1 {
            let oy = self.config.y + y;
            writer.begin_row(oy);
            let sy = y.wrapping_mul(self.inv_y);
            let ly = if exact {
                y - by
            } else {
                ((sy >> 16) - by).clamp(0, row.height - 1)
            };
            let ly1 = (((sy >> 16) - by) + 1).clamp(0, row.height - 1);
            let fy = sy & 65535;
            for x in x0..x1 {
                let ox = self.config.x + x;
                let sx = x.wrapping_mul(self.inv_x);
                let lx = if exact {
                    x - bx
                } else {
                    ((sx >> 16) - bx).clamp(0, row.width - 1)
                };
                let gray = if bilinear {
                    let lx1 = (((sx >> 16) - bx) + 1).clamp(0, row.width - 1);
                    let fx = sx & 65535;
                    let top = (row.pixels[(ly * row.stride + lx) as usize] as i32 * (65536 - fx)
                        + row.pixels[(ly * row.stride + lx1) as usize] as i32 * fx)
                        >> 16;
                    let bot = (row.pixels[(ly1 * row.stride + lx) as usize] as i32 * (65536 - fx)
                        + row.pixels[(ly1 * row.stride + lx1) as usize] as i32 * fx)
                        >> 16;
                    ((top * (65536 - fy) + bot * fy) >> 16) as u8
                } else {
                    let Some(&gray) = row.pixels.get((ly * row.stride + lx) as usize) else {
                        return false;
                    };
                    gray
                };
                let value = if self.config.dither != 0 {
                    frame::dither(gray, ox, oy)
                } else {
                    gray / 85
                };
                writer.pixel(ox, value, false);
                if self.cache.active {
                    self.cache.pixel(x, y, value);
                }
            }
        }
        true
    }
}
