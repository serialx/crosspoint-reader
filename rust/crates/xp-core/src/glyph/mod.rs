//! Borrowed glyph rasterization. Coordinates and packed pixels follow GfxRenderer.
mod source;
use crate::font::raw::Glyph;
pub use source::*;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Frame {
    pub x: i32,
    pub y: i32,
    pub dx_x: i32,
    pub dx_y: i32,
    pub dy_x: i32,
    pub dy_y: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Clip {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}
/// C++ selects the framebuffer or strip before lending this descriptor.
#[repr(C)]
pub struct GlyphTarget {
    pub buffer: *mut u8,
    pub buffer_len: u32,
    pub width: i32,
    pub height: i32,
    pub stride: i32,
    pub origin_y: i32,
    pub rows: i32,
    pub clip: Clip,
    pub orientation: u8,
    pub plane: u8,
    pub absolute: u8,
    pub strip: u8,
    pub scanning: u8,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Kind {
    Normal,
    Rotated,
    Coverage,
    Bilinear,
}
#[repr(C)]
pub struct Draw {
    pub glyph: Glyph,
    pub x: i32,
    pub y: i32,
    pub ascender: i32,
    pub scale_num: i32,
    pub scale_den: i32,
    pub scale256: i32,
    pub kind: u8,
    pub two_bit: u8,
    pub state: u8,
    pub solid: u8,
}
fn clip_axis(base: i32, step: i32, lower: i32, upper: i32, start: &mut i32, end: &mut i32) {
    if step > 0 {
        *start = (*start).max(lower - base);
        *end = (*end).min(upper - base);
    } else {
        *start = (*start).max(base - upper + 1);
        *end = (*end).min(base - lower + 1);
    }
}
fn clip_rect(frame: Frame, bounds: Clip, clip: &mut Clip) {
    if frame.dx_x != 0 {
        clip_axis(
            frame.x,
            frame.dx_x,
            bounds.left,
            bounds.right,
            &mut clip.left,
            &mut clip.right,
        );
        clip_axis(
            frame.y,
            frame.dy_y,
            bounds.top,
            bounds.bottom,
            &mut clip.top,
            &mut clip.bottom,
        );
    } else {
        clip_axis(
            frame.x,
            frame.dy_x,
            bounds.left,
            bounds.right,
            &mut clip.top,
            &mut clip.bottom,
        );
        clip_axis(
            frame.y,
            frame.dx_y,
            bounds.top,
            bounds.bottom,
            &mut clip.left,
            &mut clip.right,
        );
    }
}
impl GlyphTarget {
    pub fn valid(&self) -> bool {
        self.width > 0
            && self.width <= 65535
            && self.height > 0
            && self.height <= 65535
            && self.stride >= (self.width + 7) / 8
            && self.stride <= 8192
            && self.stride * self.height <= i32::MAX / 8
            && self.origin_y >= 0
            && self.rows >= 0
            && self.rows <= self.height
            && self.origin_y <= self.height - self.rows
            && self.buffer_len >= (self.stride * self.rows) as u32
            && self.buffer_len as u64 <= isize::MAX as u64
            && self.orientation < 4
            && self.plane < 3
            && [
                self.clip.left,
                self.clip.top,
                self.clip.right,
                self.clip.bottom,
            ]
            .iter()
            .all(|v| (-1_000_000..=1_000_000).contains(v))
    }
    #[inline]
    fn rotate(&self, x: i32, y: i32) -> (i32, i32) {
        match self.orientation {
            0 => (y, self.height - 1 - x),
            1 => (self.width - 1 - x, self.height - 1 - y),
            2 => (self.width - 1 - y, x),
            _ => (x, y),
        }
    }
    pub fn intersects_strip(&self, x0: i32, y0: i32, x1: i32, y1: i32) -> bool {
        if self.strip == 0 {
            return true;
        }
        let (_, ay) = self.rotate(x0, y0);
        let (_, by) = self.rotate(x1, y1);
        !(ay.max(by) < self.origin_y || ay.min(by) >= self.origin_y + self.rows)
    }
}
pub struct Surface<'a> {
    target: &'a GlyphTarget,
    buffer: &'a mut [u8],
}
impl<'a> Surface<'a> {
    pub fn new(target: &'a GlyphTarget, buffer: &'a mut [u8]) -> Option<Self> {
        (target.valid() && buffer.len() >= target.buffer_len as usize)
            .then_some(Self { target, buffer })
    }
    #[inline(always)]
    fn paint(&mut self, destination: i32, ink: u8, levels: u8, clear: bool) {
        if levels & (1 << ink) == 0 {
            return;
        }
        let mask = 0x80 >> (destination & 7);
        // Destination is bounded by the physical clip and validated stride.
        let byte = &mut self.buffer[(destination >> 3) as usize];
        if clear {
            *byte &= !mask;
        } else {
            *byte |= mask;
        }
    }
    fn physical_frame(&self, frame: Frame) -> Frame {
        let (x, y) = self.target.rotate(frame.x, frame.y);
        let (xx, xy) = self
            .target
            .rotate(frame.x + frame.dx_x, frame.y + frame.dx_y);
        let (yx, yy) = self
            .target
            .rotate(frame.x + frame.dy_x, frame.y + frame.dy_y);
        Frame {
            x,
            y,
            dx_x: xx - x,
            dx_y: xy - y,
            dy_x: yx - x,
            dy_y: yy - y,
        }
    }
    fn bitmap(
        &mut self,
        bitmap: &[u8],
        width: i32,
        height: i32,
        frame: Frame,
        two_bit: bool,
        state: bool,
    ) {
        let mut clip = Clip {
            left: 0,
            top: 0,
            right: width,
            bottom: height,
        };
        clip_rect(frame, self.target.clip, &mut clip);
        let frame = self.physical_frame(frame);
        let t = self.target;
        clip_rect(
            frame,
            Clip {
                left: 0,
                top: t.origin_y,
                right: t.width,
                bottom: t.origin_y + t.rows,
            },
            &mut clip,
        );
        if clip.left >= clip.right || clip.top >= clip.bottom {
            return;
        }
        let plane = if t.absolute != 0 { 0 } else { t.plane };
        let levels = if !two_bit {
            2
        } else {
            match plane {
                0 => 0x0e,
                2 => 0x06,
                _ => 0x04,
            }
        };
        let clear = if two_bit && plane != 0 { false } else { state };
        let stride = t.stride * 8;
        let step_x = frame.dx_y * stride + frame.dx_x;
        let step_y = frame.dy_y * stride + frame.dy_x;
        let start_x = frame.x + clip.left * frame.dx_x + clip.top * frame.dy_x;
        let start_y = frame.y + clip.left * frame.dx_y + clip.top * frame.dy_y;
        let mut row = (start_y - t.origin_y) * stride + start_x;
        for y in clip.top..clip.bottom {
            let mut source = y * width + clip.left;
            let mut dest = row;
            let mut remaining = clip.right - clip.left;
            if two_bit {
                while remaining > 0 && source & 3 != 0 {
                    self.paint(dest, raw(bitmap, source, true), levels, clear);
                    source += 1;
                    dest += step_x;
                    remaining -= 1;
                }
                while remaining >= 4 {
                    let packed = bitmap[(source >> 2) as usize];
                    self.paint(dest, packed >> 6, levels, clear);
                    self.paint(dest + step_x, (packed >> 4) & 3, levels, clear);
                    self.paint(dest + 2 * step_x, (packed >> 2) & 3, levels, clear);
                    self.paint(dest + 3 * step_x, packed & 3, levels, clear);
                    source += 4;
                    dest += 4 * step_x;
                    remaining -= 4;
                }
            }
            while remaining > 0 {
                self.paint(dest, raw(bitmap, source, two_bit), levels, clear);
                source += 1;
                dest += step_x;
                remaining -= 1;
            }
            row += step_y;
        }
    }
    fn pixel(&mut self, x: i32, y: i32, state: bool) {
        let t = self.target;
        if x < t.clip.left || x >= t.clip.right || y < t.clip.top || y >= t.clip.bottom {
            return;
        }
        let (px, py) = t.rotate(x, y);
        if px < 0 || px >= t.width || py < t.origin_y || py >= t.origin_y + t.rows {
            return;
        }
        self.paint((py - t.origin_y) * t.stride * 8 + px, 1, 2, state);
    }
    fn fill(&mut self, x: i32, y: i32, width: i32, height: i32, state: bool) {
        let t = self.target;
        if width <= 0 || height <= 0 || t.scanning != 0 {
            return;
        }
        let (sw, sh) = if t.orientation & 1 == 0 {
            (t.height, t.width)
        } else {
            (t.width, t.height)
        };
        let left = 0.max(x).max(t.clip.left);
        let top = 0.max(y).max(t.clip.top);
        let right = sw.min(x + width).min(t.clip.right);
        let bottom = sh.min(y + height).min(t.clip.bottom);
        if left >= right || top >= bottom {
            return;
        }
        let (ax, ay) = t.rotate(left, top);
        let (bx, by) = t.rotate(right - 1, bottom - 1);
        let x0 = ax.min(bx);
        let x1 = ax.max(bx);
        let y0 = ay.min(by).max(t.origin_y);
        let y1 = ay.max(by).min(t.origin_y + t.rows - 1);
        let start = (x0 >> 3) as usize;
        let end = (x1 >> 3) as usize;
        let head = 0xffu8 >> (x0 & 7);
        let tail = 0xffu8 << (7 - (x1 & 7));
        for py in y0..=y1 {
            let row = (py - t.origin_y) as usize * t.stride as usize;
            if start == end {
                apply(&mut self.buffer[row + start], head & tail, state);
            } else {
                apply(&mut self.buffer[row + start], head, state);
                self.buffer[row + start + 1..row + end].fill(if state { 0 } else { 255 });
                apply(&mut self.buffer[row + end], tail, state);
            }
        }
    }
    /// Invalid descriptors return false before any writes. No storage is retained.
    pub fn draw(&mut self, bitmap: &[u8], d: &Draw) -> bool {
        let kind = match d.kind {
            0 => Kind::Normal,
            1 => Kind::Rotated,
            2 => Kind::Coverage,
            3 => Kind::Bilinear,
            _ => return false,
        };
        if ![d.x, d.y, d.ascender]
            .iter()
            .all(|v| (-1_000_000..=1_000_000).contains(v))
        {
            return false;
        }
        let g = d.glyph;
        let w = i32::from(g.width);
        let h = i32::from(g.height);
        let two = d.two_bit != 0;
        let state = d.state != 0;
        if d.solid == 0 && bitmap.len() < ((w * h * (if two { 2 } else { 1 }) + 7) / 8) as usize {
            return false;
        }
        match kind {
            Kind::Normal | Kind::Rotated => {
                let frame = match kind {
                    Kind::Rotated => Frame {
                        x: d.x + d.ascender - i32::from(g.top),
                        y: d.y - i32::from(g.left),
                        dx_x: 0,
                        dx_y: -1,
                        dy_x: 1,
                        dy_y: 0,
                    },
                    _ => Frame {
                        x: d.x + i32::from(g.left),
                        y: d.y - i32::from(g.top),
                        dx_x: 1,
                        dx_y: 0,
                        dy_x: 0,
                        dy_y: 1,
                    },
                };
                let (x0, y0, x1, y1) = if matches!(kind, Kind::Rotated) {
                    (frame.x, frame.y - (w - 1), frame.x + h - 1, frame.y)
                } else {
                    (frame.x, frame.y, frame.x + w - 1, frame.y + h - 1)
                };
                if !self.target.intersects_strip(x0, y0, x1, y1) {
                    return true;
                }
                if d.solid != 0 {
                    self.fill(x0, y0, x1 - x0 + 1, y1 - y0 + 1, state);
                } else {
                    self.bitmap(bitmap, w, h, frame, two, state);
                }
            }
            Kind::Coverage => {
                let n = d.scale_num;
                let den = d.scale_den;
                if !(1..=256).contains(&n) || !(1..=256).contains(&den) {
                    return false;
                }
                let dw = (w * n + den - 1) / den;
                let dh = (h * n + den - 1) / den;
                let x = d.x + i32::from(g.left) * n / den;
                let y = d.y - i32::from(g.top) * n / den;
                if d.solid != 0 {
                    self.fill(x, y, dw, dh, state);
                    return true;
                }
                for dy in 0..dh {
                    let sy0 = dy * den / n;
                    let sy1 = h.min((sy0 + 1).max(((dy + 1) * den + n - 1) / n));
                    for dx in 0..dw {
                        let sx0 = dx * den / n;
                        let sx1 = w.min((sx0 + 1).max(((dx + 1) * den + n - 1) / n));
                        let mut coverage = 0;
                        let mut max_raw = 0;
                        let mut samples = 0;
                        for sy in sy0..sy1 {
                            for sx in sx0..sx1 {
                                let raw = sample(bitmap, sy * w + sx, two);
                                coverage += raw;
                                max_raw = max_raw.max(raw);
                                samples += 1;
                            }
                        }
                        let ink = if n * 2 == den {
                            if two {
                                max_raw >= 2 || coverage >= 2
                            } else {
                                max_raw != 0
                            }
                        } else {
                            samples > 0 && coverage * 2 >= samples * 3
                        };
                        if ink {
                            self.pixel(x + dx, y + dy, state);
                        }
                    }
                }
            }
            Kind::Bilinear => {
                let scale = d.scale256;
                if !(1..=4096).contains(&scale) || d.solid != 0 {
                    return false;
                }
                let dw = (w * scale + 255) / 256;
                let dh = (h * scale + 255) / 256;
                let left = d.x + i32::from(g.left) * scale / 256;
                let top = d.y - i32::from(g.top) * scale / 256;
                let step = (256 << 16) / scale;
                let origin = step / 2 - (1 << 15);
                let at = |x: i32, y: i32| {
                    if x < 0 || y < 0 || x >= w || y >= h {
                        0
                    } else {
                        sample(bitmap, y * w + x, two)
                    }
                };
                for dy in 0..dh {
                    let sy = origin + dy * step;
                    let y0 = sy >> 16;
                    let fy = (sy >> 8) & 255;
                    for dx in 0..dw {
                        let sx = origin + dx * step;
                        let x0 = sx >> 16;
                        let fx = (sx >> 8) & 255;
                        let upper = at(x0, y0) * (256 - fx) + at(x0 + 1, y0) * fx;
                        let lower = at(x0, y0 + 1) * (256 - fx) + at(x0 + 1, y0 + 1) * fx;
                        if (upper * (256 - fy) + lower * fy) * 2 >= 3 * 256 * 256 {
                            self.pixel(left + dx, top + dy, true);
                        }
                    }
                }
            }
        }
        true
    }
}
#[inline]
fn raw(bitmap: &[u8], pos: i32, two: bool) -> u8 {
    if two {
        (bitmap[(pos >> 2) as usize] >> (6 - (pos & 3) * 2)) & 3
    } else {
        (bitmap[(pos >> 3) as usize] >> (7 - (pos & 7))) & 1
    }
}
fn sample(bitmap: &[u8], pos: i32, two: bool) -> i32 {
    let r = raw(bitmap, pos, two);
    i32::from(if two { r } else { r * 3 })
}
fn apply(byte: &mut u8, mask: u8, clear: bool) {
    if clear {
        *byte &= !mask;
    } else {
        *byte |= mask;
    }
}

#[cfg(test)]
mod tests;

const _: () = {
    use core::mem::{offset_of, size_of};
    let narrow = size_of::<usize>() == 4;
    assert!(size_of::<Draw>() == 44);
    assert!(offset_of!(Draw, x) == 16);
    assert!(offset_of!(Draw, kind) == 40);
    assert!(size_of::<Clip>() == 16);
    assert!(size_of::<PageEntry>() == 12);
    assert!(size_of::<GlyphTarget>() == if narrow { 52 } else { 56 });
    assert!(offset_of!(GlyphTarget, clip) == if narrow { 28 } else { 32 });
    assert!(offset_of!(GlyphTarget, orientation) == if narrow { 44 } else { 48 });
    assert!(size_of::<BitmapSource>() == if narrow { 40 } else { 64 });
    assert!(offset_of!(BitmapSource, font) == if narrow { 8 } else { 16 });
    assert!(offset_of!(BitmapSource, slots) == if narrow { 20 } else { 32 });
    assert!(offset_of!(BitmapSource, overflow_first) == if narrow { 28 } else { 48 });
    assert!(size_of::<PageSlot>() == if narrow { 16 } else { 32 });
    assert!(size_of::<Bitmap>() == if narrow { 8 } else { 16 });
};
