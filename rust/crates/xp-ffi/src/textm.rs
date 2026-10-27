//! Host differential-test adapter. Firmware integration uses the native trait.
use core::ffi::c_void;
use xp_core::textm::{self, FontFace, FontRegistry, Glyph, MeasureMode};

#[repr(C)]
pub struct Face {
    context: *mut c_void,
    glyph: unsafe extern "C" fn(*mut c_void, u32, u8, *mut Glyph) -> u8,
    coverage: unsafe extern "C" fn(*mut c_void, u32, u8) -> u8,
    kern: unsafe extern "C" fn(*mut c_void, u32, u32, u8) -> i8,
    ligature: unsafe extern "C" fn(*mut c_void, u32, u32, u8) -> u32,
    has_table: unsafe extern "C" fn(*mut c_void) -> u8,
    resolve_style: unsafe extern "C" fn(*mut c_void, u8) -> u8,
    advance: unsafe extern "C" fn(*mut c_void, u32, u8) -> u16,
    ascender: unsafe extern "C" fn(*mut c_void, u8) -> i32,
    line_height: unsafe extern "C" fn(*mut c_void) -> u8,
    transparent: unsafe extern "C" fn(u32) -> u8,
}
struct CallbackFace<'a>(&'a Face);
impl FontFace for CallbackFace<'_> {
    fn glyph(&mut self, cp: u32, style: u8) -> Option<Glyph> {
        let mut g = Glyph::default();
        (unsafe { (self.0.glyph)(self.0.context, cp, style, &mut g) } != 0).then_some(g)
    }
    fn has_codepoint(&self, cp: u32, style: u8) -> bool {
        unsafe { (self.0.coverage)(self.0.context, cp, style) != 0 }
    }
    fn kerning(&mut self, left: u32, right: u32, style: u8) -> i8 {
        unsafe { (self.0.kern)(self.0.context, left, right, style) }
    }
    fn ligature(&self, left: u32, right: u32, style: u8) -> u32 {
        unsafe { (self.0.ligature)(self.0.context, left, right, style) }
    }
    fn has_advance_table(&self) -> bool {
        unsafe { (self.0.has_table)(self.0.context) != 0 }
    }
    fn resolve_style(&self, style: u8) -> u8 {
        unsafe { (self.0.resolve_style)(self.0.context, style) }
    }
    fn advance(&self, cp: u32, style: u8) -> u16 {
        unsafe { (self.0.advance)(self.0.context, cp, style) }
    }
    fn ascender(&self, style: u8) -> i32 {
        unsafe { (self.0.ascender)(self.0.context, style) }
    }
    fn line_height(&self) -> u8 {
        unsafe { (self.0.line_height)(self.0.context) }
    }
}
unsafe fn stream<'a>(p: *const u32, len: u32) -> &'a [u32] {
    if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(p, len as usize)
    }
}

