//! One serialized cache owner; native leaves retain SDK and temporary font resources.
use crate::font_registry::{self as fonts, Owner};
use core::{
    ffi::{c_char, c_void, CStr},
    ptr,
};
use xp_core::{
    font::{
        raw::{FontData, Glyph},
        runtime::{Backend, ClearKind, Physical, Prewarm, SdFontCacheCompat},
    },
    glyph::{Bitmap, BitmapSource, Draw, Kind},
};

extern "C" {
    fn fiFontMalloc(bytes: usize) -> *mut c_void;
    fn fiFontFree(bytes: *mut c_void);
    fn esp_timer_get_time() -> i64;
    fn heap_caps_get_free_size(caps: u32) -> usize;
    fn xp_native_font_cache_diagnostic(
        font: *const FontData,
        event: u32,
        a: u32,
        b: u32,
        c: u32,
        d: u32,
    );
    #[cfg(feature = "app-test")]
    fn xp_native_font_compat_resolve(native: *mut c_void, font: i32, style: u8) -> u8;
    fn xp_native_font_compat_prewarm(
        native: *mut c_void,
        font: i32,
        backend: u8,
        text: *const c_char,
        styles: u8,
        metadata: u8,
        kern_lig: u8,
        accumulate: u8,
    ) -> i32;
    fn xp_native_font_compat_clear(native: *mut c_void, font: i32, backend: u8, resident: u8);
    #[cfg(feature = "app-test")]
    fn xp_native_font_compat_reset(native: *mut c_void, font: i32);
    #[cfg(feature = "app-test")]
    fn xp_native_sd_font_bitmap(font: *const FontData, glyph: *const Glyph) -> *const u8;
}
struct Compat(*const Owner);
impl SdFontCacheCompat for Compat {
    unsafe fn resolve_style(&mut self, font: i32, style: u8) -> u8 {
        if let Some(resolved) = crate::font_sd::resolve(self.0, font, style) {
            return resolved;
        }
        #[cfg(feature = "app-test")]
        {
            xp_native_font_compat_resolve((*self.0).native, font, style)
        }
        #[cfg(not(feature = "app-test"))]
        style
    }
    unsafe fn prewarm(&mut self, font: i32, backend: Backend, request: Prewarm<'_>) -> i32 {
        if backend == Backend::Sd {
            if let Some(missed) = crate::font_sd::prewarm(self.0, font, request) {
                return missed;
            }
            #[cfg(not(feature = "app-test"))]
            return 0;
        }
        xp_native_font_compat_prewarm(
            (*self.0).native,
            font,
            backend as u8,
            request.text.as_ptr(),
            request.styles,
            u8::from(request.metadata_only),
            u8::from(request.load_kern_lig),
            u8::from(request.accumulate),
        )
    }
    unsafe fn clear(&mut self, font: i32, backend: Backend, kind: ClearKind) {
        if backend == Backend::Sd {
            if crate::font_sd::clear(self.0, font, kind) {
                return;
            }
            #[cfg(not(feature = "app-test"))]
            return;
        }
        xp_native_font_compat_clear(
            (*self.0).native,
            font,
            backend as u8,
            u8::from(kind == ClearKind::Resident),
        );
    }
    unsafe fn reset_stats(&mut self, font: i32) {
        if crate::font_sd::reset(self.0, font) {
            return;
        }
        #[cfg(feature = "app-test")]
        xp_native_font_compat_reset((*self.0).native, font);
    }
}
unsafe extern "C" fn allocate(bytes: usize) -> *mut u8 {
    fiFontMalloc(bytes).cast()
}
unsafe extern "C" fn free(bytes: *mut u8) {
    fiFontFree(bytes.cast());
}
unsafe extern "C" fn clock(microseconds: bool) -> u32 {
    let us = esp_timer_get_time() as u64;
    if microseconds {
        us as u32
    } else {
        (us / 1000) as u32
    }
}
unsafe extern "C" fn generation(_: *mut c_void, registry: *const xp_core::font::loaded::Registry) {
    (*registry).invalidate();
}
unsafe extern "C" fn diagnostic(
    _: *mut c_void,
    font: *const FontData,
    event: u32,
    a: u32,
    b: u32,
    c: u32,
    d: u32,
) {
    xp_native_font_cache_diagnostic(font, event, a, b, c, d);
}
fn physical() -> &'static Physical {
    &Physical {
        context: ptr::null_mut(),
        alloc: allocate,
        free,
        clock,
        generation,
        diagnostic: Some(diagnostic),
    }
}
/// # Safety
/// The initialized owner is serialized; source/bitmap loans have ended.
pub(crate) unsafe fn begin(owner: *const Owner) {
    (*(*owner).cache.get()).begin(
        ptr::addr_of!((*owner).registry),
        physical(),
        &mut Compat(owner),
    );
}
/// # Safety
/// Same initialized, serialized owner contract as begin; no view survives prewarm.
pub(crate) unsafe fn finish(owner: *const Owner) {
    (*(*owner).cache.get()).finish(
        ptr::addr_of!((*owner).registry),
        physical(),
        &mut Compat(owner),
    );
}
/// # Safety
/// Same initialized, serialized owner contract as begin; this closes the scope.
pub(crate) unsafe fn end(owner: *const Owner) {
    (*(*owner).cache.get()).end(
        ptr::addr_of!((*owner).registry),
        physical(),
        &mut Compat(owner),
    );
}
/// # Safety
/// Serialized cache mutation; every glyph/source loan has ended.
pub(crate) unsafe fn release(owner: *const Owner, operation: u8) -> usize {
    let cache = &*(*owner).cache.get();
    let registry = ptr::addr_of!((*owner).registry);
    if operation <= 1 {
        cache.clear(
            registry,
            if operation == 0 {
                ClearKind::Frame
            } else {
                ClearKind::Resident
            },
            physical(),
            &mut Compat(owner),
        );
        return 0;
    }
    if operation > 4 || (operation == 2 && cache.scanning()) {
        return 0;
    }
    // Match MemoryManager's MemPool::Default (SDK MALLOC_CAP_DEFAULT).
    let before = heap_caps_get_free_size(1 << 12);
    match operation {
        2 => cache.clear(registry, ClearKind::Frame, physical(), &mut Compat(owner)),
        3 => cache.clear_backend(registry, Backend::Sd, ClearKind::Frame, &mut Compat(owner)),
        4 => cache.clear_backend(
            registry,
            Backend::Vector,
            ClearKind::Resident,
            &mut Compat(owner),
        ),
        _ => unreachable!(),
    }
    heap_caps_get_free_size(1 << 12).saturating_sub(before)
}

