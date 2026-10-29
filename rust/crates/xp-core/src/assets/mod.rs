//! Asset byte layouts and streaming JPEG post-processing. Platform codecs and I/O are borrowed.
pub mod jpeg;

fn bmp_header(width: u32, height: i32, bits: u8, output: &mut [u8; 70]) -> usize {
    let colors = 1u32 << bits;
    let len = 54 + colors * 4;
    let size = (width * u32::from(bits)).div_ceil(32) * 4 * height.unsigned_abs();
    output.fill(0);
    output[..2].copy_from_slice(b"BM");
    for (offset, value) in [
        (2, len + size),
        (10, len),
        (14, 40),
        (18, width),
        (22, height as u32),
        (34, size),
        (38, 2835),
        (42, 2835),
        (46, colors),
        (50, colors),
    ] {
        output[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    output[26] = 1;
    output[28] = bits;
    for i in 0..colors as usize {
        output[54 + i * 4..57 + i * 4].fill((i as u32 * 255 / (colors - 1)) as u8);
    }
    len as usize
}

/// The source is the panel-native 1bpp plane; rows are emitted in BMP bottom-up order.
pub fn screenshot(
    frame: &[u8],
    width: usize,
    height: usize,
    write: &mut dyn FnMut(&[u8]) -> bool,
) -> u32 {
    if width == 0
        || height == 0
        || !width.is_multiple_of(8)
        || width > i32::MAX as usize
        || height > i32::MAX as usize
    {
        return 1;
    }
    let Some(bytes) = (width / 8).checked_mul(height) else {
        return 1;
    };
    if frame.len() < bytes {
        return 1;
    }
    let row_size = height.div_ceil(32) * 4;
    if row_size
        .checked_mul(width)
        .is_none_or(|size| size > u32::MAX as usize - 62)
    {
        return 1;
    }
    let mut header = [0; 70];
    let len = bmp_header(height as u32, width as i32, 1, &mut header);
    if !write(&header[..len]) {
        return 1;
    }
    if row_size > 68 {
        return 2;
    }
    let mut row = [0; 68];
    for y in 0..width {
        row.fill(0);
        let sx = width - 1 - y;
        for x in 0..height {
            let sy = height - 1 - x;
            let bit = (frame[sy * (width / 8) + sx / 8] >> (7 - sx % 8)) & 1;
            row[x / 8] |= bit << (7 - x % 8);
        }
        if !write(&row[..row_size]) {
            return 1;
        }
    }
    0
}

pub fn qr_plan(text: &[u8]) -> (usize, u8, usize) {
    let length = if text.len() > 2953 {
        crate::utf8::safe_truncate(&text[..2953])
    } else {
        text.len()
    };
    let version = match length {
        0..=114 => 4,
        115..=395 => 10,
        396..=1066 => 20,
        1067..=2110 => 30,
        _ => 40,
    };
    let size = version as usize * 4 + 17;
    (length, version, (size * size).div_ceil(8))
}

pub fn qr_rectangles(
    modules: &[u8],
    size: i32,
    bounds: [i32; 4],
    draw: &mut dyn FnMut(i32, i32, i32),
) {
    if !(1..=177).contains(&size) || modules.len() < ((size * size) as usize).div_ceil(8) {
        return;
    }
    let [x, y, width, height] = bounds;
    let px = (width.min(height) / size).max(1);
    let Some(display) = size.checked_mul(px) else {
        return;
    };
    let Some(ox) = width
        .checked_sub(display)
        .and_then(|v| x.checked_add(v / 2))
    else {
        return;
    };
    let Some(oy) = height
        .checked_sub(display)
        .and_then(|v| y.checked_add(v / 2))
    else {
        return;
    };
    if ox.checked_add(display).is_none() || oy.checked_add(display).is_none() {
        return;
    }
    for cy in 0..size {
        for cx in 0..size {
            let bit = (cy * size + cx) as usize;
            if modules[bit / 8] & (1 << (7 - bit % 8)) != 0 {
                draw(ox + px * cx, oy + px * cy, px);
            }
        }
    }
}
pub mod image;