/// # Safety
/// See the shared xp_textm_* pointer/callback contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_textm_advance(
    face: *const Face,
    p: *const u32,
    len: u32,
    style: u8,
    tracking: i8,
    layout: u8,
) -> i32 {
    let Some(face) = face.as_ref() else {
        return 0;
    };
    textm::text_advance_x(
        &mut CallbackFace(face),
        stream(p, len),
        style,
        tracking,
        if layout != 0 {
            MeasureMode::Layout
        } else {
            MeasureMode::Render
        },
        |cp| (face.transparent)(cp) != 0,
    )
}
/// # Safety
/// See xp_textm_advance.
#[no_mangle]
pub unsafe extern "C" fn xp_textm_width(
    face: *const Face,
    p: *const u32,
    len: u32,
    style: u8,
) -> i32 {
    face.as_ref().map_or(0, |f| {
        textm::text_width(&mut CallbackFace(f), stream(p, len), style)
    })
}
/// # Safety
/// See xp_textm_advance.
#[no_mangle]
pub unsafe extern "C" fn xp_textm_space_width(face: *const Face, style: u8) -> i32 {
    face.as_ref()
        .map_or(0, |f| textm::space_width(&mut CallbackFace(f), style))
}
/// # Safety
/// See xp_textm_advance.
#[no_mangle]
pub unsafe extern "C" fn xp_textm_space_advance(
    face: *const Face,
    left: u32,
    right: u32,
    style: u8,
) -> i32 {
    face.as_ref().map_or(0, |f| {
        textm::space_advance(&mut CallbackFace(f), left, right, style)
    })
}
/// # Safety
/// See xp_textm_advance.
#[no_mangle]
pub unsafe extern "C" fn xp_textm_kerning(
    face: *const Face,
    left: u32,
    right: u32,
    style: u8,
    tracking: i8,
) -> i32 {
    face.as_ref().map_or(0, |f| {
        textm::kerning(&mut CallbackFace(f), left, right, style, tracking)
    })
}
/// # Safety
/// See xp_textm_advance. Outputs must be writable and distinct; unchanged on miss.
#[no_mangle]
pub unsafe extern "C" fn xp_textm_codepoint(
    face: *const Face,
    cp: u32,
    style: u8,
    advance: *mut i32,
    top: *mut i32,
) -> u8 {
    let Some(face) = face.as_ref() else {
        return 0;
    };
    let Some((a, t)) = textm::codepoint_metrics(&mut CallbackFace(face), cp, style) else {
        return 0;
    };
    advance.write(a);
    top.write(t);
    1
}
/// # Safety
/// See xp_textm_advance.
#[no_mangle]
pub unsafe extern "C" fn xp_textm_ascender(face: *const Face) -> i32 {
    face.as_ref()
        .map_or(0, |f| textm::font_ascender(&CallbackFace(f)))
}
/// # Safety
/// See xp_textm_advance. Compression must be finite with an i32-representable result.
#[no_mangle]
pub unsafe extern "C" fn xp_textm_line_height(face: *const Face, compression: f32) -> i32 {
    face.as_ref().map_or(0, |f| {
        textm::compressed_line_height(&CallbackFace(f), compression)
    })
}

#[repr(C)]
pub struct Registry {
    context: *mut c_void,
    fallback: unsafe extern "C" fn(*mut c_void, i32, *mut i32) -> u8,
    contains: unsafe extern "C" fn(*mut c_void, i32) -> u8,
    coverage: unsafe extern "C" fn(*mut c_void, i32, u32, u8) -> u8,
}
impl FontRegistry for Registry {
    fn fallback_id(&self, id: i32) -> Option<i32> {
        let mut result = 0;
        (unsafe { (self.fallback)(self.context, id, &mut result) } != 0).then_some(result)
    }
    fn contains(&self, id: i32) -> bool {
        unsafe { (self.contains)(self.context, id) != 0 }
    }
    fn has_codepoint(&self, id: i32, cp: u32, style: u8) -> bool {
        unsafe { (self.coverage)(self.context, id, cp, style) != 0 }
    }
}
/// # Safety
/// Same borrowing/callback contract as xp_textm_advance; p is logical text.
#[no_mangle]
pub unsafe extern "C" fn xp_textm_resolve_font(
    registry: *const Registry,
    id: i32,
    p: *const u32,
    len: u32,
    style: u8,
) -> i32 {
    registry
        .as_ref()
        .map_or(id, |r| textm::resolve_font_id(r, id, stream(p, len), style))
}

