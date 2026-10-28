//! Packed panel-native row bands. The caller owns and reuses all storage.
use crate::glyph::{Clip, GlyphTarget, Surface};
use core::convert::Infallible;
use embedded_graphics_core::{
    draw_target::DrawTarget,
    geometry::{Dimensions, Point, Size},
    pixelcolor::{BinaryColor, Gray2, GrayColor},
    primitives::Rectangle,
    Pixel,
};

/// One bit per panel pixel: BW clears ink; overlay planes set gray correction bits.
/// Gray2 is luminance (0 = black, 3 = white), as in the image pixel cache.
pub struct Strip<'a> {
    target: GlyphTarget,
    bytes: &'a mut [u8],
}
impl<'a> Strip<'a> {
    /// Borrows exactly `stride * rows` bytes. Invalid geometry never writes storage.
    pub fn new(target: GlyphTarget, bytes: &'a mut [u8]) -> Option<Self> {
        if !target.valid() || bytes.len() < target.buffer_len as usize {
            return None;
        }
        Some(Self { target, bytes })
    }
    pub fn clear_plane(&mut self) {
        let clear = if self.target.plane == 0 || self.target.absolute != 0 {
            255
        } else {
            0
        };
        self.bytes[..(self.target.stride * self.target.rows) as usize].fill(clear);
    }
    pub fn surface(&mut self) -> Surface<'_> {
        // Construction validated the immutable descriptor and exclusive loan.
        unsafe { Surface::from_validated(&self.target, self.bytes) }
    }
    /// Physical band expressed in the logical coordinate system, clipped to the canvas.
    fn visible(&self) -> Rectangle {
        let t = &self.target;
        let (left, top, width, height) = match t.orientation {
            0 => (t.height - t.origin_y - t.rows, 0, t.rows, t.width),
            1 => (0, t.height - t.origin_y - t.rows, t.width, t.rows),
            2 => (t.origin_y, 0, t.rows, t.width),
            _ => (0, t.origin_y, t.width, t.rows),
        };
        Rectangle::new(
            Point::new(left, top),
            Size::new(width as u32, height as u32),
        )
        .intersection(&Rectangle::new(
            Point::new(t.clip.left, t.clip.top),
            Size::new(
                (t.clip.right - t.clip.left).max(0) as u32,
                (t.clip.bottom - t.clip.top).max(0) as u32,
            ),
        ))
    }
    // Rectangle's own intersection adds signed coordinates in i32. Public draw
    // calls can supply an offscreen origin or u32 extent, so widen before clipping.
    fn clipped(&self, area: &Rectangle) -> Rectangle {
        let v = self.visible();
        let left = i64::from(area.top_left.x).max(i64::from(v.top_left.x));
        let top = i64::from(area.top_left.y).max(i64::from(v.top_left.y));
        let right = (i64::from(area.top_left.x) + i64::from(area.size.width))
            .min(i64::from(v.top_left.x) + i64::from(v.size.width));
        let bottom = (i64::from(area.top_left.y) + i64::from(area.size.height))
            .min(i64::from(v.top_left.y) + i64::from(v.size.height));
        if left >= right || top >= bottom {
            return Rectangle::zero();
        }
        Rectangle::new(
            Point::new(left as i32, top as i32),
            Size::new((right - left) as u32, (bottom - top) as u32),
        )
    }
    fn fill_colors<C, I>(
        &mut self,
        area: &Rectangle,
        colors: I,
        map: impl Fn(&Self, C) -> Option<BinaryColor>,
    ) -> Result<(), Infallible>
    where
        I: IntoIterator<Item = C>,
    {
        let clipped = self.clipped(area);
        if clipped.size.width == 0 || clipped.size.height == 0 {
            return Ok(());
        }
        let mut colors = colors.into_iter();
        let width = u64::from(area.size.width);
        let mut consumed = 0u64;
        for y in clipped.top_left.y..clipped.top_left.y + clipped.size.height as i32 {
            let offset = (i64::from(y) - i64::from(area.top_left.y)) as u64 * width
                + (i64::from(clipped.top_left.x) - i64::from(area.top_left.x)) as u64;
            let skip = offset - consumed;
            let Ok(skip) = usize::try_from(skip) else {
                return Ok(());
            };
            let Some(first) = colors.nth(skip) else {
                return Ok(());
            };
            let mut color = map(self, first);
            consumed = offset + 1;
            let mut start = clipped.top_left.x;
            let end = start + clipped.size.width as i32;
            for x in start + 1..end {
                let Some(next) = colors.next() else {
                    if let Some(color) = color {
                        let _ = self.fill_solid(
                            &Rectangle::new(Point::new(start, y), Size::new((x - start) as u32, 1)),
                            color,
                        );
                    }
                    return Ok(());
                };
                let next = map(self, next);
                consumed += 1;
                if next != color {
                    if let Some(color) = color {
                        let _ = self.fill_solid(
                            &Rectangle::new(Point::new(start, y), Size::new((x - start) as u32, 1)),
                            color,
                        );
                    }
                    start = x;
                    color = next;
                }
            }
            if let Some(color) = color {
                let _ = self.fill_solid(
                    &Rectangle::new(Point::new(start, y), Size::new((end - start) as u32, 1)),
                    color,
                );
            }
        }
        Ok(())
    }
    fn pixel(&mut self, point: Point, color: BinaryColor) {
        let t = &self.target;
        // All callers intersect logical coordinates with visible() first.
        let (x, y) = match t.orientation {
            0 => (point.y, t.height - 1 - point.x),
            1 => (t.width - 1 - point.x, t.height - 1 - point.y),
            2 => (t.width - 1 - point.y, point.x),
            _ => (point.x, point.y),
        };
        let byte = &mut self.bytes[((y - t.origin_y) * t.stride + x / 8) as usize];
        let mask = 0x80 >> (x & 7);
        if color == BinaryColor::Off {
            *byte &= !mask;
        } else {
            *byte |= mask;
        }
    }
    fn gray(&self, color: Gray2) -> Option<BinaryColor> {
        let level = color.luma();
        let t = &self.target;
        let black = match t.plane {
            0 => return (level < 3).then_some(BinaryColor::Off),
            1 | 2 if t.absolute != 0 => !(level == 3 || level == if t.plane == 2 { 2 } else { 1 }),
            1 => return (level == 1).then_some(BinaryColor::On),
            2 => return (level == 1 || level == 2).then_some(BinaryColor::On),
            _ => return None,
        };
        Some(if black {
            BinaryColor::Off
        } else {
            BinaryColor::On
        })
    }
    /// Playback one packed 2-bpp cache row. Off-band rows and columns are skipped.
    pub fn packed_row(&mut self, x: i32, y: i32, width: u16, row: &[u8]) -> bool {
        if row.len() < usize::from(width).div_ceil(4) {
            return false;
        }
        let area = Rectangle::new(Point::new(x, y), Size::new(u32::from(width), 1));
        let clipped = self.clipped(&area);
        if clipped.size.height == 0 {
            return true;
        }
        for col in clipped.top_left.x..clipped.top_left.x + clipped.size.width as i32 {
            let source = (col - x) as usize;
            let color = Gray2::new((row[source / 4] >> (6 - source % 4 * 2)) & 3);
            if let Some(color) = self.gray(color) {
                self.pixel(Point::new(col, y), color);
            }
        }
        true
    }
}
impl Dimensions for Strip<'_> {
    fn bounding_box(&self) -> Rectangle {
        let t = &self.target;
        let (w, h) = if t.orientation & 1 == 0 {
            (t.height, t.width)
        } else {
            (t.width, t.height)
        };
        Rectangle::new(Point::zero(), Size::new(w as u32, h as u32))
    }
}
impl DrawTarget for Strip<'_> {
    type Color = BinaryColor;
    type Error = Infallible;
    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        let visible = self.visible();
        for Pixel(point, color) in pixels {
            if visible.contains(point) {
                self.pixel(point, color);
            }
        }
        Ok(())
    }
    fn fill_solid(&mut self, area: &Rectangle, color: Self::Color) -> Result<(), Self::Error> {
        let area = self.clipped(area);
        self.surface().fill(
            area.top_left.x,
            area.top_left.y,
            area.size.width as i32,
            area.size.height as i32,
            color == BinaryColor::Off,
        );
        Ok(())
    }
    fn fill_contiguous<I>(&mut self, area: &Rectangle, colors: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Self::Color>,
    {
        self.fill_colors(area, colors, |_, c| Some(c))
    }
}
/// Gray cache pixels retain the overlay pipeline's sparse write semantics.
pub struct GrayStrip<'a>(pub Strip<'a>);
impl GrayStrip<'_> {
    fn map_color(strip: &Strip<'_>, color: Gray2) -> Option<BinaryColor> {
        strip.gray(color)
    }
}
impl Dimensions for GrayStrip<'_> {
    fn bounding_box(&self) -> Rectangle {
        self.0.bounding_box()
    }
}
impl DrawTarget for GrayStrip<'_> {
    type Color = Gray2;
    type Error = Infallible;
    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Gray2>>,
    {
        for Pixel(point, color) in pixels {
            if let Some(color) = self.0.gray(color) {
                let _ = self.0.draw_iter([Pixel(point, color)]);
            }
        }
        Ok(())
    }
    fn fill_solid(&mut self, area: &Rectangle, color: Gray2) -> Result<(), Self::Error> {
        if let Some(color) = self.0.gray(color) {
            self.0.fill_solid(area, color)?;
        }
        Ok(())
    }
    fn fill_contiguous<I>(&mut self, area: &Rectangle, colors: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Gray2>,
    {
        self.0.fill_colors(area, colors, Self::map_color)
    }
}
/// Logical rectangle to physical row interval; empty rectangles have no coverage.
pub fn row_bounds(t: &GlyphTarget, r: Clip) -> (i32, i32) {
    if r.left >= r.right || r.top >= r.bottom {
        return (t.height, 0);
    }
    match t.orientation {
        0 => (
            t.height.saturating_sub(r.right),
            t.height.saturating_sub(r.left),
        ),
        1 => (
            t.height.saturating_sub(r.bottom),
            t.height.saturating_sub(r.top),
        ),
        2 => (r.left, r.right),
        _ => (r.top, r.bottom),
    }
}
#[cfg(test)]
mod tests;
