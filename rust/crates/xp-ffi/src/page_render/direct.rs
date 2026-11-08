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
}
extern "C" {
    fn xp_native_render_target(out: *mut Target);
}

pub(crate) unsafe fn paint_current(arena: &[u8], font: i32, x: i32, y: i32, mode: u8) -> u32 {
    let mut target = core::mem::MaybeUninit::uninit();
    xp_native_render_target(target.as_mut_ptr());
    let request = Request {
        font,
        x,
        y,
        mode,
        plan: ptr::null_mut(),
        bounds: ptr::null_mut(),
        count: 0,
        prepare: 0,
    };
    paint(arena, target.assume_init_ref(), &request)
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
        // Preserve the parent's fallback prewarm partitions without copying them.
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
            fonts::xp_native_sd_font_resident_slice(
                self.target.native,
                font,
                style,
                text,
                count,
                0,
            );
            text = text.add(count);
            len -= count;
        }
    }
    unsafe fn scan(&self, font: i32, style: u8, text: *const u8) {
        let Some(entry) = (*self.target.fonts).registry.find(font) else {
            return;
        };
        let data = entry.slot(style).read();
        let resolved = (0..4)
            .find(|&candidate| entry.slot(candidate).read() == data)
            .unwrap_or(style & 3);
        fonts::xp_native_font_record_resolved_text(
            self.target.native,
            font,
            resolved | (style & 128),
            text,
        );
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
        );
    }
    fn miss(&self) -> Miss {
        miss
    }
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
    let mut fallback = core::mem::zeroed();
    let glyph = if !resolved.is_null() {
        resolved
    } else if (*draw).kind == Kind::Bilinear as u8 {
        crate::font::xp_font_lookup(slot.read(), cp)
    } else {
        crate::font::xp_font_metrics(slot.read(), cp, &mut fallback)
    };
    let font = slot.read();
    let mut bitmap = xp_core::glyph::Bitmap {
        data: ptr::null(),
        len: 0,
    };
    if glyph.is_null() {
        if (*draw).kind == Kind::Normal as u8 || (*draw).kind == Kind::Rotated as u8 {
            fonts::report(context.target.fonts, 3, cp);
        }
    } else {
        (*draw).glyph = glyph.read();
        (*draw).ascender = (*font).ascender;
        (*draw).two_bit = u8::from((*font).is2_bit);
        (*draw).solid = u8::from(ptr::eq(glyph, &fallback));
        if (*draw).solid == 0 {
            bitmap = fonts::xp_native_font_bitmap_miss(context.target.native, font, glyph);
        }
    }
    fonts::xp_font_registry_source(context.target.fonts, slot.read(), source);
    bitmap
}

/// The caller holds the Page, physical target and font owners for the pass.
/// This function creates no native Page facade and no page callback table.
pub(crate) unsafe fn paint(arena: &[u8], target: &Target, request: &Request) -> u32 {
    if !target.glyphs.valid() || target.glyphs.buffer.is_null() || target.fonts.is_null() {
        return 1;
    }
    let provider = fonts::xp_font_registry_provider(target.fonts);
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
        None,
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