use xp_core::textm::native::{Engine, Provider};
unsafe fn bytes<'a>(data: *const u8, len: usize) -> &'a [u8] {
    if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(data, len)
    }
}
/// # Safety
/// Provider and input satisfy the synchronous xp_native_* contract.
#[no_mangle]
pub unsafe extern "C" fn xp_native_advance(
    p: *const Provider,
    id: i32,
    data: *const u8,
    len: usize,
    style: u8,
    tracking: i8,
    direction: i32,
    layout: u8,
) -> i32 {
    Engine::new(*p).advance(
        id,
        bytes(data, len),
        style,
        tracking,
        direction,
        if layout != 0 {
            MeasureMode::Layout
        } else {
            MeasureMode::Render
        },
    )
}
/// # Safety
/// See xp_native_advance.
#[no_mangle]
pub unsafe extern "C" fn xp_native_width(
    p: *const Provider,
    id: i32,
    data: *const u8,
    len: usize,
    style: u8,
    direction: i32,
) -> i32 {
    Engine::new(*p).width(id, bytes(data, len), style, direction)
}
/// # Safety
/// See xp_native_advance.
#[no_mangle]
pub unsafe extern "C" fn xp_native_resolve(
    p: *const Provider,
    id: i32,
    data: *const u8,
    len: usize,
    style: u8,
) -> i32 {
    Engine::new(*p).resolve(id, bytes(data, len), style)
}
/// # Safety
/// See xp_native_advance.
#[no_mangle]
pub unsafe extern "C" fn xp_native_space(p: *const Provider, id: i32, style: u8) -> i32 {
    let e = Engine::new(*p);
    textm::space_width(&mut e.face(id), style)
}
/// # Safety
/// See xp_native_advance.
#[no_mangle]
pub unsafe extern "C" fn xp_native_gap(
    p: *const Provider,
    id: i32,
    left: u32,
    right: u32,
    style: u8,
) -> i32 {
    let e = Engine::new(*p);
    e.gap(id, left, right, style)
}
/// # Safety
/// See xp_native_advance.
#[no_mangle]
pub unsafe extern "C" fn xp_native_kern(
    p: *const Provider,
    id: i32,
    left: u32,
    right: u32,
    style: u8,
    tracking: i8,
) -> i32 {
    let e = Engine::new(*p);
    if e.exists(id) {
        textm::kerning(&mut e.face(id), left, right, style, tracking)
    } else {
        0
    }
}
/// # Safety
/// See xp_native_advance. Outputs are live, disjoint writable integers.
#[no_mangle]
pub unsafe extern "C" fn xp_native_codepoint(
    p: *const Provider,
    id: i32,
    cp: u32,
    style: u8,
    advance: *mut i32,
    top: *mut i32,
) -> u8 {
    let e = Engine::new(*p);
    if let Some((a, t)) = textm::codepoint_metrics(&mut e.face(id), cp, style) {
        *advance = a;
        *top = t;
        1
    } else {
        0
    }
}

/// # Safety
/// Host-only callback may query this synchronous cache between owner mutations.
#[cfg(feature = "chapter-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_native_test_run(
    p: *const Provider,
    ctx: *mut c_void,
    run: unsafe extern "C" fn(*mut c_void, *const Engine),
) {
    run(ctx, &Engine::new(*p));
}
/// # Safety
/// Engine is the live synchronous handle from xp_native_test_run; input is readable.
#[cfg(feature = "chapter-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_native_test_advance(
    e: *const Engine,
    id: i32,
    data: *const u8,
    len: usize,
    style: u8,
) -> i32 {
    (&*e).advance(id, bytes(data, len), style, 0, -1, MeasureMode::Layout)
}

/// # Safety
/// Provider and text obey xp_native_*; callbacks are synchronous and cannot reenter
/// bidi or font registration. They must not retain any supplied pointers.
#[no_mangle]
pub unsafe extern "C" fn xp_native_draw(
    provider: *const Provider,
    id: i32,
    data: *const u8,
    len: usize,
    style: u8,
    tracking: i8,
    direction: i32,
    rotated: u8,
    context: *mut c_void,
    prepare: unsafe extern "C" fn(
        *mut c_void,
        *const u8,
        usize,
    ) -> *const *const xp_core::font::raw::FontData,
    emit: unsafe extern "C" fn(*mut c_void, *const textm::draw::Placement),
) {
    native_draw(
        provider,
        id,
        data,
        len,
        style,
        tracking,
        direction,
        rotated,
        context,
        prepare,
        |placement| emit(context, &placement),
    );
}

#[allow(clippy::too_many_arguments)]
unsafe fn native_draw(
    provider: *const Provider,
    id: i32,
    data: *const u8,
    len: usize,
    style: u8,
    tracking: i8,
    direction: i32,
    rotated: u8,
    context: *mut c_void,
    prepare: unsafe extern "C" fn(
        *mut c_void,
        *const u8,
        usize,
    ) -> *const *const xp_core::font::raw::FontData,
    mut emit: impl FnMut(textm::draw::Placement),
) {
    let text = bytes(data, len);
    let mut render = |visual: &[u8]| {
        let slot = prepare(context, visual.as_ptr(), visual.len());
        if slot.is_null() {
            return;
        }
        let mut face = textm::native::DrawingFace::new(slot, &*provider, id);
        textm::draw::walk(
            &mut face,
            textm::native::Codepoints(visual),
            style,
            tracking,
            rotated != 0,
            &mut emit,
        );
    };
    if rotated != 0 {
        render(text);
    } else {
        textm::native::with_visual(&*provider, text, direction, render);
    }
}

