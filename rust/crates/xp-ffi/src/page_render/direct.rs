//! Direct composition for Rust Page owners and the native-origin Page adapter.
use super::*;
use crate::font_registry::{self as fonts, Owner};
use core::cell::Cell;
use xp_core::glyph::{Draw, GlyphTarget};

#[repr(C)]
pub(crate) struct Target {
    pub(crate) glyphs: GlyphTarget,
    pub(crate) fonts: *const Owner,
    pub(crate) native: *mut c_void,
    pub(crate) inverted: u8,
}
#[repr(C)]
pub(crate) struct Request {
    pub(crate) font: i32,
    pub(crate) x: i32,
    pub(crate) y: i32,
    pub(crate) mode: u8,
    pub(crate) plan: *mut Plan,
    pub(crate) bounds: *mut c_void,
    pub(crate) count: usize,
    pub(crate) prepare: u8,
    pub(crate) dither_images: u8,
}
const _: () = {
    assert!(
        core::mem::size_of::<Target>()
            == if cfg!(target_pointer_width = "32") {
                64
            } else {
                80
            }
    );
    assert!(
        core::mem::size_of::<Request>()
            == if cfg!(target_pointer_width = "32") {
                32
            } else {
                48
            }
    );
    assert!(core::mem::offset_of!(Target, fonts) == core::mem::size_of::<GlyphTarget>());
};
struct Direct<'a> {
    arena: &'a [u8],
    target: &'a Target,
    font: Cell<i32>,
    style: Cell<u8>,
    dither_images: bool,
}
extern "C" {
    pub(crate) fn xp_native_render_target(
        out: *mut Target,
        scene_paint: *mut xp_ui::ffi::Paint,
    ) -> u8;
    #[cfg(feature = "chapter-test")]
    fn xp_native_page_test_glyph(
        font: *const FontData,
        placement: *const textm::draw::Placement,
        x: i32,
        y: i32,
        style: u8,
    );
}

#[inline(always)]
pub(super) fn observer() -> Option<Observer> {
    #[cfg(feature = "chapter-test")]
    return Some(observe);
    #[cfg(not(feature = "chapter-test"))]
    None
}

#[cfg(feature = "chapter-test")]
unsafe extern "C" fn observe(
    raw: *mut c_void,
    placement: *const textm::draw::Placement,
    x: i32,
    y: i32,
) {
    let resources = &*raw.cast::<Direct<'_>>();
    let font = Owner::slot(
        resources.target.fonts,
        resources.font.get(),
        resources.style.get(),
    )
    .read();
    xp_native_page_test_glyph(font, placement, x, y, resources.style.get());
}

#[inline(never)]
pub(crate) unsafe fn paint_current(arena: &[u8], font: i32, x: i32, y: i32, mode: u8) -> u32 {
    let request = Request {
        font,
        x,
        y,
        mode,
        plan: ptr::null_mut(),
        bounds: ptr::null_mut(),
        count: 0,
        prepare: 0,
        dither_images: 0,
    };
    paint_request_current(arena, &request)
}

