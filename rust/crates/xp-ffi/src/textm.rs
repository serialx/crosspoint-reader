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
