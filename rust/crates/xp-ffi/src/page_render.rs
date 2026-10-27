//! Synchronous page draw: one font cache and one bitmap-source loan per pass.
use crate::textm::{paint_glyph, GlyphPaint, Miss};
use core::{ffi::c_void, ptr, slice};
use xp_core::{
    font::raw::FontData,
    glyph::{BitmapSource, Kind, Surface},
    page::{
        self,
        render::{Canvas, Mode, Text},
    },
    textm::{
        self,
        native::{Codepoints, DrawingFace, Engine, Provider},
        FontFace, MeasureMode,
    },
};

#[repr(C)]
pub struct Services {
    context: *mut c_void,
    ensure: unsafe extern "C" fn(*mut c_void, i32) -> u8,
    source: unsafe extern "C" fn(*mut c_void, i32, u8, *mut BitmapSource) -> *const *const FontData,
    resident: unsafe extern "C" fn(*mut c_void, i32, u8, *const u8, usize),
    scan: unsafe extern "C" fn(*mut c_void, i32, u8, *const u8),
    image: unsafe extern "C" fn(*mut c_void, u16, i32, i32, u8),
    miss: Miss,
}
type Observer = unsafe extern "C" fn(*mut c_void, *const textm::draw::Placement);
struct Renderer<'a> {
    engine: Engine,
    provider: &'a Provider,
    services: &'a Services,
    paint: *mut GlyphPaint,
    slot: *const *const FontData,
    font: i32,
    style: u8,
    scan: [u8; 192],
    scan_len: usize,
    scan_font: i32,
    scan_style: u8,
    observer: Option<Observer>,
}
impl Renderer<'_> {
    fn flush_scan(&mut self) {
        if self.scan_len == 0 {
            return;
        }
        self.scan[self.scan_len] = 0;
        unsafe {
            (self.services.scan)(
                self.services.context,
                self.scan_font,
                self.scan_style,
                self.scan.as_ptr(),
            );
        }
        self.scan_len = 0;
    }
    fn record(&mut self, font: i32, style: u8, visual: &[u8]) {
        if self.scan_font != font || self.scan_style != style {
            self.flush_scan();
        }
        self.scan_font = font;
        self.scan_style = style;
        // Flush only at codepoint boundaries; recordText observes the same order.
        let mut pos = 0;
        while let Some(cp) = xp_core::utf8::next(visual, &mut pos) {
            let mut bytes = [0; 4];
            let count = xp_core::utf8::encode(cp, &mut bytes);
            if self.scan_len + count >= self.scan.len() {
                self.flush_scan();
            }
            self.scan[self.scan_len..self.scan_len + count].copy_from_slice(&bytes[..count]);
            self.scan_len += count;
        }
    }
    unsafe fn select(&mut self, font: i32, style: u8) -> bool {
        let source = &(*self.paint).source;
        let stale = source.generation.is_null()
            || (*source.generation).load(core::sync::atomic::Ordering::Relaxed)
                != source.captured_generation;
        if self.slot.is_null() || self.font != font || self.style != style || stale {
            self.slot = (self.services.source)(
                self.services.context,
                font,
                style,
                ptr::addr_of_mut!((*self.paint).source),
            );
            self.font = font;
            self.style = style;
        }
        !self.slot.is_null()
    }
}
impl Canvas for Renderer<'_> {
    fn scanning(&self) -> bool {
        unsafe { (*self.paint).target.scanning != 0 }
    }
    fn font(&mut self, requested: i32, section: i32) -> i32 {
        if requested == section
            || self.engine.exists(requested)
            || unsafe { (self.services.ensure)(self.services.context, requested) != 0 }
        {
            requested
        } else {
            section
        }
    }
    fn ascent(&self, font: i32) -> i32 {
        self.engine.face(font).ascender(0)
    }
    fn advance(&self, t: Text<'_>, mode: MeasureMode) -> i32 {
        self.engine
            .advance(t.font, t.bytes, t.style, t.tracking, t.direction, mode)
    }
    fn top(&self, font: i32, cp: u32, style: u8) -> Option<i32> {
        textm::codepoint_metrics(&mut self.engine.face(font), cp, style).map(|(_, top)| top)
    }
    fn text(&mut self, t: Text<'_>) {
        if t.bytes.is_empty() || t.bytes[0] == 0 {
            return;
        }
        let font = self.engine.resolve(t.font, t.bytes, t.style);
        let mut baseline = self.ascent(font);
        if font != t.font {
            baseline += (i32::from(self.engine.face(t.font).line_height())
                - i32::from(self.engine.face(font).line_height()))
                / 2;
        }
        textm::native::with_visual(self.provider, t.bytes, t.direction, |visual| {
            if self.scanning() {
                self.record(font, t.style, visual);
                return;
            }
            unsafe {
                if font != t.font && self.engine.is_sd(font) {
                    (self.services.resident)(
                        self.services.context,
                        font,
                        t.style,
                        visual.as_ptr(),
                        visual.len(),
                    );
                }
                if !self.select(font, t.style) {
                    return;
                }
                (*self.paint).x = t.x;
                (*self.paint).y = t.y + baseline;
                let mut face = DrawingFace::new(self.slot, self.provider, font);
                textm::draw::walk(
                    &mut face,
                    Codepoints(visual),
                    t.style,
                    t.tracking,
                    false,
                    &mut |placement| {
                        if let Some(observer) = self.observer {
                            observer(self.services.context, &placement);
                        }
                        let kind = if placement.scale_num != placement.scale_den {
                            Kind::Coverage
                        } else {
                            Kind::Normal
                        };
                        paint_glyph(
                            self.paint,
                            self.services.context,
                            self.services.miss,
                            placement,
                            kind,
                            256,
                        );
                    },
                );
            }
        });
    }
    fn end_line(&mut self) {
        self.flush_scan();
    }
    fn rect(&mut self, x: i32, y: i32, w: i32, h: i32, shade: bool) {
        unsafe {
            let target = &(*self.paint).target;
            if target.buffer.is_null() {
                return;
            }
            let buffer = slice::from_raw_parts_mut(target.buffer, target.buffer_len as usize);
            let mut surface = Surface::from_validated(target, buffer);
            if shade {
                surface.shade(x, y, w, h);
            } else {
                surface.fill(x, y, w, h, true);
            }
        }
    }
    fn scaled(&mut self, font: i32, text: &[u8], style: u8, scale: i32, mut x: i32, y: i32) {
        if scale <= 0 {
            return;
        }
        if self.scanning() {
            self.record(font, style, text);
            return;
        }
        unsafe {
            if !self.select(font, style) {
                return;
            }
            for cp in Codepoints(text) {
                (*self.paint).x = x;
                (*self.paint).y = y;
                x += crate::textm::xp_native_draw_scaled_pixels(
                    self.provider,
                    font,
                    cp,
                    style,
                    scale,
                    self.slot,
                    self.services.context,
                    self.paint,
                    self.services.miss,
                );
            }
        }
    }
    fn image(&mut self, i: u16, x: i32, y: i32, placeholder: bool) {
        self.flush_scan();
        unsafe {
            (self.services.image)(self.services.context, i, x, y, u8::from(placeholder));
        }
        // Image decoders may evict font caches before the next text element.
        self.slot = ptr::null();
    }
}
#[allow(clippy::too_many_arguments)]
unsafe fn render(
    arena: *const u8,
    len: usize,
    provider: *const Provider,
    services: *const Services,
    paint: *mut GlyphPaint,
    font: i32,
    x: i32,
    y: i32,
    mode: u8,
    observer: Option<Observer>,
) -> u32 {
    let Ok(page) = page::Page::open(slice::from_raw_parts(arena, len)) else {
        return 1;
    };
    let mode = match mode {
        0 => Mode::All,
        1 => Mode::Images,
        2 => Mode::Placeholders,
        _ => return 1,
    };
    let mut renderer = Renderer {
        engine: Engine::new(*provider),
        provider: &*provider,
        services: &*services,
        paint,
        slot: ptr::null(),
        font: 0,
        style: 0,
        scan: [0; 192],
        scan_len: 0,
        scan_font: 0,
        scan_style: 0,
        observer,
    };
    u32::from(page::render::page(page, &mut renderer, font, x, y, mode).is_err())
}
/// # Safety
/// The arena is decoded and immutable; services, provider and paint follow the
/// synchronous owner/validated-target contract declared in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_page_render(
    arena: *const u8,
    len: usize,
    provider: *const Provider,
    services: *const Services,
    paint: *mut GlyphPaint,
    font: i32,
    x: i32,
    y: i32,
    mode: u8,
) -> u32 {
    render(
        arena, len, provider, services, paint, font, x, y, mode, None,
    )
}
/// # Safety
/// Same as xp_page_render; the observer cannot mutate font owners or retain loans.
#[cfg(feature = "chapter-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_page_render_traced(
    arena: *const u8,
    len: usize,
    provider: *const Provider,
    services: *const Services,
    paint: *mut GlyphPaint,
    font: i32,
    x: i32,
    y: i32,
    mode: u8,
    observer: Observer,
) -> u32 {
    render(
        arena,
        len,
        provider,
        services,
        paint,
        font,
        x,
        y,
        mode,
        Some(observer),
    )
}

