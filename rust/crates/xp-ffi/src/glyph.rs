use xp_core::{
    font::raw::Glyph,
    glyph::{Bitmap, BitmapSource, Draw, GlyphTarget, Surface},
};

/// # Safety
/// Descriptors are live/aligned; target owns buffer_len writable bytes and bitmap
/// owns bitmap_len readable bytes. The two ranges and descriptors do not alias.
/// No owner mutation or callback may run during this synchronous call.
#[no_mangle]
pub unsafe extern "C" fn xp_glyph_draw(
    target: *const GlyphTarget,
    draw: *const Draw,
    bitmap: *const u8,
    bitmap_len: u32,
) -> u8 {
    if target.is_null() || draw.is_null() || bitmap_len as u64 > isize::MAX as u64 {
        return 0;
    }
    let t = unsafe { &*target };
    if !t.valid() || t.buffer.is_null() || (bitmap.is_null() && bitmap_len != 0) {
        return 0;
    }
    let bytes = if bitmap_len == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(bitmap, bitmap_len as usize) }
    };
    let buffer = unsafe { core::slice::from_raw_parts_mut(t.buffer, t.buffer_len as usize) };
    let Some(mut surface) = Surface::new(t, buffer) else {
        return 0;
    };
    u8::from(surface.draw(bytes, unsafe { &*draw }))
}
/// # Safety
/// target is live/aligned. No framebuffer bytes are accessed.
#[no_mangle]
pub unsafe extern "C" fn xp_glyph_intersects_strip(
    target: *const GlyphTarget,
    x0: i32,
    y0: i32,
    x1: i32,
    y1: i32,
) -> u8 {
    if target.is_null() {
        return 0;
    }
    let t = unsafe { &*target };
    if !t.valid()
        || ![x0, y0, x1, y1]
            .iter()
            .all(|v| (-1_000_000..=1_000_000).contains(v))
    {
        return 0;
    }
    u8::from(t.intersects_strip(x0, y0, x1, y1))
}
/// # Safety
/// source satisfies BitmapSource::resolve's loan contract; out is writable and
/// disjoint. Returned bytes must be consumed before any font owner callback.
#[no_mangle]
pub unsafe extern "C" fn xp_glyph_bitmap(
    source: *const BitmapSource,
    glyph: *const Glyph,
    out: *mut Bitmap,
) -> u8 {
    if source.is_null() || out.is_null() {
        return 0;
    }
    unsafe {
        *out = Bitmap::MISS;
    }
    match unsafe { (&*source).resolve(glyph) } {
        Some(bitmap) => {
            unsafe {
                *out = bitmap;
            }
            1
        }
        None => 0,
    }
}