/// # Safety
/// The face is live for this call and callbacks cannot retain any borrows.
#[no_mangle]
pub unsafe extern "C" fn xp_textm_dimensions_utf8(
    face: *const Face,
    data: *const u8,
    len: usize,
    width: *mut i32,
    height: *mut i32,
) {
    let (w, h) = textm::dimensions_stream(
        &mut CallbackFace(&*face),
        textm::native::Codepoints(bytes(data, len)),
        0,
    );
    *width = w;
    *height = h;
}
/// # Safety
/// Same provider contract as xp_native_draw. emit consumes the bitmap immediately.
#[no_mangle]
pub unsafe extern "C" fn xp_native_draw_scaled(
    provider: *const Provider,
    id: i32,
    cp: u32,
    style: u8,
    scale: i32,
    context: *mut c_void,
    emit: unsafe extern "C" fn(*mut c_void),
) -> i32 {
    if scale <= 0 {
        return 0;
    }
    let engine = Engine::new(*provider);
    let Some(glyph) = engine.face(id).glyph(cp, style) else {
        return 0;
    };
    let advance = textm::to_pixel(i32::from(glyph.advance).wrapping_mul(scale) / 256);
    emit(context);
    advance
}

use xp_core::font::raw::{FontData, Glyph as RasterGlyph};
use xp_core::glyph::{Bitmap, BitmapSource, Draw, GlyphTarget, Kind};
#[repr(C)]
pub struct GlyphPaint {
    target: GlyphTarget,
    source: BitmapSource,
    x: i32,
    y: i32,
    state: u8,
}
type Prepare = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> *const *const FontData;
type Miss = unsafe extern "C" fn(
    *mut c_void,
    u32,
    *const RasterGlyph,
    *mut Draw,
    *mut BitmapSource,
) -> Bitmap;
type Observer = unsafe extern "C" fn(*mut c_void, *const textm::draw::Placement);

// All table loans end before miss; the returned bytes are painted before another
// font operation. The callback may rebuild source, so never lend the whole paint.
#[inline(always)]
unsafe fn paint_glyph(
    paint: *mut GlyphPaint,
    context: *mut c_void,
    miss: Miss,
    placement: textm::draw::Placement,
    kind: Kind,
    scale: i32,
) {
    let mut draw = Draw {
        glyph: RasterGlyph {
            width: 0,
            height: 0,
            advance_x: 0,
            left: 0,
            top: 0,
            data_length: 0,
            data_offset: 0,
        },
        x: (*paint).x + placement.x,
        y: (*paint).y + placement.y,
        ascender: 0,
        scale_num: i32::from(placement.scale_num),
        scale_den: i32::from(placement.scale_den),
        scale256: scale,
        kind: kind as u8,
        two_bit: 0,
        state: (*paint).state,
        solid: 0,
    };
    let bitmap = {
        let source = &(*paint).source;
        if !placement.glyph.is_null()
            && !source.generation.is_null()
            && (*source.generation).load(core::sync::atomic::Ordering::Relaxed)
                == source.captured_generation
        {
            draw.glyph = *placement.glyph;
            draw.ascender = (*source.font).ascender;
            draw.two_bit = u8::from((*source.font).is2_bit);
            if matches!(kind, Kind::Normal | Kind::Rotated) {
                let g = draw.glyph;
                let x = draw.x
                    + if matches!(kind, Kind::Rotated) {
                        draw.ascender - i32::from(g.top)
                    } else {
                        i32::from(g.left)
                    };
                let y = draw.y
                    - if matches!(kind, Kind::Rotated) {
                        i32::from(g.left)
                    } else {
                        i32::from(g.top)
                    };
                let bounds = if matches!(kind, Kind::Rotated) {
                    (
                        x,
                        y - i32::from(g.width) + 1,
                        x + i32::from(g.height) - 1,
                        y,
                    )
                } else {
                    (
                        x,
                        y,
                        x + i32::from(g.width) - 1,
                        y + i32::from(g.height) - 1,
                    )
                };
                if !(*paint)
                    .target
                    .intersects_strip(bounds.0, bounds.1, bounds.2, bounds.3)
                {
                    return;
                }
            }
            source.resolve(placement.glyph)
        } else {
            None
        }
    };
    let bitmap = bitmap.unwrap_or_else(|| {
        miss(
            context,
            placement.codepoint,
            placement.glyph,
            &mut draw,
            core::ptr::addr_of_mut!((*paint).source),
        )
    });
    // The owner supplies metrics; raster selection remains local.
    draw.kind = kind as u8;
    if bitmap.data.is_null() && draw.solid == 0 {
        return;
    }
    let target = &(*paint).target;
    let buffer = core::slice::from_raw_parts_mut(target.buffer, target.buffer_len as usize);
    let bitmap = if bitmap.len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(bitmap.data, bitmap.len as usize)
    };
    xp_core::glyph::Surface::from_validated(target, buffer).draw(bitmap, &draw);
}

