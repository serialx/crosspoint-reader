//! Synchronous page draw: one font cache and one bitmap-source loan per pass.
#[cfg(feature = "reader-strips")]
#[path = "page_plan.rs"]
mod page_plan;
#[cfg(feature = "strip-spike")]
#[path = "strip_state.rs"]
mod strip_state;
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
    #[cfg(feature = "reader-strips")]
    plan: Option<&'a mut page_plan::Plan>,
    #[cfg(feature = "strip-spike")]
    strip: Option<strip_state::State<'a>>,
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
    #[cfg(feature = "strip-spike")]
    fn element(&mut self, index: u16, e: page::Element<'_>, x: i32, y: i32) -> bool {
        match self.strip.as_mut() {
            Some(s) => unsafe { s.element(&(*self.paint).target, index, e, x, y) },
            None => true,
        }
    }

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
        #[cfg(feature = "reader-strips")]
        self.record_command(page_plan::Command::text(t));
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
                        #[cfg(feature = "strip-spike")]
                        if let Some(s) = self.strip.as_mut() {
                            s.stats.glyphs += 1;
                            if s.prepare {
                                let target = &(*self.paint).target;
                                if placement.glyph.is_null() {
                                    s.unbounded(target);
                                    #[cfg(feature = "reader-strips")]
                                    if let Some(plan) = self.plan.as_mut() {
                                        plan.unbounded(target.height);
                                    }
                                } else {
                                    let g = *placement.glyph;
                                    let n = i32::from(placement.scale_num);
                                    let d = i32::from(placement.scale_den).max(1);
                                    let left = t.x + placement.x + i32::from(g.left) * n / d;
                                    let top =
                                        t.y + baseline + placement.y - i32::from(g.top) * n / d;
                                    let rect = xp_core::glyph::Clip {
                                        left,
                                        top,
                                        right: left + (i32::from(g.width) * n + d - 1) / d,
                                        bottom: top + (i32::from(g.height) * n + d - 1) / d,
                                    };
                                    s.add(target, rect);
                                    #[cfg(feature = "reader-strips")]
                                    if let Some(plan) = self.plan.as_mut() {
                                        plan.add(target, rect);
                                    }
                                }
                                return;
                            }
                        }
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
        #[cfg(feature = "reader-strips")]
        self.record_command(page_plan::Command::rect(x, y, w, h, shade));
        #[cfg(feature = "strip-spike")]
        if let Some(s) = self.strip.as_mut() {
            if s.prepare {
                unsafe {
                    s.add(
                        &(*self.paint).target,
                        xp_core::glyph::Clip {
                            left: x,
                            top: y,
                            right: x + w,
                            bottom: y + h,
                        },
                    );
                }
                return;
            }
        }
        unsafe {
            let target = &(*self.paint).target;
            if target.buffer.is_null() {
                return;
            }
            let buffer = slice::from_raw_parts_mut(target.buffer, target.buffer_len as usize);
            #[cfg(feature = "strip-spike")]
            if self.strip.is_some() && !shade {
                use embedded_graphics_core::{
                    draw_target::DrawTarget,
                    geometry::{Point, Size},
                    pixelcolor::BinaryColor,
                    primitives::Rectangle,
                };
                // Descriptor ownership stays with GlyphPaint; the target borrows its pixel loan.
                let descriptor = core::ptr::read(target);
                if let Some(mut strip) = xp_core::strip::Strip::new(descriptor, buffer) {
                    let _ = strip.fill_solid(
                        &Rectangle::new(
                            Point::new(x, y),
                            Size::new(w.max(0) as u32, h.max(0) as u32),
                        ),
                        BinaryColor::Off,
                    );
                }
                return;
            }
            let mut surface = Surface::from_validated(target, buffer);
            if shade {
                surface.shade(x, y, w, h);
            } else {
                surface.fill(x, y, w, h, true);
            }
        }
    }
    fn scaled(&mut self, font: i32, text: &[u8], style: u8, scale: i32, mut x: i32, y: i32) {
        #[cfg(feature = "reader-strips")]
        self.record_command(page_plan::Command::scaled(font, text, style, scale, x, y));
        #[cfg(feature = "strip-spike")]
        if let Some(s) = self.strip.as_mut() {
            if s.prepare {
                unsafe {
                    s.unbounded(&(*self.paint).target);
                }
                return;
            }
        }
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
        #[cfg(feature = "reader-strips")]
        self.record_command(page_plan::Command::image(i, x, y, placeholder));
        self.flush_scan();
        #[cfg(feature = "strip-spike")]
        if self.strip.as_ref().is_some_and(|s| s.prepare) {
            return;
        }
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
    #[cfg(feature = "strip-spike")] strip: Option<strip_state::State<'_>>,
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
        #[cfg(feature = "reader-strips")]
        plan: None,
        #[cfg(feature = "strip-spike")]
        strip,
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
        arena,
        len,
        provider,
        services,
        paint,
        font,
        x,
        y,
        mode,
        None,
        #[cfg(feature = "strip-spike")]
        None,
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
        #[cfg(feature = "strip-spike")]
        None,
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
            false
        }
        fn font(&mut self, id: i32, _: i32) -> i32 {
            id
        }
        fn ascent(&self, _: i32) -> i32 {
            12
        }
        fn advance(&self, text: Text<'_>, _: MeasureMode) -> i32 {
            let mut pos = 0;
            let mut width = 0;
            let mut previous = 0;
            while let Some(cp) = xp_core::utf8::next(text.bytes, &mut pos) {
                if xp_core::text::combining(cp) {
                    continue;
                }
                width += 8;
                if previous != 0 && previous != 32 && cp != 32 {
                    width += i32::from(text.tracking);
                }
                previous = cp;
            }
            width
        }
        fn top(&self, _: i32, _: u32, _: u8) -> Option<i32> {
            Some(10)
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

/// # Safety
/// Same loans as xp_page_render. Bounds has one entry per element, stats is writable.
/// prepare=1 initializes bounds without pixels; reuse only with identical page, fonts,
/// orientation and origin. prepare=0 renders into the validated borrowed strip.
#[cfg(feature = "strip-spike")]
#[no_mangle]
pub unsafe extern "C" fn xp_page_strip_render(
    arena: *const u8,
    len: usize,
    provider: *const Provider,
    services: *const Services,
    paint: *mut GlyphPaint,
    font: i32,
    x: i32,
    y: i32,
    mode: u8,
    bounds: *mut strip_state::RowBounds,
    count: usize,
    stats: *mut strip_state::Stats,
    prepare: u8,
) -> u32 {
    if arena.is_null()
        || provider.is_null()
        || services.is_null()
        || paint.is_null()
        || bounds.is_null()
        || stats.is_null()
    {
        return 1;
    }
    let Ok(page) = page::Page::open(slice::from_raw_parts(arena, len)) else {
        return 1;
    };
    let n = usize::from(page.element_count());
    if count < n || !(*paint).target.valid() {
        return 1;
    }
    let bounds = slice::from_raw_parts_mut(bounds, n);
    if prepare != 0 {
        bounds.fill(strip_state::RowBounds {
            start: (*paint).target.height,
            end: 0,
        });
    }
    let state = strip_state::State {
        bounds,
        stats: &mut *stats,
        prepare: prepare != 0,
        current: 0,
    };
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
        None,
        Some(state),
    )
}