/// # Safety
/// Text is terminated and used synchronously; the owner/resources remain live.
pub(crate) unsafe fn prewarm_text(
    owner: *const Owner,
    font: i32,
    text: *const c_char,
    styles: u8,
    accumulate: u8,
) {
    if text.is_null() {
        return;
    }
    (*(*owner).cache.get()).prewarm(
        ptr::addr_of!((*owner).registry),
        font,
        Prewarm {
            text: CStr::from_ptr(text),
            styles,
            metadata_only: false,
            load_kern_lig: true,
            accumulate: accumulate != 0,
        },
        physical(),
        &mut Compat(owner),
    );
}
/// # Safety
/// Text is terminated and borrowed only for this serialized scan operation.
pub(crate) unsafe fn record_text(owner: *const Owner, font: i32, text: *const c_char, style: u8) {
    if text.is_null() {
        return;
    }
    (*(*owner).cache.get()).record(
        ptr::addr_of!((*owner).registry),
        font,
        CStr::from_ptr(text).to_bytes(),
        style,
        physical(),
        &mut Compat(owner),
    );
}
/// # Safety
/// Owner is initialized and serialized against mutation.
pub(crate) unsafe fn scanning(owner: *const Owner) -> u8 {
    u8::from((&*(*owner).cache.get()).scanning())
}
// Native-origin adapters; Rust consumers call the operations above directly.
macro_rules! native_entry {
    ($export:ident => $operation:ident($($arg:ident: $kind:ty),*) $(-> $result:ty)?) => {
        /// # Safety
        /// Owner, text and source loans follow the corresponding Rust operation's contract.
        #[no_mangle]
        pub unsafe extern "C" fn $export($($arg: $kind),*) $(-> $result)? {
            $operation($($arg),*)
        }
    };
}
native_entry!(xp_font_frame_begin => begin(owner: *const Owner));
native_entry!(xp_font_frame_finish => finish(owner: *const Owner));
native_entry!(xp_font_frame_end => end(owner: *const Owner));
native_entry!(xp_font_cache_release => release(owner: *const Owner, operation: u8) -> usize);
native_entry!(xp_font_prewarm_text => prewarm_text(owner: *const Owner, font: i32, text: *const c_char, styles: u8, accumulate: u8));
native_entry!(xp_font_record_text => record_text(owner: *const Owner, font: i32, text: *const c_char, style: u8));
native_entry!(xp_font_scanning => scanning(owner: *const Owner) -> u8);