/// # Safety
/// Provider, source and callbacks obey the synchronous draw loan contract.
/// Target geometry satisfies GlyphTarget::valid and stays immutable during callbacks.
#[no_mangle]
pub unsafe extern "C" fn xp_native_draw_pixels(
    provider: *const Provider,
    id: i32,
    data: *const u8,
    len: usize,
    style: u8,
    tracking: i8,
    direction: i32,
    rotated: u8,
    context: *mut c_void,
    prepare: Prepare,
    paint: *mut GlyphPaint,
    miss: Miss,
) {
    draw_pixels(
        provider, id, data, len, style, tracking, direction, rotated, context, prepare, paint,
        miss, None,
    );
}
#[allow(clippy::too_many_arguments)]
unsafe fn draw_pixels(
    provider: *const Provider,
    id: i32,
    data: *const u8,
    len: usize,
    style: u8,
    tracking: i8,
    direction: i32,
    rotated: u8,
    context: *mut c_void,
    prepare: Prepare,
    paint: *mut GlyphPaint,
    miss: Miss,
    observer: Option<Observer>,
) {
    debug_assert!((*paint).target.valid());
    if (*paint).target.buffer.is_null() {
        return;
    }
    native_draw(
        provider,
        id,
        data,
        len,
        style,
        tracking,
        direction,
        rotated,
        context,
        prepare,
        |placement| {
            if let Some(observe) = observer {
                observe(context, &placement);
            }
            let kind = if rotated != 0 {
                Kind::Rotated
            } else if placement.scale_num != placement.scale_den {
                Kind::Coverage
            } else {
                Kind::Normal
            };
            paint_glyph(paint, context, miss, placement, kind, 256);
        },
    );
}
/// # Safety
/// Same as xp_native_draw_pixels; observer must not mutate font owners.
#[cfg(feature = "chapter-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_native_draw_pixels_traced(
    provider: *const Provider,
    id: i32,
    data: *const u8,
    len: usize,
    style: u8,
    tracking: i8,
    direction: i32,
    rotated: u8,
    context: *mut c_void,
    prepare: Prepare,
    paint: *mut GlyphPaint,
    miss: Miss,
    observer: Observer,
) {
    draw_pixels(
        provider,
        id,
        data,
        len,
        style,
        tracking,
        direction,
        rotated,
        context,
        prepare,
        paint,
        miss,
        Some(observer),
    );
}
/// # Safety
/// Same owner/validated-target contract as xp_native_draw_pixels; slot belongs to a stable EpdFont.
#[no_mangle]
pub unsafe extern "C" fn xp_native_draw_scaled_pixels(
    provider: *const Provider,
    id: i32,
    cp: u32,
    style: u8,
    scale: i32,
    slot: *const *const FontData,
    context: *mut c_void,
    paint: *mut GlyphPaint,
    miss: Miss,
) -> i32 {
    if scale <= 0 {
        return 0;
    }
    debug_assert!((*paint).target.valid());
    if (*paint).target.buffer.is_null() {
        return 0;
    }
    let mut face = textm::native::DrawingFace::new(slot, &*provider, id);
    let Some(glyph) = face.glyph(cp, style) else {
        return 0;
    };
    let advance = textm::to_pixel(i32::from(glyph.advance).wrapping_mul(scale) / 256);
    paint_glyph(
        paint,
        context,
        miss,
        textm::draw::Placement {
            codepoint: cp,
            glyph: face.glyph_pointer(cp),
            x: 0,
            y: 0,
            scale_num: 1,
            scale_den: 1,
        },
        Kind::Bilinear,
        scale,
    );
    advance
}
