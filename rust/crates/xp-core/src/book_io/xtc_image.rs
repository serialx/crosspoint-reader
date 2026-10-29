//! XTC cover pixels. The caller owns page/row storage and the file sink.
use super::txt::Write;
use core::ffi::c_void;

pub type Yield = unsafe extern "C" fn(*mut c_void);

fn header(width: u16, height: u16, depth: u8) -> ([u8; 70], usize) {
    let size = if depth == 2 { 70 } else { 62 };
    let row = (u32::from(width) * u32::from(depth)).div_ceil(32) * 4;
    let image = row * u32::from(height);
    let mut out = [0u8; 70];
    out[..2].copy_from_slice(b"BM");
    out[2..6].copy_from_slice(&(size as u32 + image).to_le_bytes());
    out[10] = size as u8;
    out[14] = 40;
    out[18..22].copy_from_slice(&u32::from(width).to_le_bytes());
    out[22..26].copy_from_slice(&(-i32::from(height)).to_le_bytes());
    out[26] = 1;
    out[28] = depth;
    out[34..38].copy_from_slice(&image.to_le_bytes());
    out[38..42].copy_from_slice(&2835u32.to_le_bytes());
    out[42..46].copy_from_slice(&2835u32.to_le_bytes());
    out[46] = 1 << depth;
    out[50] = 1 << depth;
    if depth == 2 {
        out[58..61].fill(0x55);
        out[62..65].fill(0xaa);
        out[66..69].fill(0xff);
    } else {
        out[58..61].fill(0xff);
    }
    (out, size)
}

fn xth_value(pixels: &[u8], width: u16, height: u16, x: u32, y: u32) -> usize {
    let columns = usize::from(height).div_ceil(8);
    let plane = usize::from(width) * columns;
    let at = (usize::from(width) - 1 - x as usize) * columns + y as usize / 8;
    let shift = 7 - y % 8;
    usize::from(((pixels[at] >> shift) & 1) * 2 + ((pixels[at + plane] >> shift) & 1))
}

/// # Safety
/// Write borrows bytes only for its call and context is valid for each callback.
pub unsafe fn cover(
    pixels: &[u8],
    width: u16,
    height: u16,
    depth: u8,
    row: &mut [u8],
    context: *mut c_void,
    write: Write,
) {
    let (hdr, len) = header(width, height, if depth == 2 { 2 } else { 1 });
    write(context, hdr.as_ptr(), len);
    if depth == 2 {
        for y in 0..height {
            row.fill(0);
            for x in 0..width {
                let value = [3u8, 1, 2, 0][xth_value(pixels, width, height, x.into(), y.into())];
                row[usize::from(x) / 4] |= value << (6 - (x % 4) * 2);
            }
            write(context, row.as_ptr(), row.len());
        }
    } else {
        let source_row = usize::from(width).div_ceil(8);
        let padded = usize::from(width).div_ceil(32) * 4;
        let padding = [0u8; 4];
        for y in 0..height {
            write(
                context,
                pixels.as_ptr().add(usize::from(y) * source_row),
                source_row,
            );
            if padded > source_row {
                write(context, padding.as_ptr(), padded - source_row);
            }
        }
    }
}

fn source_range(index: u16, inverse: u32, limit: u16) -> (u32, u32) {
    let limit = u32::from(limit);
    let start = (u32::from(index).wrapping_mul(inverse) >> 16).min(limit - 1);
    let end = ((u32::from(index) + 1).wrapping_mul(inverse) >> 16).min(limit);
    (start, end.max(start + 1).min(limit))
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Thumbnail {
    pub width: u16,
    pub height: u16,
    pub scale: f32,
}

pub fn thumbnail(width: u16, height: u16, target: i32) -> Thumbnail {
    let target_width = (f64::from(target) * 0.6) as i32;
    let x = target_width as f32 / f32::from(width);
    let y = target as f32 / f32::from(height);
    let scale = if x > y { x } else { y };
    Thumbnail {
        width: (f32::from(width) * scale) as u16,
        height: (f32::from(height) * scale) as u16,
        scale,
    }
}

/// # Safety
/// Callbacks obey cover's contract and do not mutate the page or row buffers.
#[allow(clippy::too_many_arguments)]
pub unsafe fn thumb(
    pixels: &[u8],
    width: u16,
    height: u16,
    depth: u8,
    plan: Thumbnail,
    row: &mut [u8],
    context: *mut c_void,
    write: Write,
    yield_now: Yield,
) {
    let (hdr, len) = header(plan.width, plan.height, 1);
    write(context, hdr.as_ptr(), len);
    let inverse = (65536.0f32 / plan.scale) as u32;
    let stride = usize::from(width).div_ceil(8);
    for y in 0..plan.height {
        row.fill(0xff);
        let (top, bottom) = source_range(y, inverse, height);
        for x in 0..plan.width {
            let (left, right) = source_range(x, inverse, width);
            let mut gray = 0u32;
            let mut count = 0u32;
            for sy in top..bottom {
                for sx in left..right {
                    let value = if depth == 2 {
                        [255u8, 85, 170, 0][xth_value(pixels, width, height, sx, sy)]
                    } else {
                        ((pixels[sy as usize * stride + sx as usize / 8] >> (7 - sx % 8)) & 1) * 255
                    };
                    gray = gray.wrapping_add(u32::from(value));
                    count += 1;
                }
            }
            let average = gray.checked_div(count).map_or(255, |value| value as u8);
            let hash = u32::from(x)
                .wrapping_mul(374761393)
                .wrapping_add(u32::from(y).wrapping_mul(668265263));
            let hash = (hash ^ (hash >> 13)).wrapping_mul(1274126177);
            let threshold = 128 + (((hash >> 24) as i32 - 128) / 2);
            if i32::from(average) < threshold {
                row[usize::from(x) / 8] &= !(1 << (7 - x % 8));
            }
        }
        write(context, row.as_ptr(), row.len());
        if y % 8 == 7 {
            yield_now(context);
        }
    }
}