pub(crate) unsafe fn record_resolved(owner: *const Owner, font: i32, style: u8, text: *const u8) {
    (*(*owner).cache.get()).record_resolved(
        font,
        CStr::from_ptr(text.cast()).to_bytes(),
        style,
        physical(),
    );
}
pub(crate) unsafe fn bitmap(
    owner: *const Owner,
    font: *const FontData,
    glyph: *const Glyph,
) -> Bitmap {
    let data = if let Some(vector) = (*font).vector_bitmap_handler {
        vector((*font).glyph_miss_ctx, glyph)
    } else if !(*font).groups.is_null() {
        let index = glyph.offset_from((*font).glyph) as u32;
        (*(*owner).cache.get()).bitmap(&*font, &*glyph, index, physical())
    } else if !(*font).glyph_miss_ctx.is_null() {
        #[cfg(feature = "app-test")]
        if crate::font_sd::is_native_test_face(owner, font) {
            return Bitmap {
                data: xp_native_sd_font_bitmap(font, glyph),
                len: u32::from((*glyph).data_length),
            };
        }
        crate::font_sd::bitmap(font, glyph)
    } else {
        (*font).bitmap.add((*glyph).data_offset as usize)
    };
    Bitmap {
        data,
        len: if data.is_null() {
            0
        } else {
            (u32::from((*glyph).width)
                * u32::from((*glyph).height)
                * if (*font).is2_bit { 2 } else { 1 })
            .div_ceil(8)
        },
    }
}
#[repr(C)]
pub struct GlyphContext {
    owner: *const Owner,
    slot: *const *const FontData,
}
/// # Safety
/// Context and stable slot remain live; glyph/source loans follow the synchronous raster contract.
#[no_mangle]
pub unsafe extern "C" fn xp_font_glyph_miss(
    context: *mut c_void,
    cp: u32,
    resolved: *const Glyph,
    draw: *mut Draw,
    source: *mut BitmapSource,
) -> Bitmap {
    let context = &*context.cast::<GlyphContext>();
    miss(context.owner, context.slot, cp, resolved, draw, source)
}
pub(crate) unsafe fn miss(
    owner: *const Owner,
    slot: *const *const FontData,
    cp: u32,
    resolved: *const Glyph,
    draw: *mut Draw,
    source: *mut BitmapSource,
) -> Bitmap {
    let mut fallback = core::mem::zeroed();
    let glyph = if !resolved.is_null() {
        resolved
    } else if (*draw).kind == Kind::Bilinear as u8 {
        crate::font::xp_font_lookup(slot.read(), cp)
    } else {
        crate::font::xp_font_metrics(slot.read(), cp, &mut fallback)
    };
    let font = slot.read();
    let mut result = Bitmap::MISS;
    if glyph.is_null() {
        if (*draw).kind == Kind::Normal as u8 || (*draw).kind == Kind::Rotated as u8 {
            fonts::report(
                owner,
                xp_core::textm::native::DiagnosticEvent::MissingGlyph as u32,
                cp,
            );
        }
    } else {
        (*draw).glyph = glyph.read();
        (*draw).ascender = (*font).ascender;
        (*draw).two_bit = u8::from((*font).is2_bit);
        (*draw).solid = u8::from(ptr::eq(glyph, &fallback));
        if (*draw).solid == 0 {
            result = bitmap(owner, font, glyph);
        }
    }
    fonts::xp_font_registry_source(owner, slot.read(), source);
    result
}