#[inline(never)]
pub(crate) unsafe fn paint_request_current(arena: &[u8], request: &Request) -> u32 {
    let mut target = core::mem::MaybeUninit::uninit();
    xp_native_render_target(target.as_mut_ptr(), ptr::null_mut());
    let target = target.assume_init_mut();
    // Scan state belongs to Rust; the native snapshot reads physical target facts only.
    if !target.fonts.is_null() {
        target.glyphs.scanning = crate::font_runtime::scanning(target.fonts);
    }
    paint(arena, target, request)
}
impl Resources for Direct<'_> {
    fn context(&self) -> *mut c_void {
        (self as *const Self).cast_mut().cast()
    }
    unsafe fn ensure(&self, font: i32) -> bool {
        Owner::ensure(self.target.fonts, font)
    }
    unsafe fn source(
        &self,
        font: i32,
        style: u8,
        source: *mut BitmapSource,
    ) -> *const *const FontData {
        let slot = Owner::slot(self.target.fonts, font, style);
        if slot.is_null() {
            fonts::report(self.target.fonts, 1, font as u32);
            return slot;
        }
        self.font.set(font);
        self.style.set(style);
        fonts::xp_font_registry_source(self.target.fonts, slot.read(), source);
        slot
    }
    unsafe fn resident(&self, font: i32, style: u8, mut text: *const u8, mut len: usize) {
        // Residency batches borrow at most 191 bytes and end on UTF-8 codepoint boundaries.
        while len != 0 {
            let mut count = len.min(191);
            if count < len {
                while count != 0 && text.add(count).read() & 0xc0 == 0x80 {
                    count -= 1;
                }
            }
            if count == 0 {
                break;
            }
            crate::font_sd::resident(self.target.fonts, font, style, text, count, false);
            text = text.add(count);
            len -= count;
        }
    }
    unsafe fn scan(&self, font: i32, style: u8, text: *const u8) {
        let Some(resolved) = resolved_scan_style(self.target.fonts, font, style) else {
            return;
        };
        crate::font_runtime::record_resolved(self.target.fonts, font, resolved, text);
    }
    unsafe fn image(&self, index: u16, x: i32, y: i32, placeholder: bool) {
        let Ok(page) = page::Page::open(self.arena) else {
            return;
        };
        let Ok(element) = page.element(index) else {
            return;
        };
        let Ok((path, source, width, height)) = element.image() else {
            return;
        };
        super::images::paint(
            self.target,
            path,
            source,
            x,
            y,
            i32::from(width),
            i32::from(height),
            placeholder,
            self.dither_images,
        );
    }
    fn miss(&self) -> Miss {
        miss
    }
}
// Return only the style byte so the registry-slot borrow and font pointer end
// before the scan owner consumes the resolved batch.
#[inline(never)]
unsafe fn resolved_scan_style(owner: *const Owner, font: i32, style: u8) -> Option<u8> {
    let entry = (*owner).registry.find(font)?;
    let data = entry.slot(style).read();
    let resolved = (0..4)
        .find(|&candidate| entry.slot(candidate).read() == data)
        .unwrap_or(style & 3);
    Some(resolved | (style & 128))
}

unsafe extern "C" fn miss(
    context: *mut c_void,
    cp: u32,
    resolved: *const xp_core::font::raw::Glyph,
    draw: *mut Draw,
    source: *mut BitmapSource,
) -> xp_core::glyph::Bitmap {
    let context = &*context.cast::<Direct<'_>>();
    let slot = Owner::slot(
        context.target.fonts,
        context.font.get(),
        context.style.get(),
    );
    crate::font_runtime::miss(context.target.fonts, slot, cp, resolved, draw, source)
}

/// The caller holds the Page, physical target and font owners for the pass.
/// This function creates no native Page facade and no page callback table.
#[inline(never)]
pub(crate) unsafe fn paint(arena: &[u8], target: &Target, request: &Request) -> u32 {
    if !target.glyphs.valid() || target.glyphs.buffer.is_null() || target.fonts.is_null() {
        return 1;
    }
    let provider = fonts::xp_font_registry_provider(target.fonts.cast());
    let mut paint = GlyphPaint {
        target: ptr::read(&target.glyphs),
        source: core::mem::zeroed(),
        x: 0,
        y: 0,
        state: 1,
    };
    let resources = Direct {
        arena,
        target,
        font: Cell::new(request.font),
        style: Cell::new(0),
        dither_images: request.dither_images != 0,
    };
    #[cfg(feature = "reader-strips")]
    if !request.plan.is_null() {
        return page_plan::render_plan(
            arena.as_ptr(),
            arena.len(),
            &provider,
            &resources,
            &mut paint,
            request.font,
            request.x,
            request.y,
            request.mode,
            request.bounds.cast(),
            request.count,
            request.plan,
            request.prepare,
        );
    }
    render(
        arena.as_ptr(),
        arena.len(),
        &provider,
        &resources,
        &mut paint,
        request.font,
        request.x,
        request.y,
        request.mode,
        observer(),
        #[cfg(feature = "strip-spike")]
        None,
    )
}

/// # Safety
/// Decoded arena, target and request obey xp_page_paint.h's synchronous loans.
#[no_mangle]
pub unsafe extern "C" fn xp_page_paint(
    arena: *const u8,
    len: usize,
    target: *const Target,
    request: *const Request,
) -> u32 {
    if arena.is_null() || target.is_null() || request.is_null() || len > isize::MAX as usize {
        return 1;
    }
    paint(slice::from_raw_parts(arena, len), &*target, &*request)
}
