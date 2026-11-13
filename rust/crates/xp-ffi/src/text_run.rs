//! Synchronous Rust text runs over the existing font owner and pixel target.
use crate::page_render::direct::Target;
use crate::{
    font_registry::{self, Owner},
    font_runtime, font_sd, textm,
};
use core::{ffi::c_void, ptr};
use xp_core::{
    font::raw::{FontData, Glyph},
    glyph::{Bitmap, BitmapSource, Draw},
    textm::native::{DiagnosticEvent, Engine},
};

pub(crate) struct Run<'a> {
    pub target: &'a Target,
    engine: Engine,
    has_cache: bool,
}
struct Context {
    owner: *const Owner,
    slot: *const *const FontData,
    paint: *mut textm::GlyphPaint,
    font: i32,
    fallback: bool,
    has_cache: bool,
    style: u8,
}
impl<'a> Run<'a> {
    pub unsafe fn new(target: &'a Target, has_cache: bool) -> Self {
        Self {
            target,
            engine: Engine::new(font_registry::xp_font_registry_provider(
                target.fonts.cast(),
            )),
            has_cache,
        }
    }
    pub fn width(&self, font: i32, text: &[u8]) -> i32 {
        self.width_styled(font, text, 0)
    }
    pub fn width_styled(&self, font: i32, text: &[u8], style: u8) -> i32 {
        self.engine.width(font, text, style, -1)
    }
    pub unsafe fn height(&self, font: i32) -> i32 {
        let data = Owner::data(self.target.fonts, font, 0);
        if data.is_null() {
            font_registry::report(
                self.target.fonts,
                DiagnosticEvent::MissingFont as u32,
                font as u32,
            );
            0
        } else {
            i32::from((*data).advance_y)
        }
    }
    pub unsafe fn battery_y(&self, font: i32, y: i32, height: i32) -> i32 {
        let data = Owner::data(self.target.fonts, font, 0);
        if !data.is_null() {
            let glyph = crate::font::xp_font_lookup(data, u32::from(b'0'));
            if !glyph.is_null() {
                return y + (*data).ascender - i32::from((*glyph).top)
                    + (i32::from((*glyph).height) - height) / 2;
            }
        }
        y + 6
    }
    pub unsafe fn draw(&self, font: i32, x: i32, y: i32, text: &[u8]) {
        self.draw_styled(font, x, y, text, 0, true);
    }
    #[allow(clippy::too_many_arguments)]
    pub unsafe fn draw_styled(
        &self,
        font: i32,
        x: i32,
        y: i32,
        text: &[u8],
        style: u8,
        black: bool,
    ) {
        let len = text.iter().position(|&x| x == 0).unwrap_or(text.len());
        if len == 0 {
            return;
        }
        let resolved = self.engine.resolve(font, &text[..len], style);
        let data = Owner::data(self.target.fonts, resolved, 0);
        let mut baseline = if data.is_null() { 0 } else { (*data).ascender };
        if resolved != font {
            baseline += (self.height(font) - self.height(resolved)) / 2;
        }
        let mut paint = textm::GlyphPaint {
            target: ptr::read(&self.target.glyphs),
            source: core::mem::zeroed(),
            x,
            y: y + baseline,
            state: u8::from(black),
        };
        let mut context = Context {
            owner: self.target.fonts,
            slot: ptr::null(),
            paint: &mut paint,
            font: resolved,
            fallback: resolved != font,
            has_cache: self.has_cache,
            style,
        };
        let provider = font_registry::xp_font_registry_provider(self.target.fonts.cast());
        textm::draw_pixels(
            &provider,
            resolved,
            text.as_ptr(),
            len,
            style,
            0,
            -1,
            0,
            (&mut context as *mut Context).cast(),
            prepare,
            &mut paint,
            miss,
            None,
        );
    }
}
unsafe extern "C" fn prepare(
    raw: *mut c_void,
    text: *const u8,
    len: usize,
) -> *const *const FontData {
    let c = &mut *raw.cast::<Context>();
    if c.has_cache && font_runtime::scanning(c.owner) != 0 {
        font_runtime::record_text(c.owner, c.font, text.cast(), c.style);
        return ptr::null();
    }
    if c.fallback {
        font_sd::resident(c.owner, c.font, c.style, text, len, false);
    }
    c.slot = Owner::slot(c.owner, c.font, c.style);
    if c.slot.is_null() {
        font_registry::report(c.owner, DiagnosticEvent::MissingFont as u32, c.font as u32);
        return ptr::null();
    }
    font_registry::xp_font_registry_source(c.owner, c.slot.read(), &mut (*c.paint).source);
    c.slot
}
unsafe extern "C" fn miss(
    raw: *mut c_void,
    cp: u32,
    glyph: *const Glyph,
    draw: *mut Draw,
    source: *mut BitmapSource,
) -> Bitmap {
    let c = &*raw.cast::<Context>();
    font_runtime::miss(c.owner, c.slot, cp, glyph, draw, source)
}
