//! Orientation and absolute/differential grayscale writes to a borrowed target.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Target {
    pub data: *mut u8,
    pub len: usize,
    pub stride: i32,
    pub origin: i32,
    pub rows: i32,
    pub width: i32,
    pub height: i32,
    pub mode: u8,
    pub orientation: u8,
    pub absolute: u8,
}

pub struct Writer {
    target: Target,
    x_base: i32,
    y_base: i32,
    xx: i32,
    yx: i32,
    xy: i32,
    yy: i32,
    row_x: i32,
    row_y: i32,
}

impl Writer {
    /// # Safety
    /// Target data is exclusively writable for len bytes until this writer is dropped.
    pub unsafe fn new(target: Target) -> Self {
        let (x_base, y_base, xx, yx, xy, yy) = match target.orientation {
            0 => (0, target.height - 1, 0, -1, 1, 0),
            1 => (target.width - 1, target.height - 1, -1, 0, 0, -1),
            2 => (target.width - 1, 0, 0, 1, -1, 0),
            _ => (0, 0, 1, 0, 0, 1),
        };
        Self {
            target,
            x_base,
            y_base,
            xx,
            yx,
            xy,
            yy,
            row_x: 0,
            row_y: 0,
        }
    }

    pub fn begin_row(&mut self, y: i32) {
        self.row_x = self.x_base + y * self.xy;
        self.row_y = self.y_base + y * self.yy;
    }

    pub fn columns(&self, x: i32, width: i32) -> (i32, i32) {
        let t = &self.target;
        if self.yx == 0 {
            return (
                0,
                if (self.row_y - t.origin) as u32 >= t.rows as u32 {
                    0
                } else {
                    width
                },
            );
        }
        let hi = t.origin + t.rows - 1;
        let (lo, hi) = if self.yx > 0 {
            (t.origin - self.row_y, hi - self.row_y)
        } else {
            (self.row_y - hi, self.row_y - t.origin)
        };
        let end = width.min(hi - x + 1);
        ((lo - x).max(0).min(end), end)
    }

    #[inline(always)]
    pub fn pixel(&self, x: i32, level: u8, white: bool) {
        let t = &self.target;
        let (draw, black) = match t.mode {
            0 => (white || level < 3, level < 3),
            1 | 2 if t.absolute != 0 => (
                true,
                !(level == 3 || level == if t.mode == 2 { 2 } else { 1 }),
            ),
            1 => (level == 1, false),
            2 => (level == 1 || level == 2, false),
            _ => return,
        };
        if !draw {
            return;
        }
        let px = self.row_x + x * self.xx;
        let sy = self.row_y + x * self.yx - t.origin;
        if sy < 0 || sy >= t.rows || px < 0 || px >= t.width || t.data.is_null() {
            return;
        }
        // Keep the existing image writer's 16-bit framebuffer offset.
        let index = (sy as usize * t.stride as usize + (px as usize >> 3)) as u16 as usize;
        if index >= t.len {
            return;
        }
        let mask = 1 << (7 - (px & 7));
        // SAFETY: new's exclusive target contract; index checked above.
        unsafe {
            let byte = t.data.add(index);
            if black {
                *byte &= !mask;
            } else {
                *byte |= mask;
            }
        }
    }

    pub fn packed_row(&mut self, x: i32, y: i32, width: i32, row: &[u8]) {
        self.begin_row(y);
        let (start, end) = self.columns(x, width);
        for col in start.max(0)..end {
            if let Some(byte) = row.get(col as usize >> 2) {
                self.pixel(x + col, (byte >> (6 - (col & 3) * 2)) & 3, false);
            }
        }
    }
}

pub const BAYER: [u8; 16] = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
#[inline(always)]
pub fn dither(gray: u8, x: i32, y: i32) -> u8 {
    let delta = (BAYER[((y & 3) * 4 + (x & 3)) as usize] as i32 - 8) * 5;
    ((gray as i32 + delta).clamp(0, 255) >> 6) as u8
}

#[cfg(test)]
mod tests {
    extern crate alloc;
    use super::*;
    #[test]
    fn wide_panel_retains_legacy_offset() {
        let mut bytes = alloc::vec![255u8; 160 * 720];
        let target = Target {
            data: bytes.as_mut_ptr(),
            len: bytes.len(),
            stride: 160,
            origin: 0,
            rows: 720,
            width: 1280,
            height: 720,
            mode: 0,
            orientation: 3,
            absolute: 0,
        };
        // SAFETY: bytes owns the exclusive target throughout the writer lifetime.
        let mut writer = unsafe { Writer::new(target) };
        writer.begin_row(512);
        writer.pixel(0, 0, false);
        assert_eq!(bytes[16384], 127);
        assert_eq!(bytes[81920], 255);
    }
}