// The chapter host renderer has synthetic metrics and no framebuffer. Observe
// primitive ordering through the production traversal, rather than reimplement it.
#[cfg(feature = "chapter-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_page_render_rects(
    arena: *const u8,
    len: usize,
    context: *mut c_void,
    rect: unsafe extern "C" fn(*mut c_void, i32, i32, i32, i32, u8),
    x: i32,
    y: i32,
    mode: u8,
) {
    struct Observer {
        context: *mut c_void,
        rect: unsafe extern "C" fn(*mut c_void, i32, i32, i32, i32, u8),
    }
    impl Canvas for Observer {
        fn scanning(&self) -> bool {
            true
        }
        fn font(&mut self, id: i32, _: i32) -> i32 {
            id
        }
        fn ascent(&self, _: i32) -> i32 {
            0
        }
        fn advance(&self, _: Text<'_>, _: MeasureMode) -> i32 {
            0
        }
        fn top(&self, _: i32, _: u32, _: u8) -> Option<i32> {
            None
        }
        fn text(&mut self, _: Text<'_>) {}
        fn end_line(&mut self) {}
        fn rect(&mut self, x: i32, y: i32, w: i32, h: i32, shade: bool) {
            unsafe {
                (self.rect)(self.context, x, y, w, h, u8::from(shade));
            }
        }
        fn scaled(&mut self, _: i32, _: &[u8], _: u8, _: i32, _: i32, _: i32) {}
        fn image(&mut self, _: u16, _: i32, _: i32, _: bool) {}
    }
    if let Ok(page) = page::Page::open(slice::from_raw_parts(arena, len)) {
        let mut observer = Observer { context, rect };
        let mode = match mode {
            1 => Mode::Images,
            2 => Mode::Placeholders,
            _ => Mode::All,
        };
        let _ = page::render::page(page, &mut observer, 0, x, y, mode);
    }
}