#[cfg(feature = "chapter-test")]
mod test_adapter {
    use super::*;
    use xp_core::font::{
        cache::{Cache, Services},
        runtime::FontCacheOwner,
    };
    unsafe extern "C" fn log(
        context: *mut c_void,
        _: *const FontData,
        event: u32,
        a: u32,
        b: u32,
        c: u32,
        d: u32,
    ) {
        let io = &*context.cast::<Services>();
        if let Some(diagnostic) = io.diagnostic {
            diagnostic(io.ctx, event, a, b, c, d);
        }
    }
    unsafe fn io(services: *const Services) -> Physical {
        Physical {
            context: services.cast_mut().cast(),
            alloc: (*services).alloc,
            free: (*services).free,
            clock: (*services).clock,
            generation,
            diagnostic: Some(log),
        }
    }
    unsafe extern "C" fn generation(
        context: *mut c_void,
        _: *const xp_core::font::loaded::Registry,
    ) {
        let services = &*context.cast::<Services>();
        (services.generation)(services.ctx);
    }
    #[no_mangle]
    unsafe extern "C" fn xp_font_test_init(owner: *mut FontCacheOwner) {
        FontCacheOwner::initialize(owner);
    }
    #[no_mangle]
    unsafe extern "C" fn xp_font_test_clear(owner: *mut FontCacheOwner, services: *const Services) {
        (*owner).clear_builtin(&io(services));
    }
    #[no_mangle]
    unsafe extern "C" fn xp_font_test_bitmap(
        owner: *mut FontCacheOwner,
        font: *const FontData,
        glyph: *const Glyph,
        index: u32,
        services: *const Services,
    ) -> *const u8 {
        (*owner).bitmap(&*font, &*glyph, index, &io(services))
    }
    #[no_mangle]
    unsafe extern "C" fn xp_font_test_prewarm(
        owner: *mut FontCacheOwner,
        font: *const FontData,
        text: *const c_char,
        services: *const Services,
    ) -> i32 {
        if font.is_null() || text.is_null() {
            return 0;
        }
        (*owner).prewarm_data(&*font, CStr::from_ptr(text).to_bytes(), &io(services))
    }
    #[no_mangle]
    unsafe extern "C" fn xp_font_test_reset_stats(owner: *const FontCacheOwner) {
        (*owner).reset_stats();
    }
    #[no_mangle]
    unsafe extern "C" fn xp_font_test_view(owner: *mut FontCacheOwner) -> *const Cache {
        (*owner).cache()
    }
    #[no_mangle]
    unsafe extern "C" fn xp_font_test_owner_cache(owner: *const Owner) -> *mut FontCacheOwner {
        (*owner).cache.get()
    }
    #[no_mangle]
    unsafe extern "C" fn xp_font_test_owner_bitmap(
        owner: *const Owner,
        font: *const FontData,
        glyph: *const Glyph,
    ) -> *const u8 {
        bitmap(owner, font, glyph).data
    }
}
